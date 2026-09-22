# Tutorial — Configurar o Raspberry Pi 4 (USV-AM)

Guia do zero: gravar o SO, acessar por SSH, instalar as dependências, habilitar a
UART e deixar o daemon de bordo (`rpi_daemon.py`) rodando como serviço systemd.

Cobre a **F0** (preparação) e a instalação do que já foi implementado nas
**F2/F3** (daemon + resiliência). O código do RPi vive em
`C:\Users\orlan\PycharmProjects\USVs-Drone-Fluvial-Autonomo\rpi`.

> **Por que SSH e não USB:** o RPi4 não tem armazenamento interno e não vira um
> dispositivo USB ao ligar na porta OTG — ele boota de um microSD e entra na rede.
> O acesso por SSH via Wi-Fi/ethernet é o ambiente real de produção onde o daemon
> vai rodar, então é por aí que configuramos.

---

## Pré-requisitos

- Raspberry Pi 4 (2 GB+), fonte 5V/3A
- microSD 32 GB (classe A2 recomendada) + leitor de cartão no PC
- Rede Wi-Fi (SSID + senha) ou cabo ethernet
- Projeto Firebase com um **service account** (arquivo JSON) — ver Passo 6

---

## Passo 1 — Gravar o Raspberry Pi OS no microSD

1. Instale o **Raspberry Pi Imager** no seu PC: https://www.raspberrypi.com/software/
2. Insira o microSD no leitor.
3. No Imager:
   - **Choose Device:** Raspberry Pi 4
   - **Choose OS:** Raspberry Pi OS (other) → **Raspberry Pi OS Lite (64-bit)**
     (sem desktop — o RPi roda headless)
   - **Choose Storage:** o microSD
4. Clique em **Next** → **Edit Settings** (a engrenagem ⚙️) e configure **antes de gravar**:
   - **Set hostname:** `usv-rpi`
   - **Enable SSH:** ✅ (autenticação por senha, ou cole sua chave pública)
   - **Set username and password:** ex. usuário `orlando` + uma senha
   - **Configure wireless LAN:** seu SSID + senha + país (`BR`)
   - **Set locale:** timezone `America/Manaus` (ou a sua), teclado `br`
5. **Save** → **Write**. Aguarde gravar e verificar. Ejete o cartão.

---

## Passo 2 — Primeiro boot

1. Insira o microSD no RPi.
2. Ligue a fonte 5V/3A. O LED verde deve **piscar** (atividade do SD) — se só o
   vermelho acende e o verde fica apagado, o SD não bootou (regrave o Passo 1).
3. Aguarde ~1–2 min no primeiro boot (ele expande o filesystem e conecta no Wi-Fi).

---

## Passo 3 — Acessar por SSH (do seu PC Windows)

No PowerShell:
```powershell
ssh orlando@usv-rpi.local
```
Se `usv-rpi.local` não resolver (mDNS bloqueado em algumas redes), descubra o IP
no painel do seu roteador ou com um scanner, e use o IP:
```powershell
ssh orlando@192.168.x.y
```
Aceite a fingerprint na primeira conexão e entre com a senha definida no Passo 1.

---

## Passo 4 — Atualizar o sistema e instalar dependências de SO

Já dentro do RPi (via SSH):
```bash
sudo apt update && sudo apt full-upgrade -y
sudo apt install -y git python3-venv python3-pip
sudo reboot   # se o kernel foi atualizado
```
Reconecte por SSH após o reboot.

---

## Passo 5 — Habilitar a UART de hardware (link com o ESP32)

O daemon fala com o ESP32 pela UART do RPi (GPIO14 TXD / GPIO15 RXD).

```bash
sudo raspi-config
```
- **3 Interface Options → I6 Serial Port**
  - *"Would you like a login shell over serial?"* → **No** (libera a UART pro daemon)
  - *"Would you like the serial port hardware enabled?"* → **Yes**
- **Finish** → reiniciar quando pedir.

Depois do reboot, a porta fica em `/dev/serial0`. Garanta que seu usuário está no
grupo `dialout` (necessário para abrir a serial):
```bash
sudo usermod -aG dialout $USER
# saia e reentre no SSH para o grupo valer:
exit
```
Reconecte e confirme:
```bash
groups   # deve listar 'dialout'
ls -l /dev/serial0
```

### Ligação física RPi ↔ ESP32 (3 fios, 3.3V direto)

| RPi (pino BCM) | ESP32 |
|---|---|
| GPIO14 TXD → | GPIO16 (RX2) |
| GPIO15 RXD ← | GPIO17 (TX2) |
| GND | GND |

> Ambos são 3.3V — **não** use level shifter. TX de um vai no RX do outro (cruzado).

---

## Passo 6 — Service account do Firebase

