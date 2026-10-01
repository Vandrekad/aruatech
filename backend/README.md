# Backend — Firebase RTDB (USV-AM)

> Camada de nuvem do **USV-AM**: as **Security Rules** do Realtime Database (controle de
> acesso + validação de schema) e uma ferramenta Node de **retenção de logs**. Não há
> servidor de aplicação aqui — o Firebase RTDB é o backend, e o frontend (React SDK) e o
> **RPi4** (Admin SDK) falam diretamente com ele.

![backend](https://img.shields.io/badge/backend-Firebase%20RTDB-f59e0b)
![tools](https://img.shields.io/badge/tools-Node%20%2B%20firebase--admin-3c873a)

---

## Índice

- [1. Papel no sistema](#1-papel-no-sistema)
- [2. Conteúdo da pasta](#2-conteúdo-da-pasta)
- [3. Modelo de dados](#3-modelo-de-dados)
- [4. Controle de acesso (rules)](#4-controle-de-acesso-rules)
- [5. Pré-requisitos](#5-pré-requisitos)
- [6. Passo a passo — configurar e publicar](#6-passo-a-passo--configurar-e-publicar)
- [7. Retenção de logs (limpeza automática)](#7-retenção-de-logs-limpeza-automática)
- [8. Variáveis de ambiente](#8-variáveis-de-ambiente)
- [9. Como se conecta aos outros componentes](#9-como-se-conecta-aos-outros-componentes)
- [10. Documentos de referência](#10-documentos-de-referência)

---

## 1. Papel no sistema

O backend é **serverless**: toda a lógica de tempo real acontece no próprio Firebase RTDB.
Ele cumpre três funções:

1. **Sincronização em tempo real** — telemetria/status/rota/logs escritos pelo RPi (ou pelo
   ESP32 no modo de contingência) e lidos pelo dashboard ao vivo.
2. **Controle de acesso e validação** — as Security Rules (`firebase-rtdb.rules.json`)
   definem quem lê/escreve cada nó e validam a estrutura mínima de cada payload.
3. **Retenção** — o script `scripts/cleanup-logs.mjs` remove logs antigos para conter
   custo e tamanho da árvore.

> **Este componente não roda um daemon.** O único código executável é o script de limpeza,
> pensado para rodar sob agendamento (cron/Cloud Scheduler/CI), não continuamente.

---

## 2. Conteúdo da pasta

| Arquivo | Função |
|---|---|
| `firebase-rtdb.rules.json` | **Security Rules** do RTDB — permissões por papel (`operator`/`admin`/`firmware`) + `.validate` de schema. Publicar no Firebase. |
| `scripts/cleanup-logs.mjs` | Limpeza de `/logs` por `timestamp` e `expires_at` (usa `firebase-admin`). |
| `package.json` | Define o script `cleanup:logs` e a dependência `firebase-admin`. |
| `RTDB_SCHEMA.md` | Contrato de dados detalhado de `/drones`, `/missions`, `/logs`. |
| `LOG_RETENTION_POLICY.md` | Política de retenção (90 dias por padrão) e critérios de aceite. |
| `FIREBASE_SETUP_CHECKLIST.md` | Checklist de configuração do projeto Firebase. |
| `FIREBASE_OPERATOR_ACCOUNT.md` | Conta do operador (UID/e-mail) e mapeamento para roles. |

---

## 3. Modelo de dados

Árvore do RTDB (detalhes e exemplos completos em [`RTDB_SCHEMA.md`](./RTDB_SCHEMA.md)):

```
/
├── drones/{drone_id}/
│   ├── telemetry/   # posição, sensores, atuadores — atualizado a cada 1–2s
│   ├── status/      # estado operacional (online, nav_state, active_leg, ...)
│   └── command/     # comando do operador (set_destination | emergency_stop)
├── missions/{mission_id}/
│   ├── route/       # waypoints da rota
│   └── path/        # histórico de posições (p_{ts})
└── logs/{log_id}/   # eventos (obstacle_detected, connection_lost, ...)
```

**Quem escreve o quê:** o **RPi4** (Admin SDK) publica `telemetry`, `status`, `path` e
`logs`; o **frontend** escreve `command` e cria `missions`; o **firmware** só escreve
diretamente no modo de contingência (RPi ausente por >30s).

---

## 4. Controle de acesso (rules)

`firebase-rtdb.rules.json` — fechado por padrão (`.read`/`.write` = `false` na raiz), abrindo
por nó conforme o papel do token de autenticação:

| Nó | Leitura | Escrita | Validação (`.validate`) |
|---|---|---|---|
| `drones/{id}/telemetry` | operator, admin, firmware, UID operador | firmware, admin, UID operador | exige `position, sensors, actuators, mission_id, timestamp` |
| `drones/{id}/status` | idem | firmware, admin, UID operador | exige `online, last_seen, active_mission_id, nav_state, active_leg, route_progress, last_position` |
| `drones/{id}/command` | operator, admin, firmware, UID operador | operator, admin, UID operador | exige `command_id, cmd_type, mission_id, issued_at` |
| `missions/{id}` | operator, admin, firmware, UID operador | operator, admin, firmware, UID operador | exige `drone_id, start_time, status, origin, target, route` |
| `missions/{id}/path` | (herda) | firmware, admin, UID operador | — |
| `logs/{id}` | operator, admin, UID operador | firmware, admin, UID operador | exige `drone_id, mission_id, type, timestamp` |

> **Papéis:** definidos por **custom claims** (`auth.token.role`). Enquanto os claims não
> estiverem configurados, as rules liberam explicitamente o UID do operador
> (`do01A3JzdRb5z4ulrCoVcy1mA8A2`) — ver [`FIREBASE_OPERATOR_ACCOUNT.md`](./FIREBASE_OPERATOR_ACCOUNT.md).
> O RPi usa o **Admin SDK** (service account), que ignora as rules e tem acesso total.

---

## 5. Pré-requisitos

| Para | Ferramenta |
|---|---|
| Publicar as rules | [Firebase CLI](https://firebase.google.com/docs/cli) (`npm i -g firebase-tools`) **ou** o Console. |
| Rodar a limpeza de logs | Node.js ≥ 18 + `firebase-admin` (via `npm install`) e um **service account** com acesso ao RTDB. |

---

## 6. Passo a passo — configurar e publicar

### 6.1 Criar/configurar o projeto Firebase

Siga [`FIREBASE_SETUP_CHECKLIST.md`](./FIREBASE_SETUP_CHECKLIST.md): criar o projeto, habilitar
**Realtime Database** e **Authentication**, e criar a conta do operador.

### 6.2 Publicar as Security Rules

Pelo **Console:** Realtime Database → aba **Regras** → cole o conteúdo de
`firebase-rtdb.rules.json` → **Publicar**.

Pela **CLI** (a partir da raiz do repo, com `firebase.json` apontando para o arquivo de rules):

```bash
firebase deploy --only database
```

### 6.3 Validar

- Insira um payload de telemetria de teste no RTDB e confirme que passa no `.validate`.
- Abra o dashboard autenticado e confirme leitura de `telemetry`/`status`/`logs`.

---

## 7. Retenção de logs (limpeza automática)

Remove entradas de `/logs` mais antigas que o prazo de retenção (padrão **90 dias**),
por `timestamp` e por `expires_at` (prazo explícito opcional). Detalhes e critérios de
aceite em [`LOG_RETENTION_POLICY.md`](./LOG_RETENTION_POLICY.md).

```bash
cd backend
npm install

# credenciais + URL do RTDB (ver seção 8)
export FIREBASE_DATABASE_URL="https://<seu-projeto>-default-rtdb.firebaseio.com/"
export FIREBASE_SERVICE_ACCOUNT_JSON="$(cat /caminho/serviceAccount.json)"   # ou use ADC

npm run cleanup:logs
```

No Windows (PowerShell):

```powershell
cd backend
npm install
$env:FIREBASE_DATABASE_URL = "https://<seu-projeto>-default-rtdb.firebaseio.com/"
$env:FIREBASE_SERVICE_ACCOUNT_JSON = (Get-Content C:\caminho\serviceAccount.json -Raw)
npm run cleanup:logs
```

A saída resume quantos logs foram removidos por `timestamp` e por `expires_at`.

**Agendamento recomendado:** 1×/dia em janela de baixo uso (ex.: 02:00), via Cloud
Scheduler, cron ou GitHub Actions.

---

## 8. Variáveis de ambiente

Consumidas por `scripts/cleanup-logs.mjs`:

| Variável | Obrigatória | Default | Descrição |
|---|---|---|---|
| `FIREBASE_DATABASE_URL` | **sim** | — | URL do RTDB (ex.: `https://<proj>-default-rtdb.firebaseio.com/`). |
| `FIREBASE_SERVICE_ACCOUNT_JSON` | não | ADC | JSON do service account. Se ausente, usa *Application Default Credentials* (`applicationDefault()`). |
| `LOG_RETENTION_DAYS` | não | `90` | Prazo de retenção em dias. |
| `LOG_MAX_DELETES_PER_RUN` | não | `2000` | Teto de deleções por execução. |
| `LOG_DELETE_BATCH_SIZE` | não | `500` | Tamanho de cada lote de deleção. |

> ⚠️ O `serviceAccount.json` é **segredo** — nunca faça commit dele. Em CI, injete-o por
> secret e passe via `FIREBASE_SERVICE_ACCOUNT_JSON`.

---

## 9. Como se conecta aos outros componentes

```
Frontend (React SDK, auth de usuário) ──escreve command / cria missions──▶ RTDB
RPi4 (Admin SDK, service account) ──publica telemetry/status/path/logs──▶ RTDB
Frontend ◀──escuta telemetry/status/path/logs em tempo real── RTDB
Firmware (contingência, RPi ausente) ──publica direto── RTDB
```

- As **rules** (§4) impõem quem pode fazer cada escrita acima.
- O **RPi** autentica com service account (Admin SDK) — o mesmo tipo de credencial que a
  limpeza de logs usa (§8).
- O **frontend** autentica como o operador (anonymous/email-password) e depende das rules
  para escrever `command`/`missions`.

Visão geral de todo o sistema no [README principal](../README.md).

---

## 10. Documentos de referência

- [`RTDB_SCHEMA.md`](./RTDB_SCHEMA.md) — contrato de dados completo, com exemplos.
- [`LOG_RETENTION_POLICY.md`](./LOG_RETENTION_POLICY.md) — política de retenção.
- [`FIREBASE_SETUP_CHECKLIST.md`](./FIREBASE_SETUP_CHECKLIST.md) — checklist de configuração.
- [`FIREBASE_OPERATOR_ACCOUNT.md`](./FIREBASE_OPERATOR_ACCOUNT.md) — conta do operador e roles.
