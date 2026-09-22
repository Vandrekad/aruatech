# RPi4 — Lado Raspberry Pi da migração USV-AM (F1 + F2 + F3)

Transporte serial JSON-lines entre o **Raspberry Pi 4** e o **ESP32** (F1), a
camada Firebase RTDB que o RPi assume do ESP32 (F2), e a resiliência de bordo
— watchdog, presença, log rotativo e systemd (F3). Tudo testável **sem hardware**
(estratégia mock-first do cronograma: o código é migrado antes dos componentes chegarem).

## Arquivos

| Arquivo | Função |
|---|---|
| `serial_bridge.py` | F1 — Ponte serial: lê telemetria do ESP32, envia comandos (`SerialBridge`) |
| `esp32_simulator.py` | F1 — Emula o firmware ESP32 (telemetria + resposta a comandos) |
| `firebase_client.py` | F2 — Publica no RTDB + escuta comandos. `RTDBClient` (real) / `MockRTDB` (teste) / `FirebasePublisher` |
| `rpi_daemon.py` | F2+F3 — Daemon: bridge ↔ RTDB + watchdog/presença/log (`--selftest` em memória) |
| `deploy/usv-rpi-daemon.service` | F3 — Unit systemd (auto-start no boot + restart on crash) |
| `deploy/usv-rpi-daemon.journald.conf` | F3 — Limite/rotação do journal (protege o cartão SD) |
| `requirements.txt` | `pyserial` (F1) + `firebase-admin` (F2) |

## Protocolo (JSON-lines, 115200 baud)

Pinout: **RPi GPIO14/15 (TXD/RXD) ↔ ESP32 GPIO16/17 (Serial2)** — 3.3V direto.
O GPS do ESP32 foi movido para GPIO 25/26 (Serial1) para liberar a Serial2.

### RPi → ESP32 (comandos)
```json
{"cmd":"set_destination","command_id":"cmd_1","mission_id":"m_1","lat":-3.105,"lon":-60.03,"issued_at":1712...}
{"cmd":"emergency_stop","command_id":"cmd_2","mission_id":"m_1"}
{"cmd":"request_telemetry"}
{"cmd":"ping"}
```

### ESP32 → RPi
```json
{"type":"telemetry","ts":..,"lat":..,"lon":..,"hdg":..,"obs":..,"bat":..,"thrust_l":..,"thrust_r":..,"state":"..","mission_id":"..","active_leg":..,"progress":..}
{"type":"ack","command_id":"cmd_1","ok":true}
{"type":"event","event":"obstacle_detected","value":45}
{"type":"pong"}
```

O ESP32 deduplica por `command_id` (mesmo id → ack idempotente, sem reprocessar).
Se o RPi ficar em silêncio por `RPI_LINK_TIMEOUT_MS` (30s), o ESP32 assume modo
autônomo e volta a publicar direto no Firebase — **a invariante de autonomia**.

## Setup

```bash
cd rpi
python -m venv .venv
# Windows:  .venv\Scripts\activate     Linux/RPi:  source .venv/bin/activate
pip install -r requirements.txt
```

## Testar SEM hardware

### Opção A — autoteste do simulador (não precisa de porta serial)
Valida todo o protocolo em loopback (ping/pong, set_destination, dedup,
navegação até concluir, emergency_stop, comando inválido):
```bash
python esp32_simulator.py --selftest
```

### Opção B — bridge real contra o simulador via par de portas virtuais
Crie um par de portas seriais virtuais conectadas entre si:

- **Linux:** `socat -d -d pty,raw,echo=0 pty,raw,echo=0`
  (imprime dois `/dev/pts/N` — um para o simulador, outro para a bridge)
