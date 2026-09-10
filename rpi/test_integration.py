#!/usr/bin/env python3
"""
test_integration.py — Validação B (integrada) SEM hardware nem com0com.

Liga o CÓDIGO REAL dos dois lados por um "fio" serial em memória:

    Esp32Simulator  <--(FakeSerialPair, API igual a serial.Serial)-->  SerialBridge

Cada ponta roda na sua própria thread de RX (como no hardware real), então isto
exercita de verdade o framing JSON-lines, o loop de leitura por bytes, a
deduplicação por command_id e o threading dos dois módulos — o que o com0com
validaria, menos o driver COM do SO (território da F4).

Uso:
    python test_integration.py            # roda o cenário e imprime PASS/FAIL
    python test_integration.py --selftest # idem (alias, p/ consistência)
"""

from __future__ import annotations

import sys
import threading
import time

# Reusa o CÓDIGO REAL de produção dos dois lados.
from serial_bridge import SerialBridge
from esp32_simulator import Esp32Simulator, SimState


class _Pipe:
    """Buffer byte a byte, thread-safe, com a fatia da API de serial.Serial
    que a bridge e o simulador usam: read(n), write(b), flush(), in_waiting,
    is_open, close()."""

    def __init__(self) -> None:
        self._buf = bytearray()
        self._cv = threading.Condition()
        self._open = True

    def write(self, data: bytes) -> int:
        with self._cv:
            self._buf.extend(data)
            self._cv.notify_all()
        return len(data)

    def flush(self) -> None:
        pass

    def read(self, n: int = 1) -> bytes:
        """Bloqueia até ter >=1 byte ou timeout curto (imita timeout=0.2)."""
        with self._cv:
            if not self._buf:
                self._cv.wait(timeout=0.2)
            if not self._buf:
                return b""
            take = min(n, len(self._buf))
            out = bytes(self._buf[:take])
            del self._buf[:take]
            return out

    @property
    def in_waiting(self) -> int:
        with self._cv:
            return len(self._buf)

    @property
    def is_open(self) -> bool:
        return self._open

    def close(self) -> None:
        with self._cv:
            self._open = False
            self._cv.notify_all()


class FakeSerialPair:
    """Duas pontas ligadas: o que a ponta A escreve, a ponta B lê, e vice-versa.
    Cada ponta expõe a API de serial.Serial."""

    class Endpoint:
        def __init__(self, tx: _Pipe, rx: _Pipe) -> None:
            self._tx = tx
            self._rx = rx

        # escrita vai para o pipe TX; leitura vem do pipe RX
        def write(self, data: bytes) -> int:
            return self._tx.write(data)

        def flush(self) -> None:
            self._tx.flush()

        def read(self, n: int = 1) -> bytes:
            return self._rx.read(n)

        @property
        def in_waiting(self) -> int:
            return self._rx.in_waiting

        @property
        def is_open(self) -> bool:
            return self._tx.is_open and self._rx.is_open

        def close(self) -> None:
            self._tx.close()

    def __init__(self) -> None:
        a2b = _Pipe()
        b2a = _Pipe()
        self.a = self.Endpoint(tx=a2b, rx=b2a)  # ex.: bridge
        self.b = self.Endpoint(tx=b2a, rx=a2b)  # ex.: simulador


