# Projeto_PEX — Frontend (React + Vite)

Uma aplicação frontend em React (Vite) usada como painel para o projeto USVs / Drone Fluvial Autônomo. A interface consome dados via Firebase Realtime Database e fornece visualização de telemetria em um mapa 3D usando Cesium.

**Tecnologias principais:** React, Vite, Firebase, Cesium, ESLint.

**Objetivo deste README:** descrever como preparar o ambiente, executar em modo de desenvolvimento, gerar build de produção e onde configurar o Firebase.

**Pré-requisitos:**
- **Node.js** (recomendado >= 16) e **npm** instalados.
- Conta Firebase com um projeto configurado (Realtime Database e Auth se necessário).

**Passo a passo — Instalação e execução**

1. Abra um terminal e entre na pasta do frontend:

```
cd Projeto_PEX
```

2. Copie o arquivo de exemplo de variáveis de ambiente e preencha com as credenciais do seu projeto Firebase:

```
cp .env.example .env
# (ou copie manualmente no Windows)
```

Edite o arquivo `.env` e preencha as chaves VITE_FIREBASE_... conforme descrito em [.env.example](Projeto_PEX/.env.example).

3. Instale dependências:

```
npm install
```

4. Execute a aplicação em modo de desenvolvimento (HMR):

```
npm run dev
```

Abra o navegador em `http://localhost:5173` (ou endereço mostrado pelo Vite).

5. Build de produção e preview local:

```
npm run build
npm run preview
```

6. Lint (verificação estática):

```
npm run lint
```

**Variáveis de ambiente**
- Veja o arquivo de exemplo: [Projeto_PEX/.env.example](Projeto_PEX/.env.example).
- Principais variáveis (preencha com os dados do seu Firebase): `VITE_FIREBASE_API_KEY`, `VITE_FIREBASE_AUTH_DOMAIN`, `VITE_FIREBASE_DATABASE_URL`, `VITE_FIREBASE_PROJECT_ID`, `VITE_FIREBASE_STORAGE_BUCKET`, `VITE_FIREBASE_MESSAGING_SENDER_ID`, `VITE_FIREBASE_APP_ID`.
- Observação: se faltar alguma chave, a aplicação poderá rodar em modo mock (sem conexão real com Firebase).

**Scripts disponíveis**
- **dev**: `npm run dev` — inicia o servidor de desenvolvimento (Vite).
- **build**: `npm run build` — gera artefatos para produção.
- **preview**: `npm run preview` — serve a build localmente para teste.
- **lint**: `npm run lint` — executa o ESLint na base do projeto.

**Estrutura importante do projeto**
- `src/main.jsx`: ponto de entrada da aplicação.
- `src/App.jsx`: componente principal.
- `src/components/`: componentes de UI (LoginPanel, MapPanel, TelemetryPanel, etc.).
- `src/hooks/`: hooks personalizados (`useAuthState.js`, `useFirebaseDrone.js`, `useMockDrone.js`).
- `src/lib/firebase.js`: inicialização e helpers do Firebase ([Projeto_PEX/src/lib/firebase.js](Projeto_PEX/src/lib/firebase.js)).
- `public/`: arquivos estáticos.

**Configuração do Firebase (resumo rápido)**
1. Crie um projeto no Firebase Console.
2. Habilite o Realtime Database (ou Firestore conforme uso) e configure regras.
3. Habilite o provedor Email/Password em Authentication se for usar login por e-mail.
4. Copie as credenciais do app (API Key, Auth domain, Database URL, etc.) para `.env`.
5. Opcional: crie um usuário operador para testes (sugestão `operador@usv-am.local`).

**Notas e dicas**
- A aplicação usa **Cesium** (globo 3D) para o mapa. O botão *Seguir veículo* no canto do mapa aproxima e acompanha o drone durante a navegação; desligado, mantém a visão ampla. O CSS do Cesium é importado em `src/components/MapPanel.jsx`.
- Para desenvolvimento off-line ou demonstração sem Firebase, use o hook `useMockDrone.js`.

**Contribuição**
- Abra uma issue descrevendo a proposta antes de PRs grandes.
- Siga as regras de lint antes de submeter PRs (`npm run lint`).

**Contato / Suporte**
- Para dúvidas sobre integração Firebase, veja [Projeto_PEX/src/lib/firebase.js](Projeto_PEX/src/lib/firebase.js) e os hooks em [Projeto_PEX/src/hooks](Projeto_PEX/src/hooks).

---
Atualizado: instruções de execução e configuração para o frontend `Projeto_PEX`.
