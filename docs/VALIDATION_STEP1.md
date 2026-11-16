# SettleSmart 2.0 — Step 1 Architecture Validation (Dummy Scenarios)

## Overview

In accordance with Section 14 of the specification and subsequent architectural refinements, the SettleSmart 2.0 system architecture is validated against three core conceptual scenarios before finalizing Step 1.

These are architectural evaluation walkthroughs to verify structural completeness, component responsibilities, exact financial precision, network idempotency, and tenant security boundaries. No executable application code is claimed to have run; this documents how the design satisfies each operational requirement.

---

## Scenario A: Automatic ID Generation, Idempotency, and Transaction Flow

### 1. Scenario Description
An organization creates three entities: **Entity A**, **Entity B**, and **Entity C**.
The system automatically assigns unique entity IDs.
Subsequently, two sequential transactions occur:
- **Transaction 1**: Entity A transfers ₹500.00 to Entity B with `idempotency_key = "idemp-001"`.
- **Transaction 2**: Entity B transfers ₹300.00 to Entity C with `idempotency_key = "idemp-002"`.
- **Retry Test**: Client resends Transaction 1 with `idempotency_key = "idemp-001"` after a simulated network timeout.

### 2. Expected Conceptual Behavior
- Three entity records are created in MySQL with automatic, monotonic primary keys (e.g., ID 1 for A, ID 2 for B, ID 3 for C) and composite keys `(organization_id, id)`.
- Transaction 1 is submitted without an ID; MySQL generates `id = 101`, records amount as exact `500.00` (`DECIMAL(15, 2)` / `50000` paise in C `int64_t`), assigns a UTC timestamp, stores `idempotency_key = "idemp-001"`, and commits.
- Transaction 2 is submitted without an ID; MySQL generates `id = 102`, records amount as exact `300.00`, and commits.
- **Idempotency Verification**: When the retry of Transaction 1 arrives with `idemp-001`, the backend detects the existing unique key `UNIQUE(organization_id, idempotency_key)`, aborts duplicate creation, and returns the existing transaction (ID 101) with `200 OK`.
- **Authoritative Ordering**: The transaction sequence is finalized by the MySQL commit order, not by the in-memory FIFO ingress queue.
- **Tolerance for Gaps**: If an intermediate transaction aborted, `id = 102` remains valid even if a gap occurred.
- The in-memory Directed Weighted Graph registers directed edges over the active window:
  - $A \xrightarrow{₹500.00} B$
  - $B \xrightarrow{₹300.00} C$
- Net balances computed by the HashMap using `int64_t` paise evaluate to:
  - **Entity A**: $-₹500.00$ (Sent 500.00, Received 0.00)
  - **Entity B**: $+₹200.00$ (Sent 300.00, Received 500.00 $\rightarrow$ $+200.00$)
  - **Entity C**: $+₹300.00$ (Sent 0.00, Received 300.00 $\rightarrow$ $+300.00$)
  - Total organizational net balance sum: $-500.00 + 200.00 + 300.00 = 0.00$ (Exact balance conservation, no floating-point rounding errors).
- **Stack Role**: The transactions are pushed onto the in-memory Stack for recent activity display. Popping the stack does not execute a balance rollback; reversals require an explicit compensatory transaction record.

### 3. Architecture Components Involved
- **HTTP REST Router (C Backend)**: Receives requests and passes idempotency keys.
- **Input Validation Module**: Enforces $amount > 0.00$, validates exact decimal format, and rejects floating-point representations.
- **Database Engine (MySQL)**: Enforces `UNIQUE(organization_id, idempotency_key)`, allocates monotonic `AUTO_INCREMENT` IDs, verifies composite foreign keys, and serializes authoritative commit ordering.
- **In-Memory HashMap**: Tracks balance changes in $O(1)$ using 64-bit integer arithmetic.
- **In-Memory Directed Weighted Graph (Active Window)**: Inserts directed edges and preserves transaction orientation ($A \rightarrow B \rightarrow C$).
- **WebSocket Broadcast Hub**: Emits real-time `TRANSACTION_CREATED` notifications strictly post-commit.

