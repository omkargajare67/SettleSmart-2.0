# SettleSmart 2.0 — System Architecture

## 1. Executive Summary

SettleSmart 2.0 is an enterprise-grade financial transaction management and settlement platform. It replaces informal, personal expense-splitting tools with an auditable, multi-entity settlement system designed for businesses, subsidiaries, departments, and vendor ecosystems.

The platform is designed around three foundational engineering pillars:
1. **Authoritative Persistence & Integrity**: MySQL (8.4 LTS / 9.7 LTS) functions as the single authoritative source of truth for all transactions, users, entities, and organizations. The authoritative transaction sequence and balance consistency are determined strictly by database commit order.
2. **High-Efficiency C Backend**: Core processing, API routing, real-time event dissemination, and academic data structures are engineered directly in C for maximum operational efficiency, transparent hardware utilization, and predictable memory bounds.
3. **Decoupled Interactive Visual Tier**: A lightweight, modern React frontend (to be introduced in Step 10) connects to the C backend via standard HTTP REST endpoints and persistent WebSocket streams.

---

## 2. Multi-Tier System Topology

```
+------------------------------------------------------------------------+
|                               USER TIER                                |
|   Local Developer / Classroom User       Centralized Enterprise User   |
|   (Browser on localhost:3000)            (Browser via Public URL)      |
+------------------------------------------------------------------------+
                                    |
                            HTTP / WebSockets
                                    |
                                    v
+------------------------------------------------------------------------+
|                         APPLICATION TIER (C BACKEND)                   |
|                                                                        |
|  +---------------------+   +---------------------+   +---------------+ |
|  |  HTTP REST Server   |   |  WebSocket Hub      |   | Auth Guard    | |
|  |  (Routing, Parsing) |   |  (Live Events)      |   | (Argon2id/    | |
|  |  + Idempotency Check|   |                     |   |  Sessions)    | |
|  +---------------------+   +---------------------+   +---------------+ |
|  |  Config Loader (.env / System Environment Variable Reader)         | |
|  +-------------------------------------------------------------------+ |
|                                   |                                    |
|  +------------------------------------------------------------------+  |
|  |        In-Memory DSA Subsystem (Windowed / Bounded Working Set)  |  |
|  |  - Directed Weighted Graph (Active window / filtered topology)   |  |
|  |  - HashMap (Active entity net balances with int64_t precision)   |  |
|  |  - Linked List (Chronological session transaction history)       |  |
|  |  - FIFO Queue (Ingestion buffer for incoming network requests)   |  |
|  |  - Stack (Recent activity display; NOT for financial rollback)   |  |
|  |  - Priority Queue / Max-Heap (Priority & high-value scheduling)  |  |
|  |  - Bubble Sort & Binary Search (Bounded In-Memory Algorithms)    |  |
|  |  - Greedy Settlement Algorithm (Heuristic debt recommendation)   |  |
|  +------------------------------------------------------------------+  |
+------------------------------------------------------------------------+
                                    |
                         MySQL C API (TCP Socket / TLS)
                                    |
                                    v
+------------------------------------------------------------------------+
|                           DATABASE TIER (MySQL)                        |
|                                                                        |
|  - Relational Schema with Foreign Keys & ACID Guarantees              |
|  - Supported LTS: MySQL 8.4 LTS & MySQL 9.7 LTS                        |
|  - Authoritative Commit Ordering & Gapped AUTO_INCREMENT Primary Keys  |
|  - Comprehensive Composite Foreign Keys (Entities, Users, Reversals)  |
|  - Exact DECIMAL(15, 2) Precision Storage (No Floating-Point Math)    |
|  - Idempotency Ledger to Prevent Network Retries Duplicate Creation    |
|  - Immutable Transaction Ledger & UTC Timestamps                       |
+------------------------------------------------------------------------+
```

---

## 3. Multi-Organization Architecture (Tenancy & Isolation)

SettleSmart 2.0 is fundamentally a multi-tenant platform. Multiple organizations exist independently within the same database instance.

### 3.1 Strict Tenant Isolation Rules
1. **Verified Server-Side Context**: The backend extracts `organization_id` strictly from the cryptographically validated session context. The backend **never** trusts client-supplied `organization_id` fields in request bodies or query parameters.
2. **Comprehensive Composite Foreign Key Enforcement**: To ensure isolation is guaranteed at the database engine level, entities, users, and transactions maintain composite unique identifiers. Foreign keys mandate matching `organization_id` across all relational vectors:
   - Senders: `FOREIGN KEY (organization_id, sender_entity_id) REFERENCES entities(organization_id, id)`
   - Receivers: `FOREIGN KEY (organization_id, receiver_entity_id) REFERENCES entities(organization_id, id)`
   - Creators: `FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id)`
   - Reversals: `FOREIGN KEY (reversal_of_id, organization_id) REFERENCES transactions(id, organization_id)`
   This physically blocks any SQL transaction from cross-referencing an entity, user, or original transaction belonging to another organization.
