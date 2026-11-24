"""PIE에서 M1 통신 시나리오를 실행하고 화면을 Saved/M1에 저장한다. 8765 포트가 비어 있어야 한다."""
import base64, os, socket, subprocess, sys, threading, time
from mcp_call import Unreal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "Saved", "M1")
os.makedirs(OUT, exist_ok=True)
APP = "EditorToolset.EditorAppToolset"
u = Unreal()


def emulator():
    return subprocess.Popen([sys.executable, os.path.join(HERE, "emulator", "emulator.py"), "--chaos"], stdout=subprocess.DEVNULL)


def shot(name):
    if not u.call(APP, "IsPIERunning"):
        raise SystemExit("FAILED: PIE stopped before " + name)
    def find(v):
        if isinstance(v, dict):
            if v.get("type") == "image" and v.get("data"):
                return v["data"]
            if isinstance(v.get("data"), str) and len(v["data"]) > 1000:
                return v["data"]
            v = list(v.values())
        if isinstance(v, list):
            return next((r for r in map(find, v) if r), None)
    data = find(u.call(APP, "CaptureEditorImage"))
    if data:
        open(os.path.join(OUT, name + ".png"), "wb").write(base64.b64decode(data))
    print("shot", name, bool(data), flush=True)


def silent_server(stop):
    """핸드셰이크만 하고 데이터를 보내지 않는 서버."""
    sys.path.insert(0, os.path.join(HERE, "emulator"))
    import asyncio, emulator as em

    async def client(r, w):
        try:
            await em.handshake(r, w)
            await r.read()
        except (OSError, EOFError, asyncio.IncompleteReadError):
            pass
        finally:
            w.close()

    async def main():
        server = await asyncio.start_server(client, "127.0.0.1", 8765)
        while not stop.is_set():
            await asyncio.sleep(0.1)
        server.close()
    asyncio.run(main())


with socket.socket() as s:
    if s.connect_ex(("127.0.0.1", 8765)) == 0:
        raise SystemExit("port 8765 in use")
proc = emulator()
try:
    u.call(APP, "StartPIE", options={"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 4})
    shot("1_live"); time.sleep(12); shot("2_live_later")
    proc.kill(); proc.wait(); time.sleep(4); shot("3_server_killed")
    proc = emulator(); time.sleep(10); shot("4_recovered")
    proc.kill(); proc.wait()
    stop = threading.Event(); th = threading.Thread(target=silent_server, args=(stop,), daemon=True); th.start()
    time.sleep(14); shot("5_silent_server"); stop.set(); th.join(5)
    proc = emulator(); time.sleep(14); shot("6_recovered_again")
finally:
    proc.kill()
    try:
        u.call(APP, "StopPIE")
    except RuntimeError as e:
        print(e)
