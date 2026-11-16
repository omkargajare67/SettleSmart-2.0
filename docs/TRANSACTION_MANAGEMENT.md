# SettleSmart 2.0 — Transaction Management and Idempotency Architecture

This document specifies the technical design, database persistence, idempotency mechanics, concurrency control, and API contracts for the core financial transaction subsystem in SettleSmart 2.0.

---

## 1. Architectural Principles

1. **Authoritative Database Source of Truth**: All financial transaction records originate and reside permanently in MySQL InnoDB tables with strict foreign keys, composite organization tenant scoping, and ACID guarantees.
2. **Strict Financial Precision**: Financial amounts are stored in MySQL as `DECIMAL(15,2)` and parsed/manipulated in C exclusively as 64-bit integer paise (`int64_t`). No floating-point variables (`float`, `double`) are ever used for monetary quantities or calculations.
3. **Idempotent Ingestion**: Every state-changing transaction creation and reversal endpoint requires a client-provided `Idempotency-Key` HTTP header (UUID v4 format). A canonical SHA-256 fingerprint of the request payload (`org_id:sender_id:receiver_id:amount:currency:type`) is stored alongside the key. Duplicate submissions with identical keys and payloads return the original transaction with `200 OK` and `"idempotent_replay": true`. Reusing an idempotency key with modified parameters is rejected with `409 Conflict`.
4. **Permanent Immutability**: Financial records are strictly immutable. Standard HTTP `PATCH`, `PUT`, and `DELETE` methods against `/api/transactions` and `/api/transactions/:id` return `405 Method Not Allowed`. Corrections or compensatory entries are performed solely via explicit reversals (`POST /api/transactions/:id/reversal`).
5. **Multi-Tenant Isolation**: Every database query scopes transactions by `organization_id`. Cross-organization transaction lookups return standard `404 Not Found` responses to mask resource existence across tenants.

---

## 2. MySQL Schema Enhancements

### 2.1 Table Structure: `transactions`

```sql
CREATE TABLE IF NOT EXISTS transactions (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    sender_entity_id BIGINT NOT NULL,
    receiver_entity_id BIGINT NOT NULL,
    amount DECIMAL(15,2) NOT NULL,
    currency VARCHAR(3) NOT NULL DEFAULT 'INR',
    status ENUM('Pending', 'Completed', 'Reversed', 'Disputed') NOT NULL DEFAULT 'Pending',
    transaction_type ENUM('Standard', 'Reversal', 'SettlementAdjustment') NOT NULL DEFAULT 'Standard',
    idempotency_key VARCHAR(64) NOT NULL,
    request_fingerprint CHAR(64) NOT NULL,
    reversal_of_id BIGINT NULL,
    created_by_user_id BIGINT NULL,
    description VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_trans_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_sender FOREIGN KEY (organization_id, sender_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_receiver FOREIGN KEY (organization_id, receiver_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_creator FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_reversal FOREIGN KEY (organization_id, reversal_of_id) REFERENCES transactions(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT uq_trans_org_idempotency UNIQUE (organization_id, idempotency_key),
    CONSTRAINT uq_trans_reversal_single UNIQUE (organization_id, reversal_of_id),
    INDEX idx_trans_org_created (organization_id, created_at),
    INDEX idx_trans_org_sender (organization_id, sender_entity_id),
    INDEX idx_trans_org_receiver (organization_id, receiver_entity_id),
    INDEX idx_trans_org_status (organization_id, status, created_at),
    INDEX idx_trans_org_amount (organization_id, amount, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```

### 2.2 Key Constraints and Indexes

- `uq_trans_org_idempotency (organization_id, idempotency_key)`: Guarantees uniqueness of idempotency keys within an organization while permitting different organizations to use identical keys independently.
- `uq_trans_reversal_single (organization_id, reversal_of_id)`: Because `reversal_of_id` is nullable, multiple standard transactions may have `reversal_of_id IS NULL`, but a given completed transaction can only be reversed once per organization.
- `idx_trans_org_status (organization_id, status, created_at)`: Accelerates filtered queries by lifecycle status.
- `idx_trans_org_amount (organization_id, amount, created_at)`: Optimizes range-filtered financial queries.

---

## 3. Concurrency and Deadlock Avoidance

When creating or reversing transactions, two participant entity rows (`sender_entity_id` and `receiver_entity_id`) are locked using `SELECT ... FOR UPDATE`.

To prevent distributed deadlocks between concurrent bidirectional transactions (e.g. Entity 10 -> Entity 20 occurring concurrently with Entity 20 -> Entity 10), entity locks are sorted by primary key in ascending order prior to acquisition:

```c
long long first_id = sender_id < receiver_id ? sender_id : receiver_id;
long long second_id = sender_id < receiver_id ? receiver_id : sender_id;

char lock_query[256];
snprintf(lock_query, sizeof(lock_query),
         "SELECT id, status FROM entities WHERE organization_id = %lld AND id IN (%lld, %lld) ORDER BY id FOR UPDATE;",
         organization_id, first_id, second_id);
```

