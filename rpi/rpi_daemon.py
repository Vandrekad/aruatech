#!/usr/bin/env python3
"""
rpi_daemon.py — Daemon de bordo do Raspberry Pi 4 (F2 + F3).

Costura as duas metades:
  ESP32 --(UART telemetria/eventos)--> serial_bridge --> firebase_client --> RTDB
  RTDB  --(comando do dashboard)------> firebase_client --> serial_bridge --> ESP32

F3 (resiliência):
  - Watchdog do link serial: se a porta cair, tenta reabrir com backoff exponencial.
  - Presença: heartbeat de status online + marca offline no shutdown (equivale ao
    onDisconnect do ESP32, agora feito pelo RPi enquanto ele é o dono do RTDB).
  - Detecção de silêncio do ESP32: se nenhuma telemetria chega em ESP32_SILENCE_S,
    registra um alerta em /logs (o ESP32 pode ter reiniciado ou o cabo soltou).
  - Logging estruturado via `logging` (journald capta stdout no systemd; log rotativo
    é responsabilidade do journald/logrotate, configurado na unit).

Modos:
  Produção (hardware + Firebase real):
      python rpi_daemon.py --port /dev/serial0 \
          --service-account /home/pi/serviceAccount.json \
          --database-url https://usvs-...-rtdb.firebaseio.com/ \
          --drone-id drone_01

  Mock em memória (sem rede, sem hardware — usado no selftest):
      python rpi_daemon.py --selftest
"""

from __future__ import annotations

import argparse
import logging
import sys
import threading
import time

from firebase_client import FirebasePublisher, MockRTDB, RTDBClient

DRONE_ID_DEFAULT = "drone_01"

# ── parâmetros de resiliência (F3) ───────────────────────────────────
RECONNECT_BACKOFF_START_S = 1.0      # 1º retry após 1s
RECONNECT_BACKOFF_MAX_S = 30.0       # teto do backoff
HEARTBEAT_INTERVAL_S = 5.0           # cadência do watchdog/presença
ESP32_SILENCE_S = 15.0               # sem telemetria por esse tempo => alerta

log = logging.getLogger("rpi_daemon")


