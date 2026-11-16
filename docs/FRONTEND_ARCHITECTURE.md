# FRONTEND ARCHITECTURE — SETTLESMART 2.0

## 1. Overview
SettleSmart 2.0's frontend is a modern, high-performance single-page application (SPA) built using React, Vite, TypeScript, and Tailwind CSS. It communicates directly with the high-concurrency pure C backend HTTP server (powered by Mongoose) and the MySQL database.

The frontend is designed with a premium fintech SaaS visual identity (inspired by Linear and Stripe), featuring deep navy backgrounds, dark charcoal cards, emerald green primary accents, and exact paise-denominated financial calculations.

---

## 2. Technology Stack

| Layer | Technology | Purpose |
|---|---|---|
| **Framework / Bundler** | React 19 + Vite 8 | Fast HMR development and optimized production bundling |
| **Language** | TypeScript (Strict) | Compile-time type safety across all API models and contracts |
| **Styling** | Tailwind CSS 3.4 | Utility-first responsive design, custom fintech palette |
| **Routing** | React Router DOM v7 | Client-side routing, protected routes, role-based guards |
| **Icons** | Lucide React | Consistent, lightweight SVG iconography |
| **Charts** | Recharts | Responsive SVG charts for transaction throughput and status breakdown |
| **Canvas / Graph** | Custom Interactive SVG Canvas | Zoomable, draggable, directed weighted graph with parallel curve offsets |
| **Real-Time Stream** | Native WebSocket | Persistent bidirectional connection to `/ws/graph` with auto-reconnect & replay |

---

## 3. Directory Structure

```
frontend/
├── index.html                  # HTML entry point with dark mode & Inter font
├── package.json                # Dependencies and build scripts
├── postcss.config.js           # PostCSS Tailwind plugins
├── tailwind.config.js          # Fintech color palette & theme extensions
├── tsconfig.json               # TypeScript base configuration
├── tsconfig.app.json           # Application compiler configuration
├── vite.config.ts              # Vite server & port 3000 binding
├── .env.example                # Example environment configuration
├── src/
│   ├── main.tsx                # React root mount
│   ├── App.tsx                 # Root router and context provider tree
│   ├── index.css               # Tailwind directives & custom scrollbars
│   ├── types/                  # TypeScript interface definitions
│   │   └── index.ts            # User, Entity, Transaction, Graph, WS models
│   ├── utils/                  # Safe formatting and utility functions
│   │   ├── currency.ts         # Exact paise-to-rupee integer string formatting
│   │   └── uuid.ts             # Cryptographic UUID v4 generator for Idempotency
│   ├── api/                    # Centralized API communication layer
│   │   ├── client.ts           # Credentialed fetch wrapper with CSRF injection
│   │   ├── auth.ts             # Login, register, me, logout, change password
│   │   ├── entities.ts         # Entity CRUD & status management
│   │   ├── transactions.ts     # Transaction submission, history, reversal
│   │   ├── processing.ts       # Max-heap priority queue & batch processing
│   │   ├── users.ts            # Organization user and role management
│   │   └── graph.ts            # Snapshot retrieval with sequence watermark
│   ├── contexts/               # Global state providers
│   │   ├── AuthContext.tsx     # Session state, role permissions, login/logout
│   │   ├── WebSocketContext.tsx# Real-time connection, heartbeat, event dispatcher
│   │   └── ToastContext.tsx    # In-app toast notifications
│   ├── layouts/                # Common structural wrappers
│   │   └── AppLayout.tsx       # Collapsible sidebar + navbar + content outlet
│   ├── components/             # Reusable UI components
│   │   ├── common/             # Badges, Cards, Modals, Drawers, Skeletons
│   │   ├── dashboard/          # KPI Cards, Activity Chart, Status Donut
│   │   ├── graph/              # Interactive Financial Graph SVG Canvas
│   │   └── settlement/         # Directional settlement flow visualizations
│   ├── pages/                  # Page route views
│   │   ├── LoginPage.tsx       # Secure credential login
│   │   ├── RegisterPage.tsx    # Organization onboarding & admin setup
│   │   ├── DashboardPage.tsx   # Primary executive overview
│   │   ├── EntitiesPage.tsx    # Entity management directory
│   │   ├── TransactionsPage.tsx# Ledger & Max-Heap priority processing
│   │   ├── BalancesPage.tsx    # Entity balance ledger (paise exact)
│   │   ├── SettlementPage.tsx  # Greedy algorithm recommendations
│   │   ├── GraphPage.tsx       # Fullscreen interactive graph canvas
│   │   ├── SettingsPage.tsx    # Profile, password, team management
│   │   └── NotFoundPage.tsx    # 404 fallback page
│   └── routes/
│       └── AppRoutes.tsx       # Protected and public route definitions
```

---

## 4. State Management Architecture

The application adopts a lightweight, context-driven state architecture to prevent unnecessary third-party dependencies:
1. **AuthContext**:
   - Maintains user identity, active organization ID, and role (`Admin`, `Finance_Manager`, `Viewer`).
   - Automatically validates session status upon initialization via `GET /api/auth/me`.
   - Never stores session tokens or credentials in `localStorage`.
   - Remembers only the user's last selected numeric Organization ID in `localStorage` for input convenience.
2. **WebSocketContext**:
   - Manages connection lifecycle (`LIVE`, `RECONNECTING`, `OFFLINE`).
   - Sends `{"type":"PING"}` heartbeats every 25 seconds.
   - Maintains the client's `lastSeenSequence` watermark.
   - Upon connection or recovery, transmits `{"type":"SUBSCRIBE_GRAPH", "last_seen_sequence": N}` to replay missed outbox events from MySQL.
   - Dispatches real-time events to all registered page listeners.
3. **ToastContext**:
   - In-app notification queue displaying real-time feedback for newly created transactions, completed processing batches, and network connection changes.

---

## 5. Security & Isolation

- **Credentialed Transport**: All HTTP requests use `credentials: 'include'`.
- **HttpOnly Cookies**: Session tokens (`session_token`) are stored in secure, HttpOnly, SameSite cookies managed by the browser and the C backend.
- **CSRF Token Injection**: State-changing requests (`POST`, `PATCH`, `DELETE`) automatically attach the `X-CSRF-Token` header.
- **Automatic CSRF Recovery**: If a request encounters a `403 Invalid or missing CSRF token` error (e.g., token rotation in another tab), the client automatically queries `POST /api/auth/csrf` and retries the request once.
- **Idempotency**: All transaction creation and reversal requests generate a UUID v4 header (`Idempotency-Key`), preventing duplicate transaction creation across network retries.
- **Exact Currency Handling**: Financial numbers are authoritatively calculated in `int64_t` paise by the backend. The frontend converts paise to rupees using decimal-safe BigInt/integer string manipulation (`formatPaiseToINR`), preventing JavaScript floating-point rounding inaccuracies.
