# SettleSmart 2.0
## Intelligent Transaction Management and Financial Settlement Platform

SettleSmart 2.0 is a robust financial transaction management and settlement platform designed for businesses, enterprises, and institutions handling complex transaction flows between multiple internal and external entities (departments, vendors, subsidiaries, clients, and partners).

This system is built from the ground up to replace naive expense-splitting utilities with a structured transaction-processing engine, an animated directed weighted transaction graph, and an algorithmic settlement calculator.

---

> [!WARNING]
> **CRITICAL CONFIGURATION SHARING RULE**:
> Never share your personal `.env` file or commit it to version control! The `.env` file contains sensitive local database credentials and is strictly excluded by `.gitignore`.
> When sharing the project with others through GitHub or ZIP archives, share `.env.example` instead. Each person must create their own local `.env` file and configure their own database credentials.

---

## Supported Database Engines

- **Supported LTS Versions**:
  - **MySQL 8.4 LTS** (Long Term Support) — Recommended production target.
  - **MySQL 9.7 LTS** (Long Term Support) — Verified local development target.
- *Notice*: SettleSmart 2.0 does not claim blanket support for all non-LTS intermediate innovation releases without explicit testing. Do not upgrade database servers without checking C library ABI compatibility.

---

## Key Capabilities

1. **Multi-Organization Isolation via Composite Foreign Keys**: Strict tenant data isolation. The backend derives `organization_id` strictly from server-side session context, and MySQL enforces composite foreign keys `(organization_id, id)` across entities, users, and transactions at the database engine level.
2. **Deterministic Automatic ID Generation & Gap Tolerance**: Database-driven automatic ID generation for entities and transactions via `AUTO_INCREMENT`. The application logic explicitly accounts for gaps in ID sequences. Manual ID injection is strictly prohibited.
3. **Exact Financial Precision**: All financial amounts are stored as exact MySQL `DECIMAL(15, 2)` representations and calculated in the C backend using 64-bit integer arithmetic (`int64_t` paise). Binary floating-point arithmetic is strictly prohibited.
4. **Network Retry Idempotency**: Transactions incorporate an `idempotency_key` unique per organization, preventing duplicate financial transactions during client network retries.
5. **High-Performance C Backend with Explicit Config Loader**: Engineered in C using standard modular architecture. Features an explicit `.env` configuration loader module (`env_loader`) and direct database connectivity via the MySQL C API.
6. **Persistent MySQL Source of Truth**: All operational state is permanently recorded in relational MySQL tables with strict referential integrity, UTC timestamps, and definitive commit ordering.
7. **Applied Academic Data Structures & Algorithms (DSA)**:
   - **Directed Weighted Graph**: Models multi-party transaction topologies over an active window in memory; complete history is queried from MySQL on demand.
   - **HashMap**: Entity net balance tracking in constant time using exact `int64_t` precision.
   - **Linked List**: Chronological transaction ledger records for active session data.
   - **FIFO Queue**: Ingress networking buffer scheduling worker threads (distinct from authoritative database commit order).
   - **Stack (LIFO)**: Recent transaction activity display and audit inspection; financial rollbacks require explicit compensatory reversal transactions.
   - **Priority Queue (Max-Heap)**: Priority-based transaction scheduling and high-value monitoring.
   - **Bubble Sort & Binary Search**: Algorithmic sorting and bounded querying demonstrations on small in-memory datasets.
   - **Greedy Settlement Algorithm**: Heuristic bilateral debt minimization generating practical settlement recommendations (without unproven optimality claims).
8. **Real-Time Interactive Graph**: WebSocket-powered live visual graph with animated directed edges indicating capital movements, broadcast strictly post-database commit.
9. **Dual Operating Modes**:
   - **Local Installation**: Standardized local setup via environment-driven configuration for development, classroom study, and source code sharing.
   - **Online Cloud Deployment**: Centralized multi-tenant cloud architecture serving multiple users through a unified web application.

---

## Repository Structure

