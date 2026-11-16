# SettleSmart 2.0 — Conceptual Database Design

## 1. Design Overview and Principles

This document defines the data model for SettleSmart 2.0. The schema is designed for MySQL 8.4 LTS and MySQL 9.7 LTS, enforcing relational integrity, multi-tenant isolation, exact financial precision, and auditability.

### Fundamental Database Principles
1. **Single Authoritative Source of Truth**: All operational records, user authentications, entity directories, and transaction histories reside permanently in MySQL. In-memory data structures are secondary operational caches over active datasets.
2. **Deterministic Automatic ID Generation with Gap Tolerance**: Primary keys for all entities and transactions are generated exclusively by the database engine using `AUTO_INCREMENT`. Users and client applications never assign or submit custom primary IDs. The system treats IDs as unique identifiers and explicitly does not assume gapless sequence continuity.
3. **Comprehensive Composite Foreign Key Isolation**: Every entity, user, and transaction belongs to an organization. In addition to application-level query scoping, composite foreign keys enforce organization isolation at the database engine level across:
   - Senders: `(organization_id, sender_entity_id) -> entities(organization_id, id)`
   - Receivers: `(organization_id, receiver_entity_id) -> entities(organization_id, id)`
   - Creators: `(organization_id, created_by_user_id) -> users(organization_id, id)`
   - Reversals: `(reversal_of_id, organization_id) -> transactions(id, organization_id)`
4. **Exact Financial Precision**: All monetary values are stored strictly as exact fixed-point `DECIMAL(15, 2)` representations. Ordinary binary floating-point types (`FLOAT`, `DOUBLE`) are strictly forbidden.
5. **Network Retry Idempotency**: Transactions incorporate an `idempotency_key` unique per organization to guarantee that network drops and client retries never cause duplicate financial records.
6. **Immutability of Financial Records**: Once a financial transaction is committed, it is never mutated or deleted. Corrections must take the form of auditable compensatory reversal transactions.
7. **Universal UTC Timestamps**: All creation and modification timestamps are persisted in UTC. Timezone conversions occur strictly at the presentation layer.

---

## 2. Supported Database Engine Versions

- **Primary Supported Versions**:
  - **MySQL 8.4 LTS** (Long Term Support) — Production baseline.
  - **MySQL 9.7 LTS** (Long Term Support) — Current local development baseline.
- **Support Boundary**:
  - SettleSmart 2.0 does not claim blanket support for all non-LTS intermediate innovation releases without explicit prior compatibility verification.
  - The installed database server version on the development workstation is **MySQL Community Server 9.7.1 x64**.

---

## 3. Conceptual Entities and Attributes

### 3.1 Entity: `organizations`
Represents an independent business, corporate entity, or institution utilizing the platform.

- **Primary Key**: `id` (BigInt, Auto-Increment)
- **Attributes**:
  - `name` (String, Non-Null, Unique across platform): The legal or organizational name.
  - `status` (Enum: `Active`, `Suspended`): Operational status of the tenant.
  - `event_sequence_counter` (BigInt, Non-Null, Default 0): Monotonic sequence counter locked during transaction events to guarantee gapless ordering.
  - `created_at` (UTC Timestamp, Default Current Time)
  - `updated_at` (UTC Timestamp, Auto-Updated)

### 3.2 Entity: `users`
Represents individuals authenticated to access an organization's workspace.

- **Primary Key**: `id` (BigInt, Auto-Increment)
- **Foreign Key**: `organization_id` -> `organizations(id)` (Non-Null, Restricted Delete)
- **Composite Unique Keys**:
  - `UNIQUE KEY uq_users_org_id (organization_id, id)`: Enables composite foreign key enforcement from `transactions` and `sessions`.
  - `UNIQUE KEY uq_users_org_email (organization_id, email)`: Prevents duplicate user accounts within an organization.
- **Attributes**:
  - `email` (String, Non-Null)
  - `password_hash` (String, Length 255, Non-Null): Standardized hash generated via **Argon2id** (`t=2, m=65536, p=1`).
  - `role` (Enum: `Admin`, `Finance_Manager`, `Viewer`): Access control level.
  - `status` (Enum: `Active`, `Deactivated`)
  - `created_at` (UTC Timestamp)