- **Windows:** instale [com0com](https://sourceforge.net/projects/com0com/) e crie
  um par (ex.: `CNCA0` ↔ `CNCB0`).

Terminal 1 (ESP32 simulado):
```bash
python esp32_simulator.py --port /dev/pts/3     # ou CNCB0 no Windows
```
Terminal 2 (bridge do RPi, com demo de comandos):
```bash
python serial_bridge.py --port /dev/pts/4 --demo # ou CNCA0 no Windows
```
A bridge deve imprimir `[PONG]`, `[ACK ] ok=True` e um fluxo de `[TELEM]` com a
posição avançando em direção ao destino.

## Testar COM hardware (a partir de 19/09, quando os componentes chegarem)

No RPi, habilite a UART (`raspi-config` → Interface → Serial: login shell **não**,
hardware serial **sim**) e ligue:
```bash
python serial_bridge.py --port /dev/serial0 --demo
```

## Uso como biblioteca (F2 — firebase_client vai consumir isto)

```python
from serial_bridge import SerialBridge

def on_msg(msg: dict):
    if msg["type"] == "telemetry":
        ...  # publicar no RTDB (F2)

bridge = SerialBridge("/dev/serial0", on_message=on_msg)
bridge.open()
bridge.set_destination("cmd_1", "m_1", -3.105, -60.03)
```

---

# F2 — RPi assume o Firebase RTDB

Nesta fase o RPi passa a ser dono da camada de nuvem: publica telemetria/status/
path/logs no RTDB e escuta `/drones/{id}/command` para repassar comandos ao ESP32.
O firmware já detecta o RPi (flag `RPI_PRESENT`, F1) e **para de escrever direto no
Firebase enquanto o RPi está presente** — sem escrita duplicada. Se o RPi cair, o
ESP32 retoma a publicação direta após 30s (invariante de autonomia).

## Mapeamento de dados (contrato do projeto)

| Origem (ESP32 via UART) | Destino no RTDB |
|---|---|
| `telemetry` | `/drones/{id}/telemetry` + `/drones/{id}/status` + `/missions/{mid}/path/p_{ts}` |
| `event` | `/logs` (push) |
| — | `/drones/{id}/status/online=false` no shutdown do daemon |

Comandos do dashboard no RTDB (`cmd_type` + `target`) são traduzidos para o formato
UART (`set_destination`/`emergency_stop`) e enviados ao ESP32 pela bridge.

## Testar SEM hardware nem rede

Selftest em memória (usa `MockRTDB` + bridge falsa — não instala firebase-admin,
não abre porta serial, não toca o RTDB real):
```bash
python rpi_daemon.py --selftest
```
Valida: telemetria→telemetry/status/path, evento→/logs, comando RTDB→ESP32
(set_destination e emergency_stop), ack/pong ignorados, offline no stop.

## Rodar em produção (a partir de 19/09, com hardware + Firebase)

Requer o `serviceAccount.json` (F0 item 0.4) e a URL do RTDB:
```bash
pip install -r requirements.txt
python rpi_daemon.py \
    --port /dev/serial0 \
    --service-account /home/pi/serviceAccount.json \
    --database-url https://usvs-drone-fluvial-autonomo-default-rtdb.firebaseio.com/ \
    --drone-id drone_01
```

> **Auth:** o RPi usa o **Admin SDK com service account** (privilégio total no RTDB),
> diferente do firmware que usa auth de usuário (email/senha). Gere o service account
> no Firebase Console → Configurações → Contas de serviço → Gerar nova chave privada.

---

# F3 — Resiliência (watchdog + presença + log + systemd)

O daemon agora se recupera sozinho e sobe no boot:

- **Watchdog do link serial:** se a porta cair, reabre com backoff exponencial
  (1s → 2s → … → teto 30s), resetando o backoff ao reconectar. Uma thread de
  heartbeat roda o watchdog a cada 5s.
- **Presença:** publica `/drones/{id}/status/presence` (online + last_seen) a cada
  ciclo e marca `status/online=false` no shutdown — equivale ao `onDisconnect` que
  antes era do ESP32, agora que o RPi é o dono do RTDB.
- **Detecção de silêncio do ESP32:** se nenhuma telemetria chega em 15s, registra
  `esp32_silence` em `/logs` (uma vez, até voltar a receber) — sinaliza reset do
  ESP32 ou cabo solto.
- **Log rotativo:** o daemon loga no journald (via `logging`); o limite de tamanho
  é imposto pelo journald (config abaixo), protegendo o cartão SD em campo.

## Instalar como serviço systemd (no RPi)

```bash
# 1. Rotação/limite do journal (protege o SD)
sudo cp deploy/usv-rpi-daemon.journald.conf /etc/systemd/journald.conf.d/usv.conf
sudo systemctl restart systemd-journald

# 2. Instalar a unit (ajuste caminhos/URL dentro do .service antes)
sudo cp deploy/usv-rpi-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now usv-rpi-daemon

# 3. Acompanhar
systemctl status usv-rpi-daemon
journalctl -u usv-rpi-daemon -f
```

A unit reinicia o daemon sempre que ele cair (`Restart=always`, backoff 3s), com um
freio anti-loop (`StartLimitBurst=5` em 60s). Sobe só após `network-online.target`
(o Firebase precisa de rede).

## Testar a resiliência SEM hardware

O `--selftest` já cobre a F3: exercita o watchdog (2 falhas de abertura → reconexão
com backoff), a publicação de presença, o alerta de silêncio (sem duplicar) e o
offline no shutdown — tudo com um relógio controlável, sem sleeps reais:
```bash
python rpi_daemon.py --selftest
# SELF-TEST F2+F3 OK - ...
```