Both entities are verified to belong to the caller's organization and be in `Active` status. If either entity is non-existent, inactive, or belongs to another organization, the transaction is rejected and rolled back.

---

## 4. API Specification

### 4.1 Create Transaction
- **Method**: `POST`
- **Path**: `/api/transactions`
- **Headers**:
  - `Cookie: session_token=<token>`
  - `X-CSRF-Token: <csrf_token>`
  - `Idempotency-Key: <uuid_v4>`
- **Request Body**:
  ```json
  {
    "sender_entity_id": 101,
    "receiver_entity_id": 102,
    "amount": "1500.50",
    "currency": "INR"
  }
  ```
- **Responses**:
  - `201 Created`: First successful submission (`"idempotent_replay": false`).
  - `200 OK`: Idempotent replay of previously processed request (`"idempotent_replay": true`).
  - `400 Bad Request`: Validation failure (inactive entity, self-transaction, invalid amount format, missing/invalid UUID v4 key).
  - `403 Forbidden`: Caller lacks required role (Viewer role) or invalid CSRF token.
  - `409 Conflict`: Idempotency key reused with conflicting request payload.

### 4.2 List and Search Transactions
- **Method**: `GET`
- **Path**: `/api/transactions`
- **Headers**:
  - `Cookie: session_token=<token>`
- **Query Parameters**:
  - `page`: Page number (default: 1)
  - `limit`: Records per page (default: 20, max: 100)
  - `id`: Transaction ID exact search
  - `entity_id`: Filter by participant entity ID
  - `direction`: `"incoming"`, `"outgoing"`, or omitted (both)
  - `from_date`: Inclusive start timestamp
  - `to_date`: Inclusive end timestamp
  - `min_amount`: Inclusive minimum decimal amount
  - `max_amount`: Inclusive maximum decimal amount
  - `status`: `"Pending"`, `"Completed"`, `"Reversed"`, `"Disputed"`
  - `sort_by`: `"id"`, `"amount"`, `"created_at"` (default: `"created_at"`)
  - `order`: `"ASC"`, `"DESC"` (default: `"DESC"`)
- **Response**: `200 OK`
  ```json
  {
    "success": true,
    "data": [
      {
        "id": 1,
        "organization_id": 10,
        "sender_entity_id": 101,
        "receiver_entity_id": 102,
        "amount": "1500.50",
        "currency": "INR",
        "status": "Pending",
        "type": "Standard",
        "idempotency_key": "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11",
        "request_fingerprint": "...",
        "reversal_of_id": null,
        "created_by_user_id": 5,
        "created_at": "2026-11-15 14:00:00"
      }
    ],
    "pagination": {
      "page": 1,
      "limit": 20,
      "total_records": 1,
      "total_pages": 1
    }
  }
  ```

### 4.3 Get Recent Transactions
- **Method**: `GET`
- **Path**: `/api/transactions/recent?limit=20`
- **Response**: `200 OK`
  ```json
  {
    "success": true,
    "data": [ ... ],
    "count": 20
  }
  ```

### 4.4 Get Single Transaction Details
- **Method**: `GET`
- **Path**: `/api/transactions/:id`
- **Response**: `200 OK` or `404 Not Found` (when not found or belonging to another organization).

### 4.5 Reverse Transaction
- **Method**: `POST`
- **Path**: `/api/transactions/:id/reversal`
- **Headers**:
  - `Cookie: session_token=<token>`
  - `X-CSRF-Token: <csrf_token>`
  - `Idempotency-Key: <uuid_v4>`
- **Behavior**:
  - Reversal is permitted only if original transaction status is `Completed`.
  - Reversal transaction swaps sender and receiver entities.
  - Reversal amount matches original amount exactly.
  - New transaction created with `type = Reversal`, `status = Pending`, `reversal_of_id = :id`.
  - Repeat submissions with same key return original reversal with `200 OK` (`"idempotent_replay": true`).
  - Attempting a second reversal with a different key returns `409 Conflict`.

### 4.6 Immutability Rejections
- `PATCH /api/transactions/:id`: Returns `405 Method Not Allowed`.
- `DELETE /api/transactions/:id`: Returns `405 Method Not Allowed`.
- Body: `{"success":false,"error":"Transaction records are immutable and cannot be modified or deleted"}`.

---

## 5. Test Verification Matrix

| Test Suite | File | Checks | Status |
| :--- | :--- | :---: | :---: |
| Amount & UUID Validation | `tests/transaction_validation_test.c` | 10 | Passed |
| Transaction CRUD & Persistence | `tests/transaction_crud_test.c` | 28 | Passed |
| Idempotency & Tenant Independence | `tests/transaction_idempotency_test.c` | 16 | Passed |
| Multi-Threaded Concurrency | `tests/transaction_concurrency_test.c` | 12 | Passed |
| History, Pagination & Filtering | `tests/transaction_history_test.c` | 64 | Passed |
| Transaction Reversal & Lifecycle | `tests/transaction_reversal_test.c` | 20 | Passed |
| HTTP Endpoints, RBAC & Immutability | `tests/transaction_http_test.c` | 33 | Passed |
| **Total Step 5 Checks** | | **183** | **All Passed** |
