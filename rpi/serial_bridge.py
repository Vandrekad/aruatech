#!/usr/bin/env python3
"""
serial_bridge.py — Ponte serial RPi4 <-> ESP32 (F1 da migração USV-AM).

Protocolo: JSON-lines (uma mensagem JSON por linha, '\n') a 115200 baud.
Este módulo é o transporte puro. Nas fases seguintes, o firebase_client (F2)
consumirá a telemetria daqui e publicará no RTDB, e repassará comandos do
dashboard para cá.

Uso standalone (teste manual do link):
    python serial_bridge.py --port COM5              # Windows
    python serial_bridge.py --port /dev/serial0      # Raspberry Pi (UART GPIO14/15)

Contra o simulador (porta serial virtual — ver README):
    python serial_bridge.py --port <porta_do_par_virtual>
"""

from __future__ import annotations

import argparse
import json
import threading
import time
from typing import Callable, Optional

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    raise SystemExit("pyserial não instalado. Rode: pip install -r requirements.txt")


class SerialBridge:
    """Transporte JSON-lines sobre UART com o ESP32.

    - on_message(dict): callback chamado para cada mensagem recebida do ESP32.
    - send_command(dict): serializa e envia um comando ao ESP32.
    """

    def __init__(
        self,
        port: str,
        baud: int = 115200,
        on_message: Optional[Callable[[dict], None]] = None,
    ) -> None:
        self.port = port
        self.baud = baud
        self.on_message = on_message or self._default_on_message
        self._ser: Optional[serial.Serial] = None
        self._rx_thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    # ── ciclo de vida ────────────────────────────────────────────────
    def open(self) -> None:
        self._ser = serial.Serial(self.port, self.baud, timeout=0.2)
        self._stop.clear()
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._rx_thread.start()
        print(f"[BRIDGE] Aberto {self.port} @ {self.baud}")

    def close(self) -> None:
        self._stop.set()
        if self._rx_thread:
            self._rx_thread.join(timeout=1.0)
        if self._ser and self._ser.is_open:
            self._ser.close()
        print("[BRIDGE] Fechado.")

    # ── envio ────────────────────────────────────────────────────────
    def send_command(self, command: dict) -> None:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError("Porta serial não está aberta.")
        line = json.dumps(command, separators=(",", ":")) + "\n"
        self._ser.write(line.encode("utf-8"))
        self._ser.flush()

    def ping(self) -> None:
        self.send_command({"cmd": "ping"})

    def request_telemetry(self) -> None:
        self.send_command({"cmd": "request_telemetry"})

    def set_destination(self, command_id: str, mission_id: str, lat: float, lon: float) -> None:
        self.send_command({
            "cmd": "set_destination",
            "command_id": command_id,
            "mission_id": mission_id,
            "lat": lat,
            "lon": lon,
            "issued_at": int(time.time()),
        })

    def emergency_stop(self, command_id: str, mission_id: str) -> None:
        self.send_command({
            "cmd": "emergency_stop",
            "command_id": command_id,
            "mission_id": mission_id,
            "issued_at": int(time.time()),
        })

    # ── recepção ─────────────────────────────────────────────────────
    def _rx_loop(self) -> None:
        assert self._ser is not None
        buf = bytearray()
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(256)
            except Exception as exc:  # pragma: no cover
                print(f"[BRIDGE] Erro de leitura: {exc}")
                time.sleep(0.5)
                continue
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    print(f"[BRIDGE] Linha não-JSON descartada: {line!r}")
                    continue
                self.on_message(msg)

    @staticmethod
    def _default_on_message(msg: dict) -> None:
        mtype = msg.get("type", "?")
        if mtype == "telemetry":
            print(
                f"[TELEM] state={msg.get('state')} "
                f"lat={msg.get('lat')} lon={msg.get('lon')} "
                f"hdg={msg.get('hdg')} obs={msg.get('obs')}cm "
                f"bat={msg.get('bat')}mV thrust=({msg.get('thrust_l')},{msg.get('thrust_r')}) "
                f"leg={msg.get('active_leg')} prog={msg.get('progress')}"
            )
        elif mtype == "ack":
            print(f"[ACK ] command_id={msg.get('command_id')} ok={msg.get('ok')}")
        elif mtype == "event":
            print(f"[EVT ] {msg.get('event')} value={msg.get('value')}")
        elif mtype == "pong":
            print("[PONG]")
        else:
            print(f"[RECV] {msg}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Ponte serial RPi4 <-> ESP32 (USV-AM F1)")
    ap.add_argument("--port", required=True, help="Porta serial (ex: COM5, /dev/serial0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--demo", action="store_true",
                    help="Envia ping + set_destination de demonstração após 2s")
    args = ap.parse_args()

    bridge = SerialBridge(args.port, args.baud)
    bridge.open()

    try:
        if args.demo:
            time.sleep(2)
            print("[DEMO] ping")
            bridge.ping()
            time.sleep(1)
            print("[DEMO] set_destination")
            bridge.set_destination("cmd_demo_1", "m_demo_1", -3.1050, -60.0300)
        # Mantém vivo lendo telemetria até Ctrl+C
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[BRIDGE] Encerrando...")
    finally:
        bridge.close()


if __name__ == "__main__":
    main()