### 3.3 Entity: `sessions`
Represents authenticated user sessions. Stores only SHA-256 token hashes rather than raw session or CSRF secrets.

- **Primary Key**: `id` (BigInt, Auto-Increment)
- **Composite Foreign Key**:
  - `FOREIGN KEY (organization_id, user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT`
- **Attributes**:
  - `organization_id` (BigInt, Non-Null)
  - `user_id` (BigInt, Non-Null)
  - `token_hash` (VARCHAR(64), Non-Null, Unique): SHA-256 hash of raw session token.
  - `csrf_token_hash` (VARCHAR(64), Non-Null): SHA-256 hash of raw CSRF token.
  - `created_at` (UTC Timestamp, Default Current Time)
  - `expires_at` (UTC Timestamp, Non-Null, Absolute 12-hour expiration)
  - `last_seen_at` (UTC Timestamp, Updated on every request, 30-minute sliding window)
  - `revoked_at` (UTC Timestamp, Nullable, Set on logout or password change)
- **Indexes**:
  - `INDEX idx_sessions_org_user (organization_id, user_id)`
  - `INDEX idx_sessions_expires (expires_at)`

### 3.4 Entity: `entities`
Represents participants in financial transactions (departments, internal subsidiaries, vendors, clients, or business partners).

- **Primary Key**: `id` (BigInt, Auto-Increment)
- **Foreign Key**: `organization_id` -> `organizations(id)` (Non-Null, Restricted Delete)
- **Composite Unique Keys**:
  - `UNIQUE KEY uq_entities_org_id (organization_id, id)`: Enables composite foreign key enforcement from downstream tables.
  - `UNIQUE KEY uq_entities_org_name (organization_id, name)`: Prevents duplicate entity names within an organization.
- **Composite Foreign Keys**:
  - `FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT`
  - `FOREIGN KEY (organization_id, updated_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT`
- **Attributes**:
  - `name` (VARCHAR(150), Non-Null): Name of the entity.
  - `entity_type` (Enum: `Department`, `Vendor`, `Subsidiary`, `Client`, `Partner`)
  - `status` (Enum: `Active`, `Inactive`, Default `Active`)
  - `created_by_user_id` (BigInt, Nullable): User who created the entity.
  - `updated_by_user_id` (BigInt, Nullable): User who last updated the entity.
  - `created_at` (UTC Timestamp, Default Current Time)
  - `updated_at` (UTC Timestamp, Auto-Updated)

### 3.4 Entity: `transactions`
Represents an authoritative financial movement between two entities within an organization.

- **Primary Key**: `id` (BigInt, Auto-Increment)
- **Foreign Key**: `organization_id` -> `organizations(id)` (Non-Null)
- **Composite Unique Keys**:
  - `UNIQUE KEY uq_trans_org_id (organization_id, id)`
  - `UNIQUE KEY uq_trans_id_org (id, organization_id)`: Enables self-referential composite foreign key validation.
  - `UNIQUE KEY uq_trans_org_idempotency (organization_id, idempotency_key)`: Guards against duplicate transaction execution on network retries.
- **Composite Foreign Keys (Structural Isolation)**:
  - `FOREIGN KEY (organization_id, sender_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT`
  - `FOREIGN KEY (organization_id, receiver_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT`
  - `FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT`
  - `FOREIGN KEY (reversal_of_id, organization_id) REFERENCES transactions(id, organization_id) ON DELETE RESTRICT`
- **Attributes**:
  - `idempotency_key` (String, UUID v4, Non-Null): Client-provided unique request key.
  - `request_fingerprint` (String, Char 64, Non-Null): SHA-256 hash of the original request payload.
  - `amount` (Decimal, `DECIMAL(15, 2)`, Non-Null, Strict > 0.00): Exact monetary value.
  - `currency` (String, 3-character ISO code, Default `INR`).
  - `status` (Enum: `Completed`, `Reversed`, `Disputed`).
  - `transaction_type` (Enum: `Standard`, `Reversal`, `Settlement_Adjustment`).
  - `reversal_of_id` (BigInt, Nullable): References original transaction.
  - `created_by_user_id` (BigInt, Nullable): References creator user.
  - `description` (String, Optional).
  - `created_at` (UTC Timestamp, Server-Generated).