class RpiDaemon:
    """Orquestra a ponte serial <-> RTDB com watchdog e presença.
    `bridge` e `backend` são injetados para permitir teste com duplos (mock)
    sem hardware nem rede. `clock` é injetável para o selftest controlar o tempo."""

    def __init__(self, bridge, backend, drone_id: str = DRONE_ID_DEFAULT,
                 clock=time.monotonic) -> None:
        self.bridge = bridge
        self.publisher = FirebasePublisher(backend, drone_id)
        self.backend = backend
        self.drone_id = drone_id
        self._clock = clock

        self._last_esp32_msg = 0.0
        self._link_up = False
        self._reconnect_backoff = RECONNECT_BACKOFF_START_S
        self._silence_alerted = False
        self._hb_thread: threading.Thread | None = None
        self._stop_evt = threading.Event()

    # ── ESP32 -> RTDB ────────────────────────────────────────────────
    def on_esp32_message(self, msg: dict) -> None:
        self._last_esp32_msg = self._clock()
        self._silence_alerted = False  # recebeu algo: zera o alerta de silêncio
        mtype = msg.get("type")
        if mtype == "telemetry":
            self.publisher.publish_telemetry(msg)
        elif mtype == "event":
            self.publisher.publish_event(msg)
        # ack/pong: nada a publicar

    # ── RTDB -> ESP32 ────────────────────────────────────────────────
    def on_rtdb_command(self, command: dict) -> None:
        cmd = command.get("cmd_type") or command.get("cmd")
        cid = command.get("command_id", "")
        mid = command.get("mission_id", "")
        if cmd == "set_destination":
            target = command.get("target") or {}
            lat = target.get("lat", command.get("lat"))
            lon = target.get("lon", command.get("lon"))
            self.bridge.set_destination(cid, mid, lat, lon)
            log.info("comando set_destination repassado (cid=%s)", cid)
        elif cmd == "emergency_stop":
            self.bridge.emergency_stop(cid, mid)
            log.info("comando emergency_stop repassado (cid=%s)", cid)
        else:
            log.warning("comando desconhecido ignorado: %s", cmd)

    # ── watchdog do link serial (F3) ─────────────────────────────────
    def _open_link(self) -> bool:
        """Tenta abrir a bridge. Retorna True se conseguiu."""
        try:
            self.bridge.on_message = self.on_esp32_message
            self.bridge.open()
            self._link_up = True
            self._reconnect_backoff = RECONNECT_BACKOFF_START_S
            self._last_esp32_msg = self._clock()
            log.info("link serial aberto")
            return True
        except Exception as exc:
            self._link_up = False
            log.error("falha ao abrir link serial: %s", exc)
            return False

    def _watchdog_tick(self) -> None:
        """Um ciclo do watchdog: reabre link se caiu, publica presença,
        detecta silêncio do ESP32. Chamado pelo heartbeat OU pelo selftest."""
        now = self._clock()

        # 1) link caído -> tenta reabrir (backoff exponencial)
        if not self._link_up:
            if self._open_link():
                pass  # reaberto
            else:
                self._reconnect_backoff = min(
                    self._reconnect_backoff * 2, RECONNECT_BACKOFF_MAX_S
                )
            return

        # 2) presença: heartbeat de status online + last_seen
        try:
            self.publisher.b.set_json(f"/drones/{self.drone_id}/status/presence", {
                "online": True,
                "last_seen": int(time.time()),
            })
        except Exception as exc:
            log.error("falha ao publicar presença: %s", exc)

        # 3) silêncio do ESP32 -> alerta uma vez até voltar a receber
        if (now - self._last_esp32_msg) > ESP32_SILENCE_S and not self._silence_alerted:
            self._silence_alerted = True
            log.warning("ESP32 silencioso ha >%ss - possivel reset/cabo solto", ESP32_SILENCE_S)
            try:
                self.publisher.b.push_json("/logs", {
                    "drone_id": self.drone_id,
                    "type": "esp32_silence",
                    "value": round(now - self._last_esp32_msg, 1),
                    "timestamp": int(time.time()),
                })
            except Exception:
                pass

    def _heartbeat_loop(self) -> None:
        while not self._stop_evt.wait(HEARTBEAT_INTERVAL_S):
            try:
                self._watchdog_tick()
            except Exception as exc:  # nunca deixa a thread morrer
                log.exception("erro no watchdog: %s", exc)

    def start(self) -> None:
        self._open_link()
        self.backend.listen_command(self.drone_id, self.on_rtdb_command)
        self._stop_evt.clear()
        self._hb_thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
        self._hb_thread.start()
        log.info("daemon iniciado (drone=%s)", self.drone_id)

    def stop(self) -> None:
        self._stop_evt.set()
        if self._hb_thread:
            self._hb_thread.join(timeout=2.0)
        try:
            self.publisher.set_offline()  # presença: marca offline no shutdown
        except Exception:
            pass
        self.bridge.close()
        self.backend.close()
        self._link_up = False
        log.info("daemon encerrado")


# ── produção ─────────────────────────────────────────────────────────
def run_production(args) -> None:
    from serial_bridge import SerialBridge  # import tardio (pyserial só na prod)

    backend = RTDBClient(args.service_account, args.database_url)
    bridge = SerialBridge(args.port, args.baud)  # on_message setado pelo daemon
    daemon = RpiDaemon(bridge, backend, args.drone_id)
    daemon.start()
    log.info("rodando. porta=%s drone=%s. Ctrl+C para sair.", args.port, args.drone_id)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log.info("SIGINT recebido, encerrando...")
    finally:
        daemon.stop()