O daemon usa o **Admin SDK** com uma chave de service account (privilégio total no
RTDB), diferente do firmware que usa email/senha.

1. Firebase Console → ⚙️ **Configurações do projeto** → **Contas de serviço**
2. **Gerar nova chave privada** → baixa um JSON.
3. Copie o JSON para o RPi (do seu PC):
   ```powershell
   scp C:\caminho\serviceAccount.json orlando@usv-rpi.local:/home/orlando/serviceAccount.json
   ```
4. Proteja o arquivo no RPi:
   ```bash
   chmod 600 /home/orlando/serviceAccount.json
   ```

---

## Passo 7 — Clonar o projeto e criar o ambiente Python

```bash
cd ~
git clone https://github.com/Vandrekad/USVs-Drone-Fluvial-Autonomo.git
cd USVs-Drone-Fluvial-Autonomo/rpi

python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt   # pyserial + firebase-admin
```

### Validar sem hardware antes de conectar o ESP32

```bash
python rpi_daemon.py --selftest        # F2+F3 em memória
python test_integration.py             # ESP32 simulado <-> bridge por fio em memória
```
Ambos devem terminar com `OK`.

---

## Passo 8 — Rodar o daemon manualmente (teste com hardware)

Com o ESP32 flashado e ligado pelos 3 fios do Passo 5:
```bash
source ~/USVs-Drone-Fluvial-Autonomo/rpi/.venv/bin/activate
python rpi_daemon.py \
    --port /dev/serial0 \
    --service-account /home/orlando/serviceAccount.json \
    --database-url https://usvs-drone-fluvial-autonomo-default-rtdb.firebaseio.com/ \
    --drone-id drone_01
```
Você deve ver no log: `link serial aberto`, telemetria chegando do ESP32, e o
status/telemetria aparecendo no RTDB. `Ctrl+C` para parar.

---

## Passo 9 — Instalar como serviço systemd (auto-start + restart)

Assim o daemon sobe sozinho no boot e reinicia se cair (F3).

Ajuste os caminhos/usuário dentro do `.service` se você não usou `orlando`/paths
padrão (o arquivo versionado usa `pi`; edite `User=`, `WorkingDirectory=`,
`ExecStart=` e o caminho do `serviceAccount.json`):
```bash
cd ~/USVs-Drone-Fluvial-Autonomo/rpi

# 1. Limite/rotação do journal (protege o cartão SD em campo)
sudo cp deploy/usv-rpi-daemon.journald.conf /etc/systemd/journald.conf.d/usv.conf
sudo systemctl restart systemd-journald

# 2. Instalar a unit
sudo cp deploy/usv-rpi-daemon.service /etc/systemd/system/
sudo nano /etc/systemd/system/usv-rpi-daemon.service   # ajuste User/paths/URL
sudo systemctl daemon-reload
sudo systemctl enable --now usv-rpi-daemon
```

Acompanhar:
```bash
systemctl status usv-rpi-daemon
journalctl -u usv-rpi-daemon -f
```

O serviço reinicia sozinho em caso de falha (`Restart=always`, backoff 3s) com um
freio anti-loop (5 falhas em 60s → para). Só sobe após a rede estar pronta.

---

## Checklist final (F0 concluída)

- [ ] RPi bootando e acessível por SSH (`ssh orlando@usv-rpi.local`)
- [ ] Sistema atualizado, `git` e `python3-venv` instalados
- [ ] UART habilitada (`/dev/serial0`), usuário no grupo `dialout`
- [ ] `serviceAccount.json` copiado e com `chmod 600`
- [ ] Projeto clonado, `.venv` criado, `requirements.txt` instalado
- [ ] `--selftest` e `test_integration.py` passam (`OK`)
- [ ] `usv-rpi-daemon` habilitado e ativo no systemd

Com isso a F0 fecha. O bring-up com sensores/motores reais é a F4 (a partir de 19/09).

---

## Troubleshooting rápido

| Sintoma | Causa provável | Ação |
|---|---|---|
| Só LED vermelho, verde apagado | SD não bootou | Regravar o SO (Passo 1) |
| `usv-rpi.local` não resolve | mDNS bloqueado na rede | Usar o IP direto (Passo 3) |
| `Permission denied` em `/dev/serial0` | Usuário fora do `dialout` | `sudo usermod -aG dialout $USER` + relogar |
| `/dev/serial0` não existe | UART não habilitada | `raspi-config` (Passo 5) + reboot |
| Daemon dá `firebase-admin não instalado` | venv não ativado / deps faltando | `source .venv/bin/activate && pip install -r requirements.txt` |
| Telemetria não sobe no RTDB | service account errado / DB URL errada | Conferir `--service-account` e `--database-url` |
| `esp32_silence` nos logs | ESP32 mudo (reset/cabo solto) | Conferir os 3 fios e alimentação do ESP32 |
