# SettleSmart 2.0 — Transaction Lifecycle and DSA Pipeline

## 1. End-to-End Lifecycle Overview

Every financial transaction processed by SettleSmart 2.0 moves through a rigorous pipeline designed for financial accuracy, concurrency safety, and audit integrity:

```
[ Client Request ]
       | (1) POST /api/transactions { idempotency_key, sender_id, receiver_id, amount, note }
       v
[ Auth & Tenant Guard ]
       | (2) Verify Session, Check Role (Admin/Finance), Extract org_id from Server Session
       v
[ Input & Idempotency Check ]
       | (3) Check: amount > 0 (exact decimal), sender != receiver, check idempotency_key
       v
[ Ingestion Queue (FIFO) ]
       | (4) Enqueue transaction in arrival order (Worker thread buffer)
       v
[ Database Persistence (MySQL ACID) ]
       | (5) START TRANSACTION -> Check Composite FKs -> INSERT -> Commit
       |     *Authoritative sequence and ordering established at COMMIT*
       v
[ In-Memory DSA Engine (Active Window) ]
       | (6) Update Graph, HashMap (int64_t paise), Linked List, Stack (Audit only), Max-Heap
       v
[ Real-Time Broadcast ]
       | (7) WebSocket Hub sends TRANSACTION_CREATED to org_id clients
       v
[ Client HTTP Response ]
       | (8) Return HTTP 201 (or HTTP 200 on idempotent replay)
```

---

## 2. Detailed Lifecycle Stages

### Stage 1: Client Request Dispatch with Idempotency Key
- An authenticated user initiates a transfer between two entities.
- The client dispatches an HTTP `POST` request to `/api/transactions` with:
  - Header: `Authorization: Bearer <session_token>`
  - Header: `X-Idempotency-Key: <uuid-v4>` (or passed in body)
- **Request Body (JSON)**:
  ```json
  {
    "idempotency_key": "9b1deb4d-3b7d-4bad-9bdd-2b0d7b3dcb6d",
    "sender_entity_id": 14,
    "receiver_entity_id": 27,
    "amount": 25000.00,
    "description": "Logistics cross-docking invoice #402"
  }
  ```
- *Strict Rule*: The client never sends `transaction_id`, `created_at`, or `organization_id`.

### Stage 2: Authentication and Server-Side Organization Extraction
- The C backend intercepts the incoming HTTP request.
- The session token is validated against the active session store:
  - If invalid or expired: Returns `401 Unauthorized`.
  - If valid: Extracts the authenticated `user_id`, `role`, and `organization_id`.
- **Zero Trust on Client Organization ID**: The backend uses the `organization_id` derived directly from the server session context. Any client-supplied organization ID is ignored.
- Role Check:
  - `Admin` and `Finance_Manager`: Authorized.
  - `Viewer`: Rejected with `403 Forbidden`.

### Stage 3: Input Validation and Idempotency Verification
- The backend performs immediate domain checks:
  1. **Exact Amount Precision**: Validates `amount` as an exact decimal string or integer sub-units (e.g., `2500000` paise for ₹25,000.00). Binary floating-point parsing is strictly prohibited.
  2. **Non-Self Transfer**: `sender_entity_id != receiver_entity_id`.
  3. **Idempotency Lookup**: The backend checks if `(organization_id, idempotency_key)` has already been committed in MySQL.
     - If found: Returns the existing committed transaction payload immediately with `200 OK`, preventing duplicate fund movement.
     - If not found: Continues to the ingestion pipeline.

### Stage 4: Ingestion Queue (FIFO) vs. Concurrency Ordering
- The validated transaction payload is enqueued into the local **FIFO Queue**.
- **Role and Boundary of FIFO Queue**:
  - The in-memory FIFO queue buffers network arrival spikes and schedules jobs for worker threads on this backend instance.
  - **Important Architectural Distinction**: The FIFO queue **does not** guarantee global financial order across multiple concurrent backend instances or threads.
  - Global, authoritative financial ordering is established **only** at the database layer (Stage 5) when MySQL serializes and commits the row.

### Stage 5: Database Persistence & Transactional Outbox (Authoritative Order & Commit)
- The transaction worker executes an atomic database transaction using the MySQL C API:
  ```sql
  START TRANSACTION;
  -- 1. Insert transaction record
  INSERT INTO transactions (
      organization_id,
      idempotency_key,
      request_fingerprint,
      sender_entity_id,
      receiver_entity_id,
      amount,
      currency,
      status,
      transaction_type,
      created_by_user_id,
      created_at,
      description
  ) VALUES (
      ?, ?, ?, ?, ?, ?, 'INR', 'Completed', 'Standard', ?, UTC_TIMESTAMP(), ?
  );
  -- 2. Lock sequence counter and increment
  SELECT event_sequence_counter FROM organizations WHERE id = ? FOR UPDATE;
  UPDATE organizations SET event_sequence_counter = event_sequence_counter + 1 WHERE id = ?;
  -- 3. Append to persistent outbox
  INSERT INTO event_outbox (organization_id, event_sequence, event_type, transaction_id, payload)
  VALUES (?, ?, 'TRANSACTION_CREATED', ?, ?);
  COMMIT;
  ```