# ── duplos de teste ──────────────────────────────────────────────────
class _FakeBridge:
    """Duplo do SerialBridge: registra comandos, permite empurrar telemetria,
    e pode simular falha de abertura (para exercitar o watchdog)."""

    def __init__(self, fail_open_times: int = 0) -> None:
        self.on_message = None
        self.sent: list[dict] = []
        self.opened = False
        self.open_calls = 0
        self._fail_open_times = fail_open_times

    def open(self) -> None:
        self.open_calls += 1
        if self.open_calls <= self._fail_open_times:
            raise OSError("porta serial indisponivel (simulado)")
        self.opened = True

    def close(self) -> None:
        self.opened = False

    def set_destination(self, cid, mid, lat, lon) -> None:
        self.sent.append({"cmd": "set_destination", "command_id": cid,
                          "mission_id": mid, "lat": lat, "lon": lon})

    def emergency_stop(self, cid, mid) -> None:
        self.sent.append({"cmd": "emergency_stop", "command_id": cid, "mission_id": mid})

    def feed(self, msg: dict) -> None:
        if self.on_message:
            self.on_message(msg)


class _FakeClock:
    """Relógio controlável para o selftest (sem sleeps reais)."""

    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t

    def advance(self, dt: float) -> None:
        self.t += dt


# ── selftest em memória (sem hardware, sem rede, sem pyserial) ────────
def _selftest_f2(bridge, backend, daemon) -> None:
    # 1) telemetria do ESP32 -> RTDB (telemetry + status + path)
    bridge.feed({
        "type": "telemetry", "ts": 1000, "lat": -3.1019, "lon": -60.025, "hdg": 145.2,
        "obs": 120, "bat": 7400, "thrust_l": 80, "thrust_r": 65,
        "state": "NAVIGATING_TO_GOAL", "mission_id": "m_1", "active_leg": 1, "progress": 0.52,
    })
    tel = backend.get("/drones/drone_01/telemetry")
    assert tel and tel["position"]["lat"] == -3.1019, "telemetria nao publicada"
    st = backend.get("/drones/drone_01/status")
    assert st and st["nav_state"] == "NAVIGATING_TO_GOAL" and st["online"] is True, "status errado"
    path = backend.get("/missions/m_1/path/p_1000")
    assert path and path["ts"] == 1000, "ponto de rota nao publicado"

    # 2) evento do ESP32 -> /logs
    bridge.feed({"type": "event", "event": "obstacle_detected", "value": 45, "ts": 1001})
    logs = backend.get("/logs")
    assert logs and any(v.get("type") == "obstacle_detected" for v in logs.values()), "log nao publicado"

    # 3) comando do dashboard (RTDB) -> ESP32 via bridge (contrato: cmd_type + target)
    backend.inject_command("drone_01", {
        "command_id": "cmd_1", "cmd_type": "set_destination", "mission_id": "m_1",
        "target": {"lat": -3.1050, "lon": -60.0300},
    })
    assert bridge.sent and bridge.sent[-1] == {
        "cmd": "set_destination", "command_id": "cmd_1", "mission_id": "m_1",
        "lat": -3.1050, "lon": -60.0300,
    }, f"comando nao repassado: {bridge.sent}"

    # 4) emergency_stop
    backend.inject_command("drone_01", {
        "command_id": "cmd_2", "cmd_type": "emergency_stop", "mission_id": "m_1",
    })
    assert bridge.sent[-1]["cmd"] == "emergency_stop", "emergency_stop nao repassado"

    # 5) ack/pong nao geram log
    n_logs_before = len(backend.get("/logs"))
    bridge.feed({"type": "ack", "command_id": "cmd_1", "ok": True})
    bridge.feed({"type": "pong"})
    assert len(backend.get("/logs")) == n_logs_before, "ack/pong nao deveriam virar log"


