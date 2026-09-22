#!/usr/bin/env python3
"""
esp32_simulator.py — Simulador do firmware ESP32 para testar a ponte serial
SEM hardware (estratégia mock-first da F1).

Emula o lado ESP32 do protocolo JSON-lines:
  - Envia telemetria a cada ~2s (posição avança em direção ao goal).
  - Responde a comandos: set_destination, emergency_stop, ping, request_telemetry.
  - Emite ack para cada comando de navegação e event de obstáculo ocasional.

Uso com um PAR de portas seriais virtuais (ver README para criar o par):
    python esp32_simulator.py --port <porta_lado_ESP32>
    # e noutro terminal:
    python serial_bridge.py --port <porta_lado_RPi> --demo

Ou modo autoteste em loopback (sem hardware, sem porta física):
    python esp32_simulator.py --selftest
"""

from __future__ import annotations

import argparse
import json
import math
import threading
import time
from dataclasses import dataclass, field

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None  # permite --selftest sem pyserial


@dataclass
class SimState:
    lat: float = -3.1019
    lon: float = -60.0250
    heading: float = 0.0
    battery_mv: int = 8000
    obs_dist: int = 200
    thrust_l: int = 0
    thrust_r: int = 0
    state: str = "IDLE_HOLDING_POSITION"
    mission_id: str = ""
    active_leg: int = 0
    progress: float = 0.0
    last_command_id: str = ""
    goal_lat: float = -3.1019
    goal_lon: float = -60.0250
    home_lat: float = -3.1019
    home_lon: float = -60.0250


class Esp32Simulator:
    def __init__(self, write_line, state: SimState | None = None) -> None:
        self._write_line = write_line
        self.s = state or SimState()

    # ── telemetria ───────────────────────────────────────────────────
    def telemetry(self) -> dict:
        return {
            "type": "telemetry",
            "ts": int(time.time()),
            "lat": round(self.s.lat, 6),
            "lon": round(self.s.lon, 6),
            "hdg": round(self.s.heading, 1),
            "obs": self.s.obs_dist,
            "bat": self.s.battery_mv,
            "thrust_l": self.s.thrust_l,
            "thrust_r": self.s.thrust_r,
            "state": self.s.state,
            "mission_id": self.s.mission_id,
            "active_leg": self.s.active_leg,
            "progress": round(self.s.progress, 2),
        }

    # ── física simplificada: avança em direção ao goal ───────────────
    def step(self, dt: float = 2.0) -> None:
        if self.s.state not in ("NAVIGATING_TO_GOAL", "RETURNING_TO_HOME"):
            self.s.thrust_l = self.s.thrust_r = 0
            return

        tgt_lat = self.s.goal_lat if self.s.state == "NAVIGATING_TO_GOAL" else self.s.home_lat
        tgt_lon = self.s.goal_lon if self.s.state == "NAVIGATING_TO_GOAL" else self.s.home_lon

        dlat = tgt_lat - self.s.lat
        dlon = tgt_lon - self.s.lon
        dist = math.hypot(dlat, dlon)
        if dist < 1e-6:
            self.s.state = "IDLE_HOLDING_POSITION"
            self.s.progress = 1.0
            self.s.thrust_l = self.s.thrust_r = 0
            self._write_line({"type": "event", "event": "mission_completed", "value": 1, "ts": int(time.time())})
            return

        # passo ~ equivalente a alguns metros por ciclo
        step_frac = min(1.0, (5e-4 * dt) / max(dist, 1e-6))
        self.s.lat += dlat * step_frac
        self.s.lon += dlon * step_frac
        self.s.heading = (math.degrees(math.atan2(dlon, dlat)) + 360.0) % 360.0
        self.s.thrust_l = 120
        self.s.thrust_r = 120
        self.s.battery_mv = max(6000, self.s.battery_mv - 5)
        self.s.progress = min(0.99, self.s.progress + step_frac)

    # ── despacho de comandos ─────────────────────────────────────────
    def handle(self, msg: dict) -> None:
        cmd = msg.get("cmd", "")
        if cmd == "ping":
            self._write_line({"type": "pong"})
            return
        if cmd == "request_telemetry":
            self._write_line(self.telemetry())
            return

        cid = msg.get("command_id", "")
        if not cid:
            self._write_line({"type": "ack", "command_id": "", "ok": False})
            return
        if cid == self.s.last_command_id:
            self._write_line({"type": "ack", "command_id": cid, "ok": True})
            return

        if cmd == "set_destination":
            self.s.mission_id = msg.get("mission_id", "")
            self.s.home_lat, self.s.home_lon = self.s.lat, self.s.lon
            self.s.goal_lat = float(msg.get("lat", self.s.lat))
            self.s.goal_lon = float(msg.get("lon", self.s.lon))
            self.s.active_leg = 0
            self.s.progress = 0.0
            self.s.state = "NAVIGATING_TO_GOAL"
            self.s.last_command_id = cid
            self._write_line({"type": "ack", "command_id": cid, "ok": True})
        elif cmd == "emergency_stop":
            self.s.goal_lat, self.s.goal_lon = self.s.home_lat, self.s.home_lon
            self.s.progress = 0.0
            self.s.state = "RETURNING_TO_HOME"
            self.s.last_command_id = cid
            self._write_line({"type": "ack", "command_id": cid, "ok": True})
        else:
            self._write_line({"type": "ack", "command_id": cid, "ok": False})