### 4. Verification & Requirement Satisfaction
- **Requirement Satisfied**: **YES**.
- Duplicate funds are prevented by the idempotency key.
- Floating point inaccuracies are eliminated.
- Database commit order guarantees authoritative consistency.

---

## Scenario B: Multi-Organization Isolation & Composite Foreign Keys

### 1. Scenario Description
Two distinct corporate tenants onboard onto the platform:
- **Organization A** (e.g., Logistics Fleet Corp).
- **Organization B** (e.g., Retail Distribution Ltd).

Both organizations have registered users (using **Argon2id** password hashing), independent entities, and transactions.

### 2. Expected Conceptual Behavior
- A user authenticated to Organization A must **never** be able to:
  - View, list, or search Organization B's entities.
  - View, list, or search Organization B's transactions or balances.
  - Create a transaction involving an entity from Organization B.
  - Receive WebSocket events originating from Organization B.
- **Structural Database Enforcement**:
  - If a corrupted or malicious request from Organization A references `sender_entity_id = 99` (which belongs to Organization B), the query fails not only at backend validation, but also at the database level:
    `FOREIGN KEY (organization_id, sender_entity_id) REFERENCES entities(organization_id, id)`
    Since `organization_id` is locked to Organization A from the server session, MySQL triggers a foreign key constraint violation.

### 3. Architecture Components Involved
- **Session Authentication Guard (C Backend)**: Extracts authenticated `organization_id` strictly from server session tokens; ignores any client-supplied organization ID.
- **Password Subsystem**: Validates credentials via standard Argon2id / bcrypt verification (reading embedded salt directly from hash string).
- **Relational Data Layer (MySQL)**: Composite foreign keys enforce tenant boundaries at the database engine level.
- **WebSocket Connection Manager**: Channels client socket connections into organization-specific broadcast rooms.

### 4. Verification & Requirement Satisfaction
- **Requirement Satisfied**: **YES**.
- Isolation is verified at both application and database engine layers.

---

## 3. Scenario C: Dual Operating Modes & Explicit Config Loading

### 1. Scenario Description
Verify that the exact same SettleSmart 2.0 codebase functions properly in both operational environments:
- **Mode A (Local Installation)**: A student or developer clones the repository, sets up a local MySQL database, configures credentials in `.env`, compiles the C backend, and runs the application.
- **Mode B (Online Cloud Deployment)**: The system is deployed to a cloud server with a managed cloud MySQL database, serving remote users over public HTTPS.

### 2. Expected Conceptual Behavior
- **Configuration Loading in C**:
  - The backend uses an explicit `env_loader` module.
  - In Mode A: `env_loader` reads `.env` line by line, parses key-value pairs, and sets process environment variables.
  - In Mode B: `env_loader` reads existing system environment variables injected by cloud container engines (Docker/Kubernetes).
- **Zero Hardcoded Credentials**: No database passwords or secrets exist in the repository source code.
- **ID Independence & Gap Handling**:
  - In Mode A, local auto-increment IDs start from 1 for the local user.
  - In Mode B, all cloud users share the centralized cloud database sequence.
  - Both modes gracefully handle gaps in `AUTO_INCREMENT` IDs.
- **Client Independence in Cloud Mode**: Remote end-users access SettleSmart via a web browser without needing GCC, MySQL, or local development environments installed.

### 3. Architecture Components Involved
- **`env_loader` Module in C**: Dynamically reads configuration from `.env` or system environment.
- **Build System (Makefile)**: Standardized compilation targeting C11.
- **MySQL C Client**: Connects via TCP/IP sockets to either `127.0.0.1` (local) or a remote cloud endpoint (cloud).
- **React Frontend (Step 10)**: Connects to relative `/api` paths or configurable backend URLs.

### 4. Verification & Requirement Satisfaction
- **Requirement Satisfied**: **YES**.
- C configuration loading is explicitly architected and validated.