def _selftest_f3() -> None:
    clock = _FakeClock()
    backend = MockRTDB()
    # bridge que falha 2x ao abrir, depois abre (exercita o watchdog + backoff)
    bridge = _FakeBridge(fail_open_times=2)
    daemon = RpiDaemon(bridge, backend, "drone_01", clock=clock)

    # start tenta abrir e falha (1ª tentativa)
    daemon._open_link()
    assert not daemon._link_up, "link nao deveria subir na 1a tentativa (falha simulada)"
    assert daemon._reconnect_backoff == RECONNECT_BACKOFF_START_S

    # watchdog: 2ª tentativa falha, backoff dobra
    daemon._watchdog_tick()
    assert not daemon._link_up, "2a tentativa ainda falha"
    assert daemon._reconnect_backoff == RECONNECT_BACKOFF_START_S * 2, "backoff deveria dobrar"

    # watchdog: 3ª tentativa (open_calls=3 > fail_open_times=2) -> sobe
    daemon._watchdog_tick()
    assert daemon._link_up, "link deveria subir na 3a tentativa"
    assert daemon._reconnect_backoff == RECONNECT_BACKOFF_START_S, "backoff deveria resetar ao reconectar"

    # presença: watchdog publica heartbeat de presença
    daemon._watchdog_tick()
    pres = backend.get("/drones/drone_01/status/presence")
    assert pres and pres["online"] is True, "presença nao publicada pelo watchdog"

    # silêncio do ESP32: avança o relógio além do limite -> alerta em /logs (uma vez)
    clock.advance(ESP32_SILENCE_S + 1)
    daemon._watchdog_tick()
    logs = backend.get("/logs") or {}
    assert any(v.get("type") == "esp32_silence" for v in logs.values()), "alerta de silêncio nao registrado"
    n_silence = sum(1 for v in logs.values() if v.get("type") == "esp32_silence")
    # segundo tick sem novos dados NAO deve duplicar o alerta
    daemon._watchdog_tick()
    logs2 = backend.get("/logs") or {}
    n_silence2 = sum(1 for v in logs2.values() if v.get("type") == "esp32_silence")
    assert n_silence2 == n_silence, "alerta de silêncio nao deveria duplicar"

    # recebeu telemetria de novo -> zera o alerta (proximo silencio volta a alertar)
    bridge.feed({"type": "telemetry", "ts": 2000, "lat": -3.1, "lon": -60.0, "mission_id": ""})
    assert daemon._silence_alerted is False, "receber telemetria deveria zerar o alerta de silêncio"

    # stop marca offline
    daemon.stop()
    assert backend.get("/drones/drone_01/status/online") is False, "deveria marcar offline no stop"


def run_selftest() -> int:
    # F2: fluxo normal (bridge abre de primeira)
    bridge = _FakeBridge()
    backend = MockRTDB()
    daemon = RpiDaemon(bridge, backend, "drone_01", clock=_FakeClock())
    daemon._open_link()
    daemon.backend.listen_command(daemon.drone_id, daemon.on_rtdb_command)
    assert bridge.opened, "bridge deveria abrir de primeira no fluxo normal"
    _selftest_f2(bridge, backend, daemon)
    daemon.stop()
    assert backend.get("/drones/drone_01/status/online") is False, "offline no stop (F2)"

    # F3: watchdog / backoff / presença / silêncio / offline
    _selftest_f3()

    print("SELF-TEST F2+F3 OK - publicacao RTDB, repasse de comandos, ack/pong ignorados, "
          "watchdog reconecta com backoff, presenca publicada, alerta de silencio (sem duplicar), "
          "offline no stop.")
    return 0


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    ap = argparse.ArgumentParser(description="Daemon de bordo RPi4 (F2+F3) - ponte UART <-> RTDB")
    ap.add_argument("--selftest", action="store_true", help="Roda selftest em memoria (sem hardware/rede)")
    ap.add_argument("--port", help="Porta serial do ESP32 (ex: /dev/serial0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--service-account", help="Caminho do serviceAccount.json do Firebase")
    ap.add_argument("--database-url", help="URL do RTDB")
    ap.add_argument("--drone-id", default=DRONE_ID_DEFAULT)
    args = ap.parse_args()

    if args.selftest:
        sys.exit(run_selftest())
    if not (args.port and args.service_account and args.database_url):
        ap.error("produção exige --port, --service-account e --database-url (ou use --selftest)")
    run_production(args)


if __name__ == "__main__":
    main()