- **Composite Foreign Key Verification**: The database engine verifies `(organization_id, sender_entity_id)` and `(organization_id, receiver_entity_id)` against the `entities` table. If either entity belongs to another organization, MySQL aborts with a foreign key violation.
- **Authoritative Sequence**: The commit moment establishes the authoritative transaction ordering.
- **AUTO_INCREMENT Allocation**: Upon commit, `mysql_insert_id()` retrieves the assigned ID. The system recognizes that this ID is unique but may contain sequence gaps if previous transactions were aborted or rolled back.

### Stage 6: In-Memory DSA Engine Synchronization (Active Window)
Once MySQL confirms the write, the C backend immediately synchronizes its in-memory data structures for the active working set of that organization:

1. **Directed Weighted Graph (Active Window)**:
   - Updates the directed weighted edge: $Sender \xrightarrow{amount} Receiver$.
   - Preserves parallel edges for distinct transactions.
   - Updates statuses to `Reversed` upon compensatory reversals.
   - Bounded cache evicts oldest edges and prunes isolated nodes if capacity is reached.
2. **HashMap (Entity Balance Tracker)**:
   - Updates entity balances in $O(1)$ time using exact 64-bit integer arithmetic (`int64_t` paise):
     - `sender_balance -= 2500000`
     - `receiver_balance += 2500000`
3. **Linked List (Session Ledger)**:
   - Appends a transaction node to the chronological linked list ledger for fast sequential traversal of active session transactions.
4. **Stack (Recent Activity & Audit)**:
   - Pushes the new transaction onto the organization's LIFO stack for quick UI inspection.
   - **Crucial Clarification**: Popping from this stack does **NOT** roll back money. Financial reversals must always be executed as explicit database reversal transactions.
5. **Priority Queue (Max-Heap)**:
   - Inserts the transaction keyed by amount/priority into the Max-Heap for high-value monitoring and VIP transaction inspection.

### Stage 7: Real-Time WebSocket Event Dispatch and Replay
- The transaction event is broadcast **only after** MySQL commit:
  ```json
  {
    "event_type": "TRANSACTION_CREATED",
    "event_sequence": 42,
    "transaction_id": 1082,
    "organization_id": 3,
    "sender_entity_id": 14,
    "receiver_entity_id": 27,
    "amount": "25000.00",
    "currency": "INR",
    "status": "Completed",
    "created_at": "2026-11-15 09:30:00"
  }
  ```
- **Replay Capability**: If a client missed events due to network disconnection, it submits `REPLAY <seq>` to receive missing events in order from `event_outbox`.
- The WebSocket Hub broadcasts this message strictly to clients authenticated to `organization_id = 3`.

### Stage 8: HTTP Response Delivery
- The C backend delivers `201 Created` with the committed transaction details and authoritative ID.

---

## 3. Financial Reversal vs. In-Memory Rollback

- **Stack Role**: The in-memory Stack tracks recent activity for user display and diagnostic inspection.
- **Compensatory Reversal Record**:
  - In financial accounting, completed transactions cannot be deleted.
  - To reverse a transaction, an authorized user (Admin or Finance Manager) submits a reversal request referencing `reversal_of_id`.
  - A new transaction record is inserted with swapped sender and receiver entities, `transaction_type = 'Reversal'`, and exact compensatory amount.
  - Both original and reversal records remain permanent entries in MySQL.

---

## 4. In-Memory DSA Layer Integration (Step 6)

In Step 6, the in-memory processing layer is encapsulated within an organization-scoped `DsaState` structure (`backend/src/dsa/dsa_state.h`).

### Active Routing Rules:
1. **Pending Transactions**:
   - Appended to `Queue pending_queue` (FIFO order).
   - Inserted into `PriorityQueue pending_priority_queue` (Max-Heap by `amount_paise DESC`).
   - Prepended to `LinkedList recent_transactions` (bounded recent history).
   - **Do NOT** alter `HashMap entity_balances` and are **not** pushed to `Stack completed_stack`.
2. **Completed Transactions**:
   - Pushed to `Stack completed_stack` (LIFO order, newest on top).
   - Removed from `pending_queue` and `pending_priority_queue`.
   - Update `HashMap entity_balances`: sender balance decreases by `amount_paise`, receiver balance increases by `amount_paise`.
   - Prepended to `LinkedList recent_transactions`.
3. **Reversals**:
   - Created with `status = Completed`, `sender_entity_id = original_receiver`, `receiver_entity_id = original_sender`.
   - Updates balances in reverse direction (original sender credited, original receiver debited).
   - Pushed to `Stack completed_stack`.
4. **Recovery from MySQL**:
   - On backend startup or failover, `dsa_state_recover_from_db` reconstructs all 5 structures for the tenant directly from authoritative MySQL tables.