```
SettleSmart-2.0/
├── backend/
│   ├── Makefile                 # Automated cross-platform build script
│   ├── src/
│   │   ├── auth/                # crypto_utils, password, authorization, session, auth_service
│   │   ├── config/              # env_loader.c / env_loader.h
│   │   ├── database/            # db_client.c / db_client.h
│   │   ├── entities/            # entity.c / entity.h, entity_service.c / entity_service.h
│   │   ├── transactions/        # transaction.c, transaction_service.c, transaction_processor.c
│   │   ├── dsa/                 # linked_list.c, hashmap.c, queue.c, stack.c, priority_queue.c, dsa_state.c
│   │   ├── graph/               # graph.c / graph.h, graph_service.c / graph_service.h
│   │   ├── websocket/           # event_dispatcher.c / event_dispatcher.h, websocket.c / websocket.h
│   │   ├── algorithms/          # bubble_sort.c, binary_search.c, settlement.c
│   │   └── http/                # http_server.c, entity_routes.c, transaction_routes.c, graph_routes.c, processing_routes.c
│   ├── tests/                   # 56 test suites covering config, DB, integrity, auth, entities, transactions, DSA, graph, processing, multi-instance & algorithms
│   └── third_party/             # Argon2 reference library & Mongoose HTTP/WebSocket
├── frontend/                    # React 19 + Vite 8 + TypeScript + Tailwind CSS Frontend (Step 10 Completed)
│   ├── src/                     # App, components, pages, contexts, api, utils
│   └── dist/                    # Production static build
├── database/
│   ├── schema.sql               # Authoritative MySQL physical DDL schema
│   └── migrations/              # Database migration scripts (003, 004, 005, 006)
├── docs/                        # System architecture and technical documentation
│   ├── ARCHITECTURE.md          # Comprehensive architectural specification
│   ├── FRONTEND_ARCHITECTURE.md # React frontend architectural specification
│   ├── UI_COMPONENTS.md         # Component inventory and design system
│   ├── FRONTEND_API_INTEGRATION.md # API endpoints mapping and protocols
│   ├── FRONTEND_DEPLOYMENT.md   # Local run & production deployment instructions
│   ├── AUTHENTICATION.md        # Technical documentation of auth & RBAC subsystem
│   ├── ENTITY_MANAGEMENT.md     # Technical documentation of entity management & RBAC
│   ├── TRANSACTION_MANAGEMENT.md# Technical documentation of transaction engine & idempotency
│   ├── TRANSACTION_PROCESSING.md# Technical documentation of transaction processor & state transitions
│   ├── GREEDY_SETTLEMENT.md     # Mathematical specification of greedy settlement algorithm
│   ├── SORTING_AND_SEARCHING.md # Algorithmic specification of Bubble Sort & Binary Search
│   ├── BALANCE_CALCULATION.md   # Exact integer paise arithmetic & reversal neutralization rules
│   ├── KNOWN_ISSUES.md          # System constraints, architectural observations & edge cases
│   ├── DSA_IMPLEMENTATION.md    # Technical documentation of DSA in-memory structures & state
│   ├── GRAPH_IMPLEMENTATION.md  # Technical documentation of Directed Weighted Graph & bounded cache
│   ├── REALTIME_COMMUNICATION.md# Technical documentation of WebSocket streaming & transactional outbox
│   ├── DATABASE_DESIGN.md       # Conceptual database schema, composite FKs & precision
│   ├── TRANSACTION_FLOW.md      # Detailed transaction lifecycle & idempotency
│   ├── REALTIME_GRAPH.md        # WebSocket, windowed graph & directed canvas edges
│   ├── DEPLOYMENT.md            # Local installation, cloud deployment & C env loader
│   ├── DEVELOPMENT_ROADMAP.md   # The 10 development stages and deliverables
│   ├── VALIDATION_STEP1.md      # Step 1 conceptual dummy scenario validation
│   └── VALIDATION_STEP2.md      # Step 2 physical execution validation results
├── .env.example                 # Environment variable template
├── .gitignore                   # Version control ignore rules
├── README.md                    # Project overview
└── PROJECT_GUIDE.md             # Central project documentation explaining files & modules
```

---

## 10-Step Development Sequence

- **Step 1 — Project Foundation and Architecture** (Completed & Approved)
- **Step 2 — MySQL Database Integration & Configuration Loader** (Completed & Verified)
- **Step 3 — Authentication and Organization Management** (Completed & Verified: 12 test suites, 121 checks passing)
- **Step 4 — Entity Management** (Completed & Verified: 17 test suites, 253 checks passing)
- **Step 5 — Transaction Management & Idempotency** (Completed & Verified: 24 test suites, 436 checks passing)
- **Step 6 — Core DSA Implementation** (Completed & Verified: 32 test suites, 660 checks passing)
- **Step 7 — Graph and Real-Time Communication** (Completed & Verified: 43 test suites, 828 checks passing)
- **Step 8 — Transaction Engine and Algorithms** (Completed & Verified: 50 test suites, 933+ checks passing)
- **Step 9 — Complete Backend Integration and Testing** (Awaiting explicit authorization)
- **Step 10 — Frontend and Final Integration**

---

## Current Status

Step 8 Transaction Processing Engine & Algorithms has been fully implemented, rigorously tested, and verified across all operational layers:
1. **Central Transaction Processing Engine**: Orchestrates ACID transactions, row-level locking (`SELECT ... FOR UPDATE`), ordered entity verification, atomic `TRANSACTION_STATUS_CHANGED` outbox events, and post-commit DSA synchronization.
2. **Reversal Processing & Neutralization**: Original `Completed` transactions coupled with `Pending` reversals are atomically transitioned to `Reversed` and `Completed` respectively, restoring net entity balances to exact pre-transaction values without destructive row deletions.
3. **FIFO Queue & Priority Queue Integration**: FIFO Queue drives sequential order-of-admission execution (`process_next`), while the array-based Max-Heap Priority Queue enables priority-based inspection by amount, timestamp, and ID.
4. **Greedy Settlement Algorithm**: Partitions entities into debtors and creditors, asserts the zero-sum balance invariant, and greedily matches the largest debtor with the largest creditor to minimize bilateral transaction volume.
5. **Bubble Sort**: In-memory stable sorting by financial amount, timestamp, or ID in ascending or descending order with deterministic tie-breaking and early termination.
6. **Binary Search**: $O(\log n)$ random-access search for transactions and entities on pre-sorted arrays.
7. **REST Endpoints & RBAC**: Complete suite of 8 new HTTP endpoints protected by session cookies, CSRF tokens, and role-based permissions (Admin, Finance_Manager, Viewer).

The complete test suite now executes **50 test suites** passing **933+ checks** (100% success rate) with zero regressions, zero compiler errors, and zero source code comments. Awaiting explicit user authorization before proceeding to Step 9 (Complete Backend Integration and Testing).
