"""가상 배터리 셀 공장 설비 에뮬레이터 (표준 라이브러리만 사용).

WebSocket(ws://127.0.0.1:8765)으로 설비들의 합성 텔레메트리를 10Hz로 보낸다.

    python Tools/emulator/emulator.py [--port 8765] [--seed 7] [--chaos]

--chaos: 일부 프레임을 중복·지연 전송해 수신 측의 순서 처리를 시험한다.
"""
import argparse
import asyncio
import base64
import hashlib
import json
import random
import struct
import time
import uuid

import plant as plant_model

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MAX_PAYLOAD = 64 * 1024


class Emulator:
    """공정 모델에 측정 잡음과 프로토콜을 입힌다. 시드가 같으면 같은 순서의 값을 만든다."""

    def __init__(self, seed):
        self.plant = plant_model.Plant(plant_model.load_configs())
        self.rng = random.Random(seed)
        self.session = uuid.uuid4().hex  # 에뮬레이터 재시작을 수신 측이 구분하는 값
        self.seq = 0

    def step(self):
        self.plant.step()
        self.seq += 1
        machines = []
        for i, m in enumerate(self.plant.machines):
            machines.append({
                "id": m.id,
                "state": m.state,
                "temperature": round(m.temp + self.rng.gauss(0, 0.2), 2),
                "vibration": round(max(0.0, m.vibration + self.rng.gauss(0, 0.03)), 3),
                "throughput": round(m.throughput, 3),
                "capacity": round(m.capacity, 3),
                "speed": m.speed,
                "cooling": round(m.cooling, 4),
                "wear": round(m.wear, 4),
                "maint": round(m.maint, 2),
                "bypass": m.bypass,
                "buffer": round(self.plant.buffers[i], 3) if i < len(self.plant.buffers) else 0.0,
            })
        return {"type": "telemetry", "v": 2, "session": self.session, "seq": self.seq, "ts": int(time.time() * 1000),
                "produced": round(self.plant.produced, 4), "machines": machines}

    def handle(self, text):
        """명령을 처리하고 응답을 돌려준다. 명령이 아니면 None."""
        try:
            msg = json.loads(text)
        except ValueError:
            return None
        if not isinstance(msg, dict) or msg.get("type") != "command" or msg.get("v") != 2 or not isinstance(msg.get("id"), str):
            return None
        ack = {"type": "ack", "v": 2, "id": msg["id"], "ok": True, "error": ""}
        try:
            self.plant.command(msg.get("action"), msg.get("machine"), msg.get("value"))
        except plant_model.CommandError as e:
            ack.update(ok=False, error=str(e))
        print(f"command {msg.get('action')} {msg.get('machine')} -> {'ok' if ack['ok'] else ack['error']}", flush=True)
        return ack


def frame(opcode, payload):
    n = len(payload)
    if n < 126:
        head = struct.pack("!BB", 0x80 | opcode, n)
    elif n < 65536:
        head = struct.pack("!BBH", 0x80 | opcode, 126, n)
    else:
        head = struct.pack("!BBQ", 0x80 | opcode, 127, n)
    return head + payload


async def read_frame(reader):
    b0, b1 = await reader.readexactly(2)
    n = b1 & 0x7F
    if n == 126:
        n = struct.unpack("!H", await reader.readexactly(2))[0]
    elif n == 127:
        n = struct.unpack("!Q", await reader.readexactly(8))[0]
    if n > MAX_PAYLOAD or not b1 & 0x80:  # 클라이언트 프레임은 반드시 마스킹되어야 한다
        raise ConnectionError("bad frame")
    mask = await reader.readexactly(4)
    data = await reader.readexactly(n)
    return b0 & 0x0F, bytes(b ^ mask[i & 3] for i, b in enumerate(data))


async def handshake(reader, writer):
    request = (await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 5)).decode("latin-1")
    headers = {}
    for line in request.split("\r\n")[1:]:
        key, sep, value = line.partition(":")
        if sep:
            headers[key.strip().lower()] = value.strip()
    key = headers.get("sec-websocket-key")
    # 브라우저 페이지가 로컬 서버에 접속하지 못하게 Origin이 로컬이 아니면 거부한다.
    origin = headers.get("origin", "")
    host = origin.split("://")[-1].split("/")[0].rsplit(":", 1)[0]
    if not key or (origin and host not in ("127.0.0.1", "localhost")):
        writer.write(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
        await writer.drain()
        raise ConnectionError(f"handshake rejected origin={origin!r}")
    accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    reply = ["HTTP/1.1 101 Switching Protocols", "Upgrade: websocket", "Connection: Upgrade",
             f"Sec-WebSocket-Accept: {accept}"]
    if headers.get("sec-websocket-protocol"):
        reply.append("Sec-WebSocket-Protocol: " + headers["sec-websocket-protocol"].split(",")[0].strip())
    writer.write(("\r\n".join(reply) + "\r\n\r\n").encode())
    await writer.drain()


async def serve(args):
    plant = Emulator(args.seed)
    clients = set()

    async def on_client(reader, writer):
        try:
            await handshake(reader, writer)
            clients.add(writer)
            print(f"connected ({len(clients)})", flush=True)
            while True:
                op, data = await read_frame(reader)
                if op == 0x8:
                    writer.write(frame(0x8, data[:2]))
                    await writer.drain()
                    break
                if op == 0x1:
                    ack = plant.handle(data.decode("utf-8", "replace"))
                    if ack:
                        writer.write(frame(0x1, json.dumps(ack).encode()))
                if op == 0x9:
                    writer.write(frame(0xA, data))
        except (ConnectionError, asyncio.IncompleteReadError, asyncio.LimitOverrunError,
                asyncio.TimeoutError, OSError) as e:
            print(f"client ended: {type(e).__name__} {e}", flush=True)
        finally:
            if writer in clients:
                clients.discard(writer)
                print(f"disconnected ({len(clients)})", flush=True)
            writer.close()

    server = await asyncio.start_server(on_client, "127.0.0.1", args.port)
    print(f"ws://127.0.0.1:{args.port} session={plant.session}", flush=True)
    chaos = random.Random(args.seed + 1)
    held = None
    dt = plant_model.DT
    async with server:
        next_tick = time.monotonic()
        while True:
            payload = json.dumps(plant.step()).encode()
            out = [payload]
            if args.chaos:
                if held:
                    out.append(held)  # 새 프레임 다음에 옛 프레임을 보낸다
                    held = None
                elif chaos.random() < 0.1:
                    held, out = payload, []
                elif chaos.random() < 0.1:
                    out.append(payload)
            for w in list(clients):
                # 느린 클라이언트 때문에 버퍼가 무한히 커지지 않게 프레임을 건너뛴다.
                if not w.is_closing() and w.transport.get_write_buffer_size() < 1 << 20:
                    for p in out:
                        w.write(frame(0x1, p))
            next_tick += dt
            await asyncio.sleep(max(0.0, next_tick - time.monotonic()))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--chaos", action="store_true")
    try:
        asyncio.run(serve(ap.parse_args()))
    except KeyboardInterrupt:
        pass
