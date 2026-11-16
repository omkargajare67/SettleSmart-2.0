# SettleSmart 2.0 — 10-Step Development Roadmap

## 1. Roadmap Overview and Execution Philosophy

SettleSmart 2.0 follows a strict sequential development process across 10 disciplined milestones. Each milestone builds strictly upon the verified foundation of preceding steps.

### Mandatory 5-Phase Cycle for Every Step
Every technical development stage (Steps 2 through 10) must adhere to this five-phase execution protocol:

```
+-------------------------------------------------------------+
| PHASE 1: Implement functionality for specific dummy example |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| PHASE 2: Test those examples and verify expected output     |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| PHASE 3: Identify and fix errors / edge cases               |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| PHASE 4: Generalize implementation for larger inputs        |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
| PHASE 5: Update PROJECT_GUIDE.md documentation              |
+-------------------------------------------------------------+
```

---

## 2. Milestone Breakdown

### STEP 1 — Project Foundation and Architecture (COMPLETED & APPROVED)
- **Status**: Completed.
- **Key Deliverables**: Complete multi-tier architecture, data modeling, lifecycle flow, gap tolerance, idempotency design, and documentation.

### STEP 2 — MySQL Database Integration & Configuration Loader (COMPLETED)
- **Status**: Completed and Verified.
- **Key Deliverables**:
  - Environment inspection identifying 64-bit MySQL and 64-bit MinGW-w64 toolchain compatibility.
  - Configuration loader (`env_loader.c`, `env_loader.h`) with unit test (`env_loader_test.c`).
  - Database client module (`db_client.c`, `db_client.h`) with connection test (`db_connection_test.c`).
  - Isolated dummy-table verification (`db_dummy_table_test.c`) verifying CRUD, AUTO_INCREMENT, transactions (commit/rollback), and exact `DECIMAL(15, 2)` precision.
  - Physical database schema (`database/schema.sql`) with composite foreign keys, idempotency keys, and constraints.
  - Database integrity test suite (`db_integrity_test.c`) passing all 7 integrity and isolation checks.
  - Automated cross-platform build configuration (`backend/Makefile`).
- **Dependencies**: Step 1 architecture.

### STEP 3 — Authentication and Organization Management (COMPLETED & VERIFIED)
- **Status**: Completed and Verified.
- **Key Deliverables**:
  - Cryptographic utilities (`crypto_utils.c`, `crypto_utils.h`) using OS-level CSPRNG, SHA-256 digests, and constant-time comparison.
  - Password security module (`password.c`, `password.h`) implementing Argon2id (`t=2, m=65536, p=1`), 32-byte salts, 12-128 length checks.
  - Role-based authorization module (`authorization.c`, `authorization.h`) with Last Active Administrator protection.
  - Session lifecycle management (`session.c`, `session.h`) with 12h lifetime, 30m inactivity timeout, activity touch, and token hash storage.
  - Authentication service (`auth_service.c`, `auth_service.h`) with atomic registration, generic login errors, rate limiting, and user CRUD.
  - REST HTTP service (`http_server.c`, `http_server.h`) supporting CORS, secure cookies (`HttpOnly`, `SameSite=Lax`), and CSRF verification.
  - Database migration `database/migrations/003_authentication.sql` creating `sessions` table.
  - 6 independent test suites (`auth_password_test`, `auth_registration_test`, `auth_login_test`, `auth_session_test`, `auth_authorization_test`, `auth_csrf_test`) covering Groups A through I.
- **Dependencies**: Step 2 (Database connection & config loader).

