#!/usr/bin/env python3
"""
firebase_client.py — Camada Firebase RTDB do lado do Raspberry Pi 4 (F2).

Nesta fase o RPi assume a camada de nuvem que antes era do ESP32:
  - PUBLICA telemetria/status/path/logs no RTDB (o ESP32 para de escrever direto
    quando o RPi está presente — flag RPI_PRESENT no firmware, feita na F1).
  - ESCUTA /drones/{drone_id}/command e entrega comandos novos ao ESP32 via UART.

Dois backends, selecionados automaticamente:
  - RTDBClient  : firebase-admin real (produção). Requer serviceAccount.json + URL.
  - MockRTDB    : dicionário em memória + fila de comandos (testes sem rede/credencial).

O daemon (rpi_daemon.py) injeta o backend; este módulo não decide sozinho.
"""

from __future__ import annotations

import json
import threading
import time
from typing import Callable, Optional

# firebase-admin é import OPCIONAL — o mock roda sem ele (selftest sem instalar SDK).
try:
    import firebase_admin
    from firebase_admin import credentials, db as fb_db
    _HAS_FIREBASE = True
except ImportError:  # pragma: no cover
    _HAS_FIREBASE = False


# ── contrato comum ───────────────────────────────────────────────────
class RTDBBackend:
    """Interface mínima que o daemon usa. Real e mock implementam isto."""

    def set_json(self, path: str, value: dict) -> None: ...
    def set_value(self, path: str, value) -> None: ...
    def push_json(self, path: str, value: dict) -> None: ...
    def listen_command(self, drone_id: str, on_command: Callable[[dict], None]) -> None: ...
    def close(self) -> None: ...


# ── backend real (firebase-admin) ────────────────────────────────────
class RTDBClient(RTDBBackend):
    def __init__(self, service_account_path: str, database_url: str) -> None:
        if not _HAS_FIREBASE:
            raise RuntimeError(
                "firebase-admin não instalado. Rode: pip install -r requirements.txt "
                "(ou use MockRTDB para teste sem rede)."
            )
        if not firebase_admin._apps:
            cred = credentials.Certificate(service_account_path)
            firebase_admin.initialize_app(cred, {"databaseURL": database_url})

    def set_json(self, path: str, value: dict) -> None:
        fb_db.reference(path).set(value)

    def set_value(self, path: str, value) -> None:
        fb_db.reference(path).set(value)

    def push_json(self, path: str, value: dict) -> None:
        fb_db.reference(path).push(value)

    def listen_command(self, drone_id: str, on_command: Callable[[dict], None]) -> None:
        ref = fb_db.reference(f"/drones/{drone_id}/command")

        def _handler(event) -> None:  # firebase StreamEvent
            data = event.data
            if isinstance(data, dict) and data.get("command_id"):
                on_command(data)

        ref.listen(_handler)  # thread própria do SDK

    def close(self) -> None:
        pass


# ── backend mock (em memória) ────────────────────────────────────────
class MockRTDB(RTDBBackend):
    """RTDB simulado: dict em memória + injeção manual de comandos.

    Usado no selftest e no daemon --mock (sem rede/credencial).
    """

    def __init__(self) -> None:
        self.tree: dict = {}
        self._lock = threading.Lock()
        self._cmd_cb: Optional[Callable[[dict], None]] = None

    def _set_at(self, path: str, value) -> None:
        parts = [p for p in path.strip("/").split("/") if p]
        with self._lock:
            node = self.tree
            for p in parts[:-1]:
                node = node.setdefault(p, {})
            node[parts[-1]] = value

    def set_json(self, path: str, value: dict) -> None:
        self._set_at(path, json.loads(json.dumps(value)))  # cópia defensiva

    def set_value(self, path: str, value) -> None:
        self._set_at(path, value)

    def push_json(self, path: str, value: dict) -> None:
        parts = [p for p in path.strip("/").split("/") if p]
        key = f"k_{int(time.time() * 1000)}_{id(value) & 0xffff}"
        with self._lock:
            node = self.tree
            for p in parts:
                node = node.setdefault(p, {})
            node[key] = json.loads(json.dumps(value))

    def listen_command(self, drone_id: str, on_command: Callable[[dict], None]) -> None:
        self._cmd_cb = on_command

    # helper de teste: simula o dashboard escrevendo um comando no RTDB
    def inject_command(self, drone_id: str, command: dict) -> None:
        self.set_json(f"/drones/{drone_id}/command", command)
        if self._cmd_cb:
            self._cmd_cb(command)

    def get(self, path: str):
        parts = [p for p in path.strip("/").split("/") if p]
        with self._lock:
            node = self.tree
            for p in parts:
                if not isinstance(node, dict) or p not in node:
                    return None
                node = node[p]
            return node

    def close(self) -> None:
        self._cmd_cb = None


# ── publicador de alto nível ─────────────────────────────────────────
class FirebasePublisher:
    """Traduz telemetria/eventos do ESP32 (via bridge) em escritas no RTDB.

    Espelha os paths do contrato de dados do README do projeto:
      /drones/{id}/telemetry, /drones/{id}/status, /missions/{mid}/path, /logs
    """

    def __init__(self, backend: RTDBBackend, drone_id: str) -> None:
        self.b = backend
        self.drone_id = drone_id

    def publish_telemetry(self, msg: dict) -> None:
        """msg = telemetria vinda do ESP32 (type=='telemetry')."""
        tid = self.drone_id
        self.b.set_json(f"/drones/{tid}/telemetry", {
            "timestamp": msg.get("ts"),
            "mission_id": msg.get("mission_id", ""),
            "position": {"lat": msg.get("lat"), "lon": msg.get("lon"), "heading": msg.get("hdg")},
            "sensors": {"battery_mv": msg.get("bat"), "obs_dist": msg.get("obs")},
            "actuators": {"thrust_l": msg.get("thrust_l"), "thrust_r": msg.get("thrust_r")},
        })
        # status operacional (fonte de verdade é o ESP32; RPi só espelha)
        self.b.set_json(f"/drones/{tid}/status", {
            "online": True,
            "last_seen": msg.get("ts"),
            "active_mission_id": msg.get("mission_id", ""),
            "nav_state": msg.get("state"),
            "active_leg": msg.get("active_leg", 0),
            "route_progress": msg.get("progress", 0.0),
            "last_position": {"lat": msg.get("lat"), "lon": msg.get("lon")},
        })
        # ponto de rota (histórico) quando há missão ativa
        mid = msg.get("mission_id")
        if mid:
            self.b.set_json(
                f"/missions/{mid}/path/p_{msg.get('ts')}",
                {"lat": msg.get("lat"), "lon": msg.get("lon"), "ts": msg.get("ts")},
            )

    def publish_event(self, msg: dict) -> None:
        """msg = evento operacional do ESP32 (type=='event')."""
        self.b.push_json("/logs", {
            "drone_id": self.drone_id,
            "type": msg.get("event"),
            "value": msg.get("value"),
            "timestamp": msg.get("ts"),
        })

    def set_offline(self) -> None:
        self.b.set_value(f"/drones/{self.drone_id}/status/online", False)
