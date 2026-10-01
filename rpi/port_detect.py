#!/usr/bin/env python3
"""
port_detect.py — Autodetecção da porta serial do ESP32 no Raspberry Pi.

PROBLEMA: o conversor USB-serial do ESP32 enumera ora como /dev/ttyUSB0, ora
/dev/ttyUSB1 (a ordem depende de qual dispositivo o kernel registrou primeiro no
boot / replug). Fixar --port /dev/ttyUSBx na unit systemd é frágil.

SOLUÇÃO: identificar o ESP32 pelo PROTOCOLO, não pelo número da porta. Abrimos
cada candidato /dev/ttyUSB* (e ttyACM* por segurança), enviamos {"cmd":"ping"} e
aceitamos a porta que responder com um JSON NOSSO (um "pong", ou qualquer linha
com um "type" conhecido — a telemetria periódica do ESP32 também vale) dentro de
um timeout curto. Isso é imune à troca ttyUSB0<->ttyUSB1.

Opcionalmente, pode-se restringir os candidatos por VID:PID do conversor
(CP2102 = 10c4:ea60, CH340 = 1a86:7523) para não sondar outros periféricos USB.
"""

from __future__ import annotations

import glob
import json
import time
from typing import Optional

try:
    import serial  # pyserial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    raise SystemExit("pyserial não instalado. Rode: pip install -r requirements.txt")


# VID:PID conhecidos de conversores USB-serial usados em placas ESP32.
# (opcional — usado só para priorizar/filtrar candidatos.)
KNOWN_USB_SERIAL_IDS = {
    (0x10C4, 0xEA60),  # Silicon Labs CP2102 / CP2104
    (0x1A86, 0x7523),  # QinHeng CH340
    (0x1A86, 0x55D4),  # QinHeng CH9102
    (0x0403, 0x6001),  # FTDI FT232
}

# Tipos de mensagem válidos do protocolo do ESP32 (serial_link.cpp).
_VALID_TYPES = {"pong", "telemetry", "event", "ack"}


def list_candidate_ports() -> list[str]:
    """Lista as portas serial candidatas, com os conversores conhecidos primeiro.

    Ordem: portas cujo VID:PID bate com um conversor ESP32 conhecido vêm antes;
    depois o resto dos /dev/ttyUSB* e /dev/ttyACM*. Assim a sondagem acerta de
    primeira no caso comum, sem depender do número da porta.
    """
    known: list[str] = []
    others: list[str] = []
    seen: set[str] = set()

    for p in list_ports.comports():
        dev = p.device
        seen.add(dev)
        vid_pid = (p.vid, p.pid) if p.vid is not None else None
        if vid_pid in KNOWN_USB_SERIAL_IDS:
            known.append(dev)
        else:
            others.append(dev)

    # Fallback por glob (caso list_ports não traga tudo em algum kernel).
    for dev in sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")):
        if dev not in seen:
            others.append(dev)

    return known + others


def probe_port(dev: str, baud: int = 115200, timeout_s: float = 2.0) -> bool:
    """Sonda UMA porta: abre, manda ping, espera uma linha JSON nossa.

    Retorna True se a porta respondeu com um JSON cujo "type" é conhecido do
    nosso protocolo (pong/telemetry/event/ack). Qualquer exceção de abertura
    (porta ocupada, permissão, sumiu) retorna False — o chamador tenta a próxima.
    """
    ser = None
    try:
        ser = serial.Serial(dev, baud, timeout=0.2)
    except Exception:
        return False

    try:
        # Dê um instante caso a abertura tenha disparado o auto-reset do ESP32
        # (DTR/RTS pulsam no CP2102/CH340 e reiniciam a placa).
        time.sleep(0.3)
        try:
            ser.reset_input_buffer()
        except Exception:
            pass

        # Envia um ping; o ESP32 responde {"type":"pong"}.
        try:
            ser.write(b'{"cmd":"ping"}\n')
            ser.flush()
        except Exception:
            return False

        deadline = time.monotonic() + timeout_s
        buf = bytearray()
        while time.monotonic() < deadline:
            try:
                chunk = ser.read(256)
            except Exception:
                return False
            if chunk:
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
                        continue  # ruído/log — ignora, continua lendo
                    if isinstance(msg, dict) and msg.get("type") in _VALID_TYPES:
                        return True  # é o ESP32: fala o nosso protocolo
            else:
                time.sleep(0.05)
        return False
    finally:
        try:
            ser.close()
        except Exception:
            pass


def detect_esp32_port(baud: int = 115200, probe_timeout_s: float = 2.0) -> Optional[str]:
    """Varre os candidatos UMA vez e retorna o device do ESP32, ou None.

    Não faz retentativa — o chamador (daemon) decide a política de retry/backoff.
    """
    for dev in list_candidate_ports():
        if probe_port(dev, baud, probe_timeout_s):
            return dev
    return None


def detect_esp32_port_blocking(
    baud: int = 115200,
    probe_timeout_s: float = 2.0,
    backoff_start_s: float = 1.0,
    backoff_max_s: float = 15.0,
    on_attempt=None,
) -> str:
    """Bloqueia até encontrar o ESP32, com backoff exponencial entre varreduras.

    on_attempt(ciclo:int, candidatos:list[str]) — callback opcional de log.
    """
    backoff = backoff_start_s
    attempt = 0
    while True:
        attempt += 1
        candidates = list_candidate_ports()
        if on_attempt:
            on_attempt(attempt, candidates)
        for dev in candidates:
            if probe_port(dev, baud, probe_timeout_s):
                return dev
        time.sleep(backoff)
        backoff = min(backoff * 2, backoff_max_s)