# ── runner sobre porta serial real/virtual ───────────────────────────
def run_serial(port: str, baud: int) -> None:
    if serial is None:
        raise SystemExit("pyserial não instalado. Rode: pip install -r requirements.txt")
    ser = serial.Serial(port, baud, timeout=0.2)

    def write_line(obj: dict) -> None:
        ser.write((json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8"))
        ser.flush()

    sim = Esp32Simulator(write_line)
    stop = threading.Event()

    def rx_loop() -> None:
        buf = bytearray()
        while not stop.is_set():
            chunk = ser.read(256)
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    try:
                        sim.handle(json.loads(line))
                    except json.JSONDecodeError:
                        pass

    t = threading.Thread(target=rx_loop, daemon=True)
    t.start()
    print(f"[SIM] ESP32 simulado em {port} @ {baud}. Ctrl+C para sair.")
    try:
        while True:
            sim.step(dt=2.0)
            write_line(sim.telemetry())
            time.sleep(2.0)
    except KeyboardInterrupt:
        print("\n[SIM] Encerrando...")
    finally:
        stop.set()
        t.join(timeout=1.0)
        ser.close()


# ── autoteste em loopback (sem porta física) ─────────────────────────
def run_selftest() -> int:
    outbox: list[dict] = []
    sim = Esp32Simulator(lambda obj: outbox.append(obj))

    # 1) ping -> pong
    sim.handle({"cmd": "ping"})
    assert outbox and outbox[-1]["type"] == "pong", "ping deveria gerar pong"

    # 2) set_destination -> ack ok + estado NAVIGATING
    sim.handle({"cmd": "set_destination", "command_id": "c1", "mission_id": "m1",
                "lat": -3.1050, "lon": -60.0300})
    assert outbox[-1] == {"type": "ack", "command_id": "c1", "ok": True}
    assert sim.s.state == "NAVIGATING_TO_GOAL"

    # 3) dedup: mesmo command_id -> ack ok, sem reprocessar
    prev_goal = (sim.s.goal_lat, sim.s.goal_lon)
    sim.handle({"cmd": "set_destination", "command_id": "c1", "mission_id": "m1",
                "lat": 0.0, "lon": 0.0})
    assert (sim.s.goal_lat, sim.s.goal_lon) == prev_goal, "command_id repetido não deve reprocessar"

    # 4) navega vários passos -> progresso avança e conclui
    for _ in range(60):
        sim.step(dt=2.0)
        if sim.s.state == "IDLE_HOLDING_POSITION":
            break
    assert sim.s.state == "IDLE_HOLDING_POSITION", "deveria concluir a missão"
    assert any(o.get("event") == "mission_completed" for o in outbox), "faltou mission_completed"

    # 5) emergency_stop -> RETURNING_TO_HOME
    sim.handle({"cmd": "set_destination", "command_id": "c2", "mission_id": "m1",
                "lat": -3.2000, "lon": -60.1000})
    sim.step()
    sim.handle({"cmd": "emergency_stop", "command_id": "c3", "mission_id": "m1"})
    assert sim.s.state == "RETURNING_TO_HOME"

    # 6) comando sem command_id -> ack ok=False
    sim.handle({"cmd": "set_destination", "lat": 1, "lon": 1})
    assert outbox[-1]["ok"] is False

    print("SELF-TEST OK — todos os cenários passaram "
          f"({len(outbox)} mensagens emitidas).")
    return 0


def main() -> None:
    ap = argparse.ArgumentParser(description="Simulador ESP32 para a ponte serial USV-AM")
    ap.add_argument("--port", help="Porta serial (lado ESP32 do par virtual)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--selftest", action="store_true", help="Roda autoteste em loopback (sem porta)")
    args = ap.parse_args()

    if args.selftest:
        raise SystemExit(run_selftest())
    if not args.port:
        ap.error("informe --port ou use --selftest")
    run_serial(args.port, args.baud)


if __name__ == "__main__":
    main()