### STEP 4 — Entity Management (COMPLETED AND VERIFIED)
- **Objective**: Manage financial participants (departments, vendors, subsidiaries, clients, partners) with automatic database AUTO_INCREMENT ID assignment, organization-scoped duplicate name prevention, role-based access control, search, filter, pagination, and status toggling.
- **Deliverables**:
  - Entity data models and validation helpers (`entity.c`, `entity.h`).
  - Entity service layer (`entity_service.c`, `entity_service.h`) with parameterized SQL queries and MySQL error code mapping.
  - HTTP routing and request handlers (`entity_routes.c`, `entity_routes.h`, integrated into `http_server.c`).
  - Endpoints: `POST /api/entities`, `GET /api/entities`, `GET /api/entities/:id`, `PATCH /api/entities/:id`, `PATCH /api/entities/:id/status`.
  - Database migration `004_entity_management.sql` updating status to `Active`/`Inactive`, adding audit columns (`created_by_user_id`, `updated_by_user_id`, `updated_at`), and composite foreign keys to `users`.
  - 5 comprehensive test suites (`entity_validation_test`, `entity_crud_test`, `entity_pagination_test`, `entity_concurrency_test`, `entity_http_test`) passing 132 checks.
  - Comprehensive documentation in `docs/ENTITY_MANAGEMENT.md`.
- **Dependencies**: Step 3 (Auth & Multi-tenancy).

### STEP 5 — Transaction Management & Idempotency (COMPLETED AND VERIFIED)
- **Objective**: Implement robust transaction recording, idempotency key verification, exact `int64_t` paise arithmetic, audit trails, server-generated timestamps, deadlock-free row locking, and compensatory reversals.
- **Deliverables**:
  - Migration `database/migrations/005_transaction_management.sql` (updated status enum with `Pending`, single-reversal unique index `uq_trans_reversal_single`, and performance indexes).
  - Transaction data structures and validation functions (`transaction.c`, `transaction.h`).
  - Transaction service layer (`transaction_service.c`, `transaction_service.h`) with idempotency check/replay, ascending entity row locking, dynamic history search, and compensatory reversals.
  - HTTP routes (`transaction_routes.c`, `transaction_routes.h`, integrated into `http_server.c`) covering: `POST /api/transactions`, `GET /api/transactions`, `GET /api/transactions/recent`, `GET /api/transactions/:id`, `POST /api/transactions/:id/reversal`, and immutability rejection (`405 Method Not Allowed` on `PATCH`/`DELETE`).
  - 7 test suites (`transaction_validation_test`, `transaction_crud_test`, `transaction_idempotency_test`, `transaction_concurrency_test`, `transaction_history_test`, `transaction_reversal_test`, `transaction_http_test`) passing 183 checks.
  - Comprehensive documentation in `docs/TRANSACTION_MANAGEMENT.md`.
- **Dependencies**: Step 4 (Entity management).

### STEP 6 — Core DSA Implementation (COMPLETED AND VERIFIED)
- **Objective**: Build clean, modular C implementations of foundational data structures scoped for active transaction management, with organization-scoped multi-tenant state and MySQL restart recovery.
- **Deliverables**:
  - **Singly Linked List** (`linked_list.c`, `linked_list.h`): Bounded chronological transaction ledger with $O(1)$ head insertion and oldest-tail capacity eviction.
  - **HashMap** (`hashmap.c`, `hashmap.h`): Real-time entity balance tracking in exact `int64_t` paise using separate chaining, deterministic modulo hashing, and overflow/underflow protection.
  - **FIFO Queue** (`queue.c`, `queue.h`): Chronological submission order for pending transactions with duplicate ID rejection.
  - **Stack (LIFO)** (`stack.c`, `stack.h`): Reverse chronological tracking of completed transactions with bounded capacity and non-destructive pop inspection.
  - **Priority Queue (Max-Heap)** (`priority_queue.c`, `priority_queue.h`): Dynamic heap array ordering pending transactions by financial value (`amount_paise DESC`) with multi-level tie-breaking (`created_at ASC`, `id ASC`).
  - **Organization-Scoped State** (`dsa_state.c`, `dsa_state.h`): Multi-tenant isolation and persistent MySQL state recovery.
  - 8 comprehensive test suites (`dsa_linked_list_test`, `dsa_hashmap_test`, `dsa_queue_test`, `dsa_stack_test`, `dsa_priority_queue_test`, `dsa_state_recovery_test`, `dsa_multi_org_test`, `dsa_transaction_sync_test`) passing 224 checks.
  - Benchmarked 10,000 transaction synchronization processed in **23.00 ms**.
  - Detailed documentation in `docs/DSA_IMPLEMENTATION.md`.