3. **Query Scoping**: Every database query executed by the C backend must bind `WHERE organization_id = ?` derived directly from the verified session token.
4. **Cross-Tenant Access Prevention**: An authenticated user of Organization A attempting to request an entity or record belonging to Organization B receives an immediate HTTP 404 Not Found response (identical to a nonexistent record), completely eliminating ID enumeration and tenant resource existence leakage.
5. **Isolated WebSocket Channels**: Real-time WebSocket broadcasts are partitioned by `organization_id`. Connected clients only receive events pertaining to their authenticated organization.

### 3.2 Role-Based Access Control (RBAC)
Within each organization, users are assigned one of three granular roles:

| Role | Permissions & Responsibilities |
|---|---|
| **Admin** | Full administrative authority. Manages organization users, configures settings, creates entities, records transactions, triggers settlement algorithms, and views full audit logs. |
| **Finance Manager** | Operational financial authority. Creates entities, records transactions, issues compensatory reversals, views transaction history, and executes settlement plans. Cannot manage other users. |
| **Viewer** | Read-only authority. Views entities, searches transactions, monitors the live animated graph, and views settlement recommendations. Cannot create or modify records. |

---

## 4. Supported Database Engine Versions

- **Supported LTS Versions**:
  - **MySQL 8.4 LTS** (Long Term Support) — Production baseline.
  - **MySQL 9.7 LTS** (Long Term Support) — Local developer baseline.
- **Policy**:
  - The platform targets verified long-term support releases.
  - SettleSmart 2.0 does not claim blanket support for all non-LTS short-cycle innovation releases without prior testing.
  - Installed development server: **MySQL Community Server 9.7.1 x64**.

---

## 5. In-Memory DSA Subsystem & MySQL State Recovery (Step 6)

The C backend combines high-speed in-memory data structures with persistent MySQL ACID guarantees:

1. **Organization-Scoped Containment (`DsaState`)**:
   Every tenant operates in an independent in-memory state container containing:
   - `LinkedList`: Bounded recent transaction history (500 items).
   - `HashMap`: Separate chaining balance index with exact `int64_t` paise precision.
   - `Queue`: FIFO queue for pending transactions awaiting settlement.
   - `Stack`: Bounded LIFO stack for recently completed transactions (100 items).
   - `PriorityQueue`: Dynamic Max-Heap ordering pending items by financial amount with deterministic tie-breaking.
2. **State Rebuilding and Crash Recovery**:
   The in-memory DSA state is completely transient and reconstructible. If a backend instance restarts or crashes, `dsa_state_recover_from_db()` queries MySQL to restore net entity balances, populate pending queues and heap, and fill the completed stack.
3. **Strict Tenancy Isolation**:
   Cross-tenant operations are strictly rejected at the DSA API level: any attempt to synchronize a transaction into a mismatched `DsaState` returns an immediate error (-1) and preserves memory boundaries.

---

## 6. Directed Weighted Graph & Real-Time Communication Architecture (Step 7)

Step 7 implements the visual and topological core of SettleSmart 2.0:

1. **In-Memory Directed Weighted Graph**:
   - Implemented via Adjacency List (`GraphEdge`, `GraphNode`, `Graph`).
   - Stores exact financial weights in 64-bit integer paise (`int64_t`).
   - Supports parallel edges: multiple transfers between identical entities are preserved as distinct edges.
   - Financial reversals are recorded as distinct opposite-direction edges while updating original edge status to `Reversed`.
   - Bounded cache eviction: enforces configured edge capacities by evicting oldest edges and pruning isolated nodes.
2. **Transactional Outbox Pattern**:
   - Every transaction creation and reversal records an event in MySQL `event_outbox` within the identical database transaction.
   - Sequential monotonic ordering per organization is guaranteed using row locks on `organizations.event_sequence_counter` via `SELECT FOR UPDATE`.
3. **Real-Time WebSocket Streaming (`/ws/graph`)**:
   - Authenticated via HttpOnly session cookies and CORS origin allowlist validation.
   - Multi-tenant connection registry ensures events are broadcast strictly to matching organization connections.
4. **Resilient Reconnection and Replay**:
   - Clients query initial graph state and current watermark via `GET /api/graph`.
   - On reconnect, clients issue `REPLAY <seq>` to stream any missed outbox events gaplessly.

