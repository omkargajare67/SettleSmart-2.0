# TRANSACTION PROCESSING ENGINE SPECIFICATION

## 1. Overview
The Transaction Processing Engine in SettleSmart 2.0 governs the state transitions of financial records from admission to completion or reversal. It bridges persistent MySQL transactions with in-memory Data Structures & Algorithms (DSA) state and real-time WebSocket event dissemination.

## 2. Architecture & Components
- **Source Module**: `backend/src/transactions/transaction_processor.c`, `transaction_processor.h`
- **Concurrency Strategy**: Pessimistic locking via `SELECT ... FOR UPDATE` with deterministic ascending ID ordering for sender and receiver entities to prevent database deadlocks.
- **ACID Integrity**: Atomically updates transaction status, writes `TRANSACTION_STATUS_CHANGED` events to `event_outbox`, and commits before triggering post-commit in-memory DSA state updates and WebSocket broadcasts.
- **Multi-Tenant Isolation**: Enforces `organization_id` on all database operations, in-memory queues, hash maps, heaps, and WebSocket channels.

## 3. Transaction Lifecycle States
1. **Pending**: Admitted via `POST /api/transactions`, enqueued into the FIFO processing queue and priority heap.
2. **Completed**: Processed via `POST /api/transactions/process-next` or `POST /api/transactions/process-batch`. Financial balances updated in the entity HashMap and edges reflected in the directed graph.
3. **Reversed**: Created via `POST /api/transactions/:id/reversal` in `Pending` state. When processed, both the original transaction transitions to `Reversed` and the reversal transaction transitions to `Completed`.
4. **Disputed**: Flagged for administrative audit; excluded from standard automated processing queues.

## 4. Normal Processing Flow
1. Fetch the next pending transaction in FIFO order (`ORDER BY created_at ASC, id ASC LIMIT 1`).
2. Begin MySQL transaction (`START TRANSACTION`).
3. Lock the transaction record with `FOR UPDATE`. Verify it is still in `Pending` status.
4. Lock both sender and receiver entities in ascending ID order (`ORDER BY id FOR UPDATE`) and verify both are `Active`.
5. Update transaction status to `Completed`.
6. Write outbox event `TRANSACTION_STATUS_CHANGED` with transaction payload and incremented organization event sequence counter.
7. Commit MySQL transaction.
8. Synchronize in-memory `DsaState`:
   - Dequeue from FIFO `Queue`.
   - Pop/remove from `PriorityQueue`.
   - Update `HashMap` balances: debit sender (`-amount_paise`), credit receiver (`+amount_paise`).
   - Push to LIFO `Stack` (recent transactions history).
   - Append to `LinkedList`.
   - Update `DirectedGraph` edge status to `Completed`.
9. Broadcast `TRANSACTION_STATUS_CHANGED` event to all connected WebSocket clients in the organization.

## 5. Reversal Processing Flow
1. Fetch pending reversal transaction (`transaction_type = 'Reversal'` and `reversal_of_id > 0`).
2. Begin MySQL transaction.
3. Lock both the reversal transaction and the original transaction with `FOR UPDATE`.
4. Verify the original transaction is currently `Completed`.
5. Lock sender and receiver entities and verify active status.
6. Atomically update original transaction to `Reversed` and reversal transaction to `Completed`.
7. Write two outbox events for the status transitions.
8. Commit MySQL transaction.
9. Synchronize in-memory structures:
   - Balance adjustments: The reversal transaction (receiver -> sender) credits the original sender and debits the original receiver, exactly cancelling out net balances to 0 paise.
   - DirectedGraph: Updates original edge to `Reversed` and inserts reversal edge as `Completed`.
10. Broadcast WebSocket events for both transactions.

## 6. HTTP API Endpoints
- `POST /api/transactions/process-next`: Processes the oldest pending transaction in FIFO order. (Roles: Admin, Finance_Manager).
- `POST /api/transactions/process-batch`: Processes up to `batch_size` (default 10, max 100) pending transactions sequentially. (Roles: Admin, Finance_Manager).
- `GET /api/transactions/processing/status`: Returns counts of pending, completed, reversed, and total transactions. (Roles: Admin, Finance_Manager, Viewer).
- `GET /api/transactions/pending/priority?limit=10`: Returns pending transactions ordered by Priority Queue (Max-Heap by amount descending, timestamp ascending, ID ascending). (Roles: Admin, Finance_Manager, Viewer).
- `GET /api/transactions/recent/sorted?sort_by=amount&order=desc`: Returns recent transactions sorted in memory using Bubble Sort.
- `GET /api/transactions/recent/search?id=:id`: Performs O(log n) Binary Search on sorted recent transactions by ID.