- **Dependencies**: Step 5 (Transaction Management & Idempotency).

### STEP 7 — Directed Weighted Graph and Real-Time Communication (COMPLETED AND VERIFIED)
- **Objective**: Implement in-memory directed weighted graph in C over a bounded working set, with on-demand historical MySQL queries and real-time WebSocket event dispatching.
- **Deliverables**:
  - Directed Weighted Graph module in C (`graph.c`, `graph.h`): Adjacency list representation, parallel edge preservation, status updates, FIFO cache eviction, and automatic pruning of isolated nodes.
  - Transactional Outbox Pattern (`event_dispatcher.c`, `event_dispatcher.h`): Atomic in-transaction event persistence to `event_outbox` with sequential monotonic ordering (`event_sequence_counter`) locked via `SELECT FOR UPDATE`.
  - Database recovery & snapshot API (`graph_service.c`, `graph_service.h`, `graph_routes.c`, `graph_routes.h`): `GET /api/graph` supporting limit/page pagination, entity filtering, and watermark tracking.
  - Real-time WebSocket streaming (`websocket.c`, `websocket.h`): Live event distribution over `/ws/graph` authenticated via HttpOnly cookie and CORS origin allowlist, with tenant connection isolation and `REPLAY <seq>` reconnection protocol.
  - Database migration `database/migrations/006_graph_realtime.sql` adding `event_sequence_counter` to `organizations` and creating `event_outbox` table.
  - 11 comprehensive test suites (`graph_structure_test`, `graph_edge_test`, `graph_cache_test`, `graph_recovery_test`, `graph_multi_org_test`, `graph_http_test`, `websocket_auth_test`, `websocket_event_test`, `websocket_isolation_test`, `websocket_recovery_test`, `websocket_concurrency_test`) passing 168 checks.
  - Comprehensive documentation in `docs/GRAPH_IMPLEMENTATION.md` and `docs/REALTIME_COMMUNICATION.md`.
- **Dependencies**: Step 6 (Core DSA) and Step 5 (Transaction Management).

### STEP 8 — Transaction Engine and Algorithms (AWAITING USER AUTHORIZATION)
- **Objective**: Integrate sorting, searching, and heuristic settlement algorithms into the transaction pipeline.
- **Deliverables**:
  - **Bubble Sort**: Algorithmic sorting demonstration on bounded in-memory transaction arrays.
  - **Binary Search**: Fast O(log n) lookup on contiguous sorted entity/transaction buffers.
  - **Greedy Settlement Algorithm**: Heuristic bilateral debt minimization generating actionable settlement plans (without unproven optimality claims).
  - Endpoints to generate and execute settlement plans.
- **Dependencies**: Step 7 (Graph & DSA engine).

### STEP 9 — Complete Backend Integration and Stress Testing
- **Objective**: Unify all C backend modules, verify composite tenant isolation, test network idempotency, perform error handling, and test high transaction volumes.
- **Deliverables**:
  - Full end-to-end integration of HTTP routing, database queries, DSA state, and WebSockets.
  - Stress testing with simulated high-volume dummy transaction batches.
  - Security audit verifying tenant isolation and role restrictions.
  - Finalized Makefile/CMake build system.
- **Dependencies**: Steps 2 through 8.

### STEP 10 — Frontend and Final System Integration
- **Objective**: Develop the modern React frontend, live animated directed graph, dashboards, and settlement interface.
- **Deliverables**:
  - React application with clean, responsive UI.
  - Authentication views (Login, Org switcher).
  - Entity and transaction management dashboards with search/filtering.
  - Live animated Directed Weighted Graph using WebSockets (Canvas/SVG).
  - Settlement planning interface showing debt reduction recommendations.
  - End-to-end validation across both Mode A (Local) and Mode B (Cloud).
- **Dependencies**: Step 9 (Complete C backend API).

---

## 3. Strict Boundary Rule

Development must never advance to a subsequent step without explicit authorization from the project lead. Skipping steps or introducing premature code violates the project guidelines.