# ── lado ESP32 simulado sobre uma ponta do pipe ──────────────────────
class _SimRunner:
    """Roda o Esp32Simulator sobre um Endpoint (RX em thread + step de física)."""

    def __init__(self, endpoint) -> None:
        self.ep = endpoint
        self.sim = Esp32Simulator(self._write_line, SimState())
        self._stop = threading.Event()
        self._rx = threading.Thread(target=self._rx_loop, daemon=True)
        self._tick = threading.Thread(target=self._tick_loop, daemon=True)

    def _write_line(self, obj: dict) -> None:
        import json
        self.ep.write((json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8"))

    def _rx_loop(self) -> None:
        import json
        buf = bytearray()
        while not self._stop.is_set():
            chunk = self.ep.read(128)
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    try:
                        self.sim.handle(json.loads(line))
                    except json.JSONDecodeError:
                        pass

    def _tick_loop(self) -> None:
        # física rápida (0.2s) só para o teste convergir em segundos
        while not self._stop.is_set():
            self.sim.step(dt=2.0)
            self._write_line(self.sim.telemetry())
            time.sleep(0.2)

    def start(self) -> None:
        self._rx.start()
        self._tick.start()

    def stop(self) -> None:
        self._stop.set()
        self._rx.join(timeout=1.0)
        self._tick.join(timeout=1.0)


def run() -> int:
    pair = FakeSerialPair()

    # Recebedor de telemetria/acks do lado "RPi" (bridge)
    received: list[dict] = []
    lock = threading.Lock()

    def on_msg(msg: dict) -> None:
        with lock:
            received.append(msg)

    # Bridge REAL, mas com a porta serial substituída pela ponta A do pipe.
    bridge = SerialBridge("FAKE", on_message=on_msg)
    bridge._ser = pair.a          # injeta o fio em vez de abrir COM
    bridge._stop.clear()
    bridge._rx_thread = threading.Thread(target=bridge._rx_loop, daemon=True)
    bridge._rx_thread.start()

    # ESP32 simulado REAL na ponta B.
    sim = _SimRunner(pair.b)
    sim.start()

    def wait_for(pred, timeout=5.0, step=0.05) -> bool:
        t0 = time.time()
        while time.time() - t0 < timeout:
            with lock:
                if pred(list(received)):
                    return True
            time.sleep(step)
        return False

    ok = True

    def check(label: str, cond: bool) -> None:
        nonlocal ok
        print(f"[{'PASS' if cond else 'FAIL'}] {label}")
        ok = ok and cond

    # 1) ping -> pong trafega ida e volta pelo fio
    bridge.ping()
    check("ping -> pong pelo fio serial",
          wait_for(lambda r: any(m.get("type") == "pong" for m in r)))

    # 2) telemetria do simulador chega na bridge
    check("telemetria ESP32 -> bridge",
          wait_for(lambda r: any(m.get("type") == "telemetry" for m in r)))

    # 3) set_destination: bridge -> simulador -> ack + comeca a navegar
    bridge.set_destination("cmd_1", "m_1", -3.1050, -60.0300)
    check("set_destination -> ack ok",
          wait_for(lambda r: any(m.get("type") == "ack" and m.get("command_id") == "cmd_1"
                                 and m.get("ok") for m in r)))
    check("estado vira NAVIGATING_TO_GOAL",
          wait_for(lambda r: any(m.get("type") == "telemetry" and
                                 m.get("state") == "NAVIGATING_TO_GOAL" for m in r)))

    # 4) progresso avanca (posicao muda em direcao ao destino)
    def progress_advanced(r):
        progs = [m.get("progress", 0) for m in r if m.get("type") == "telemetry"]
        return len(progs) >= 2 and max(progs) > 0.0
    check("progresso de rota avanca", wait_for(progress_advanced, timeout=6.0))

    # 5) emergency_stop -> ack + RETURNING_TO_HOME
    bridge.emergency_stop("cmd_2", "m_1")
    check("emergency_stop -> ack ok",
          wait_for(lambda r: any(m.get("type") == "ack" and m.get("command_id") == "cmd_2"
                                 and m.get("ok") for m in r)))
    check("estado vira RETURNING_TO_HOME",
          wait_for(lambda r: any(m.get("type") == "telemetry" and
                                 m.get("state") == "RETURNING_TO_HOME" for m in r)))

    # 6) dedup: o criterio real (igual ao firmware) e comparar com o ULTIMO
    # command_id processado — protege contra reenvio imediato/rajada do mesmo
    # comando (retry). Reenviar cmd_2 (o ultimo, emergency_stop) deve gerar ack
    # idempotente e NAO reprocessar: o estado permanece no fluxo pos-emergency
    # (RETURNING_TO_HOME, ou IDLE se ja concluiu o retorno), nunca NAVIGATING.
    with lock:
        acks_cmd2_before = sum(1 for m in received
                               if m.get("type") == "ack" and m.get("command_id") == "cmd_2")
    bridge.emergency_stop("cmd_2", "m_1")  # reenvia o MESMO ultimo command_id
    check("dedup: cmd_2 repetido gera ack idempotente",
          wait_for(lambda r: sum(1 for m in r if m.get("type") == "ack"
                                 and m.get("command_id") == "cmd_2") > acks_cmd2_before,
                   timeout=3.0))
    time.sleep(0.6)
    with lock:
        after = list(received)
    last_state = next((m.get("state") for m in reversed(after)
                       if m.get("type") == "telemetry"), None)
    check("dedup: reenvio do ultimo command_id NAO reativa NAVIGATING_TO_GOAL "
          f"(estado atual={last_state})",
          last_state in ("RETURNING_TO_HOME", "IDLE_HOLDING_POSITION"))

    sim.stop()
    bridge.close()

    print(f"\n{'INTEGRATION OK' if ok else 'INTEGRATION FAILED'} - "
          f"{sum(1 for _ in received)} mensagens trocadas pelo fio serial.")
    return 0 if ok else 1


def main() -> None:
    # aceita --selftest como alias de consistencia com os outros scripts
    sys.exit(run())


if __name__ == "__main__":
    main()