### 3.6 Entity: `event_outbox`
Implements the Transactional Outbox Pattern to record real-time graph events atomically with financial transactions.

- **Primary Key**: `id` (BigInt, Auto-Increment)
- **Composite Foreign Keys**:
  - `FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT`
  - `FOREIGN KEY (organization_id, transaction_id) REFERENCES transactions(organization_id, id) ON DELETE RESTRICT`
- **Composite Unique Keys**:
  - `UNIQUE KEY uq_outbox_org_seq (organization_id, event_sequence)`: Enforces gapless monotonic sequence ordering per organization.
- **Attributes**:
  - `organization_id` (BigInt, Non-Null)
  - `event_sequence` (BigInt, Non-Null): Strictly incrementing counter per tenant.
  - `event_type` (Enum: `TRANSACTION_CREATED`, `TRANSACTION_REVERSED`)
  - `transaction_id` (BigInt, Non-Null)
  - `payload` (JSON, Non-Null): Full JSON snapshot of the event broadcast.
  - `created_at` (UTC Timestamp, Default Current Time)
- **Indexes**:
  - `INDEX idx_outbox_org_seq (organization_id, event_sequence)`

---

## 4. Entity-Relationship Overview

```
+-------------------------------------------------------------+
|                        organizations                        |
|   id (PK)                                                   |
+-------------------------------------------------------------+
   |                        |
   | 1:N                    | 1:N
   v                        v
+-----------------------+ +-----------------------------------+
|         users         | |             entities              |
|  id (PK)              | |  id (PK)                          |
|  organization_id (FK) | |  organization_id (FK)             |
|  UNIQUE (org_id, id)  | |  UNIQUE (org_id, id)              |
|  UNIQUE (org_id, email)| |  UNIQUE (org_id, name)            |
+-----------------------+ +-----------------------------------+
        |                    |             |
        | (org, user_id)     | (org, sender)  | (org, receiver)
        | Composite FK       | Composite FK| Composite FK
        v                    v             v
+-------------------------------------------------------------+
|                        transactions                         |
|  id (PK, AUTO_INCREMENT - Gaps Possible)                    |
|  organization_id (FK)                                       |
|  idempotency_key                                            |
|  request_fingerprint                                        |
|  UNIQUE (org_id, id)                                        |
|  UNIQUE (id, org_id)                                        |
|  UNIQUE (org_id, idempotency_key)                           |
|  sender_entity_id (Composite FK to entities)                |
|  receiver_entity_id (Composite FK to entities)              |
|  created_by_user_id (Composite FK to users)                 |
|  reversal_of_id (Composite Self-Ref FK to transactions)     |
|  amount (DECIMAL(15, 2) - No Floating Point)                |
|  status (Completed | Reversed | Disputed)                   |
|  created_at (UTC Timestamp)                                 |
+-------------------------------------------------------------+
```

---

## 5. Automatic ID Generation Architecture & Gap Tolerance

- **No Manual ID Injection**: Users provide human-meaningful names and numeric amounts. The client payload never contains an ID for new records.
- **Database Monotonicity**: Primary keys rely on MySQL `AUTO_INCREMENT`.
- **Handling AUTO_INCREMENT Gaps**: Gaps occur naturally when transactions roll back, insert statements fail validation, deadlocks abort, or server restarts occur. The application logic must never assume IDs are gapless sequential numbers.

---

## 6. Network Retry Idempotency Strategy

1. **Client Responsibility**: The client generates a unique `idempotency_key` (UUID v4) and computes a `request_fingerprint` (SHA-256) for every distinct transaction action.
2. **Database Enforcement**: The `transactions` table enforces `UNIQUE(organization_id, idempotency_key)`.
3. **Backend Idempotency Pipeline**:
   - If `(organization_id, idempotency_key)` exists in MySQL:
     - If `request_fingerprint` matches: Returns the original committed transaction (`HTTP 200 OK`).
     - If `request_fingerprint` differs: Rejects with `409 Conflict` (idempotency key reused with different payload).
   - If key does not exist: Proceeds with atomic insertion (`HTTP 201 Created`).
