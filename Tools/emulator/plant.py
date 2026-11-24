"""직렬 공정 모델. Source/MetaTwin/TwinPlant.cpp와 같은 수식이어야 한다.

수식을 바꾸면 `python Tools/emulator/plant.py`로 기준 데이터를 다시 만들고 C++ 테스트를 통과시킨다.
"""
import json
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
LAYOUT = os.path.join(ROOT, "Content", "Data", "plant_layout.json")
GOLDEN = os.path.join(ROOT, "Source", "MetaTwin", "Tests", "plant_golden.json")

DT = 0.1
BUFFER_CAP, BUFFER_INIT = 12.0, 6.0
TAU = 12.0                      # 열 시정수(s)
WARN_RISE, TRIP_RISE, RESET_RISE = 15.0, 22.0, 4.0
RAMP = 0.05                     # 열화 속도(1/s)
COOLING_FAULT, WEAR_FAULT = 0.25, 0.5
BYPASS_RATE = 0.7
MAINT_SECONDS = 20.0
SPEED_MIN, SPEED_MAX = 0.3, 1.0


class CommandError(Exception):
    pass


class Machine:
    def __init__(self, cfg):
        self.id, self.base, self.vib, self.rate, self.heat = cfg["id"], float(cfg["temp"]), float(cfg["vib"]), float(cfg["rate"]), float(cfg["heat"])
        self.temp = self.base + self.heat
        self.speed = self.cooling = self.cooling_target = self.wear = self.wear_target = 1.0
        self.maint = 0.0
        self.bypass = self.tripped = False
        self.throughput = self.capacity = 0.0
        self.trips = 0

    @property
    def state(self):
        if self.maint > 0:
            return "maintenance"
        if self.tripped:
            return "fault"
        if self.temp > self.base + WARN_RISE or self.cooling < 0.8 or self.wear < 0.8:
            return "warning"
        return "running"

    @property
    def vibration(self):
        load = 0.0 if self.bypass else self.throughput / self.rate
        return self.vib * (1.0 + 0.6 * load + 1.5 * (1.0 - self.wear))


def approach(value, target, step):
    return min(target, value + step) if value < target else max(target, value - step)


class Plant:
    def __init__(self, configs):
        self.machines = [Machine(c) for c in configs]
        self.buffers = [BUFFER_INIT] * (len(configs) - 1)
        self.produced = 0.0

    def step(self, dt=DT):
        last = len(self.machines) - 1
        for i in range(last, -1, -1):  # 뒤 공정부터 당겨 간다.
            m = self.machines[i]
            maintaining = m.maint > 0
            if maintaining:
                m.maint = max(0.0, m.maint - dt)
                if m.maint < 1e-9:
                    m.maint = 0.0
                    m.cooling = m.cooling_target = m.wear = m.wear_target = 1.0
            m.cooling = approach(m.cooling, m.cooling_target, RAMP * dt)
            m.wear = approach(m.wear, m.wear_target, RAMP * dt)
            if m.tripped and m.temp < m.base + RESET_RISE:
                m.tripped = False

            if m.bypass:
                m.capacity = m.rate * BYPASS_RATE
            elif maintaining or m.tripped:
                m.capacity = 0.0
            else:
                m.capacity = m.rate * m.speed * m.wear
            amount = m.capacity * dt / 60.0
            if i > 0:
                amount = min(amount, self.buffers[i - 1])
            if i < last:
                amount = min(amount, BUFFER_CAP - self.buffers[i])
            if i > 0:
                self.buffers[i - 1] -= amount
            if i < last:
                self.buffers[i] += amount
            else:
                self.produced += amount
            m.throughput = amount * 60.0 / dt

            load = 0.0 if m.bypass else m.throughput / m.rate
            m.temp += (m.base + m.heat * load / m.cooling - m.temp) * dt / TAU
            if not m.bypass and not maintaining and not m.tripped and m.temp > m.base + TRIP_RISE:
                m.tripped = True
                m.trips += 1

    def command(self, action, machine_id, value=None):
        m = next((x for x in self.machines if x.id == machine_id), None)
        if m is None:
            raise CommandError("unknown machine")
        if action == "inject_cooling":
            m.cooling_target = COOLING_FAULT
        elif action == "inject_wear":
            m.wear_target = WEAR_FAULT
        elif action == "set_speed":
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not SPEED_MIN <= value <= SPEED_MAX:
                raise CommandError("speed out of range")
            m.speed = float(value)
        elif action == "set_bypass":
            if value not in (0, 1):
                raise CommandError("bypass must be 0 or 1")
            m.bypass = bool(value)
        elif action == "maintain":
            if m.maint > 0:
                raise CommandError("already in maintenance")
            m.maint = MAINT_SECONDS
            m.tripped = False
        else:
            raise CommandError("unknown action")


def load_configs():
    with open(LAYOUT, encoding="utf-8") as f:
        return json.load(f)["machines"]


# (시각 s, 동작, 설비, 값)
GOLDEN_SCRIPT = [
    (5, "inject_cooling", "COATER-01", 0), (60, "set_speed", "COATER-01", 0.5), (90, "set_speed", "COATER-01", 1.0),
    (110, "maintain", "COATER-01", 0), (140, "inject_wear", "FORMATION-01", 0), (170, "set_bypass", "FORMATION-01", 1),
    (190, "maintain", "FORMATION-01", 0), (220, "set_bypass", "FORMATION-01", 0),
]


def write_golden():
    plant, rows = Plant(load_configs()), []
    for n in range(2600):
        for t, action, mid, value in GOLDEN_SCRIPT:
            if n == round(t / DT):
                plant.command(action, mid, value)
        plant.step()
        if n % 50 == 49:
            rows.append({"step": n + 1, "produced": plant.produced, "buffers": list(plant.buffers),
                         "machines": [[m.temp, m.throughput, m.cooling, m.wear, m.trips] for m in plant.machines],
                         "states": [m.state for m in plant.machines]})
    with open(GOLDEN, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"script": [{"step": round(t / DT), "action": a, "machine": m, "value": v} for t, a, m, v in GOLDEN_SCRIPT], "rows": rows}, f, indent=0)
    return rows


if __name__ == "__main__":
    for r in write_golden()[::4]:
        print(r["step"] * DT, round(r["produced"], 1), [s[:4] for s in r["states"]], [round(m[0]) for m in r["machines"]])
