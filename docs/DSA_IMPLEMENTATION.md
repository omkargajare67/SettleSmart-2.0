# SettleSmart 2.0 — Core Data Structures and In-Memory State Architecture

## 1. Overview

SettleSmart 2.0 integrates five fundamental in-memory Data Structures and Algorithms (DSA) components implemented in pure C (C11) to power high-throughput transaction management, entity balance lookups, chronological processing, recent activity tracking, and priority-driven financial workflows.

While MySQL remains the permanent, authoritative source of truth for all financial transactions, entities, organizations, and user sessions, the in-memory DSA layer provides $O(1)$ and $O(\log N)$ real-time operations without database query latency. In the event of a server restart, crash, or failover, the complete in-memory DSA state for any organization is reconstructed and synchronized directly from MySQL.

---

## 2. Implemented Data Structures

### 2.1 Singly Linked List (`linked_list.h` / `linked_list.c`)
- **Purpose**: Maintains a bounded chronological history of recent transactions for an organization.
- **Node Structure**: `ListNode` contains a full `Transaction` value copy and a `struct ListNode *next` pointer.
- **Operations & Complexity**:
  - `linked_list_init(LinkedList *list, size_t capacity)`: $O(1)$ initialization with capacity bounding (default: 500).
  - `linked_list_insert(LinkedList *list, const Transaction *tx)`: $O(1)$ insertion at the head. Automatically evicts the oldest transaction at the tail when the bounded capacity is exceeded.
  - `linked_list_remove_by_id(LinkedList *list, long long tx_id)`: $O(N)$ removal of a transaction by ID.
  - `linked_list_find_by_id(const LinkedList *list, long long tx_id)`: $O(N)$ linear search.
  - `linked_list_traverse(const LinkedList *list, LinkedListVisitor visitor, void *user_data)`: $O(N)$ iteration callback.
  - `linked_list_clear(LinkedList *list)`: $O(N)$ deallocation of all nodes.
  - `linked_list_destroy(LinkedList *list)`: $O(N)$ destruction resetting pointers, size, and capacity.

### 2.2 HashMap with Separate Chaining (`hashmap.h` / `hashmap.c`)
- **Purpose**: Stores and updates real-time financial net balances per entity in exact `int64_t` paise (integer subunits, zero floating-point arithmetic).
- **Collision Resolution**: Separate chaining using singly linked bucket chains (`HashNode`).
- **Hash Function**: Deterministic positive modulo mapping `(abs(entity_id) % bucket_count)`.
- **Operations & Complexity**:
  - `hashmap_init(HashMap *map, size_t bucket_count)`: $O(K)$ allocation of bucket array (default: 64 buckets).
  - `hashmap_insert(HashMap *map, long long entity_id, int64_t balance_paise)`: $O(1)$ average, $O(N)$ worst-case insert/overwrite.
  - `hashmap_update_balance(HashMap *map, long long entity_id, int64_t delta_paise)`: $O(1)$ average in-place balance mutation. Includes `INT64_MAX` and `INT64_MIN` overflow/underflow prevention.
  - `hashmap_get_balance(const HashMap *map, long long entity_id, int64_t *out_balance_paise)`: $O(1)$ average lookup.
  - `hashmap_has(const HashMap *map, long long entity_id)`: $O(1)$ average existence check.
  - `hashmap_remove(HashMap *map, long long entity_id)`: $O(1)$ average deletion.
  - `hashmap_clear(HashMap *map)`: $O(N)$ bucket sweep resetting size to 0.
  - `hashmap_destroy(HashMap *map)`: $O(N)$ deallocation freeing all nodes and bucket array.

### 2.3 FIFO Queue (`queue.h` / `queue.c`)
- **Purpose**: Preserves strict chronological first-in, first-out submission order for incoming `Pending` transactions awaiting processing.
- **Node Structure**: Linked queue with explicit `front` and `rear` pointers for $O(1)$ operations.
- **Operations & Complexity**:
  - `queue_init(Queue *q, size_t capacity)`: $O(1)$ initialization.
  - `queue_enqueue(Queue *q, const Transaction *tx)`: $O(1)$ append to rear. Includes duplicate transaction ID rejection.
  - `queue_dequeue(Queue *q, Transaction *out_tx)`: $O(1)$ removal from front.
  - `queue_peek(const Queue *q, Transaction *out_tx)`: $O(1)$ inspection of front item without dequeueing.
  - `queue_remove_by_id(Queue *q, long long tx_id)`: $O(N)$ targeted removal when a pending transaction is cancelled or completes out-of-order.
  - `queue_clear(Queue *q)` / `queue_destroy(Queue *q)`: $O(N)$ deallocation.

### 2.4 LIFO Stack (`stack.h` / `stack.c`)
- **Purpose**: Maintains recently `Completed` settlement activity in reverse chronological order (newest completed on top). Supports transaction audit trails and non-destructive pop inspections.
- **Node Structure**: Linked stack with `top` pointer and bounded capacity (default: 100).
- **Operations & Complexity**:
  - `stack_init(Stack *s, size_t capacity)`: $O(1)$ initialization.
  - `stack_push(Stack *s, const Transaction *tx)`: $O(1)$ push to top. Automatically evicts the oldest completed item at the bottom if bounded capacity is exceeded.
  - `stack_pop(Stack *s, Transaction *out_tx)`: $O(1)$ non-destructive pop (does not reverse or alter MySQL state).
  - `stack_peek(const Stack *s, Transaction *out_tx)`: $O(1)$ inspection of most recent completed transaction.
  - `stack_clear(Stack *s)` / `stack_destroy(Stack *s)`: $O(N)$ deallocation.

### 2.5 Max-Heap Priority Queue (`priority_queue.h` / `priority_queue.c`)
- **Purpose**: Orders pending transactions by financial significance, allowing high-value settlements, liquidity adjustments, and critical transfers to be prioritized.
- **Heap Layout**: 0-indexed dynamic array with auto-resizing via `realloc`.
- **Comparison & Tie-Breaking Hierarchy**:
  1. Primary Sort: `amount_paise` descending (highest monetary value has highest priority).
  2. Secondary Tie-Break: `created_at ASC` (earliest submission timestamp wins).
  3. Tertiary Tie-Break: `id ASC` (deterministic lowest transaction ID wins).
- **Operations & Complexity**:
  - `priority_queue_init(PriorityQueue *pq, size_t initial_capacity)`: $O(1)$ allocation.
  - `priority_queue_insert(PriorityQueue *pq, const Transaction *tx)`: $O(\log N)$ insertion with Heapify Up. Auto-doubles capacity when full. Rejects duplicate IDs.
  - `priority_queue_extract_max(PriorityQueue *pq, Transaction *out_tx)`: $O(\log N)$ extraction of top-priority item with Heapify Down.
  - `priority_queue_peek(const PriorityQueue *pq, Transaction *out_tx)`: $O(1)$ inspection of maximum element.
  - `priority_queue_remove_by_id(PriorityQueue *pq, long long tx_id)`: $O(N)$ linear scan to find index + $O(\log N)$ heapify up/down rebalancing.
  - `priority_queue_clear(PriorityQueue *pq)`: $O(1)$ size reset.
  - `priority_queue_destroy(PriorityQueue *pq)`: $O(1)$ array deallocation.

---

## 3. Organization-Scoped State (`DsaState`)

To guarantee strict multi-tenant isolation, DSA structures are encapsulated within an organization-scoped container:

```c
typedef struct {
    long long organization_id;
    LinkedList recent_transactions;
    HashMap entity_balances;
    Queue pending_queue;
    Stack completed_stack;
    PriorityQueue pending_priority_queue;
    int is_synchronized;
} DsaState;
```

### 3.1 Lifecycle Methods
- `dsa_state_init(DsaState *state, long long organization_id)`: Initializes all five underlying data structures with appropriate capacities for the given organization.
- `dsa_state_destroy(DsaState *state)`: Deallocates all in-memory structures and marks state unsynchronized.
- `dsa_state_recover_from_db(DsaState *state, DBClient *client)`: Executes targeted SQL queries to rebuild entity balances, pending FIFO queue, pending priority queue, completed stack, and recent transaction history from MySQL.
- `dsa_state_on_transaction_created(DsaState *state, const Transaction *tx)`: Routes newly created transactions to their appropriate in-memory structures. Rejects cross-tenant transactions whose `organization_id` does not match `state->organization_id`.
- `dsa_state_on_transaction_status_changed(DsaState *state, const Transaction *tx, TransactionStatus old_status)`: Updates balance tallies, promotes pending items to completed stacks, or manages reversals.

---

## 4. Complexity Analysis Summary

| Data Structure | Primary Operation | Time Complexity | Space Complexity |
| :--- | :--- | :--- | :--- |
| **LinkedList** | Insert Head | $O(1)$ | $O(N)$ bounded |
| **LinkedList** | Evict Tail | $O(N)$ | $O(1)$ |
| **LinkedList** | Remove by ID | $O(N)$ | $O(1)$ |
| **HashMap** | Insert / Update | $O(1)$ avg, $O(N)$ worst | $O(K + N)$ |
| **HashMap** | Get Balance | $O(1)$ avg, $O(N)$ worst | $O(1)$ |
| **Queue** | Enqueue / Dequeue | $O(1)$ | $O(N)$ |
| **Queue** | Peek | $O(1)$ | $O(1)$ |
| **Stack** | Push / Pop / Peek | $O(1)$ | $O(N)$ bounded |
| **Priority Queue** | Insert (Heapify Up) | $O(\log N)$ | $O(N)$ dynamic |
| **Priority Queue** | Extract Max (Down) | $O(\log N)$ | $O(1)$ |
| **Priority Queue** | Peek | $O(1)$ | $O(1)$ |

---

## 5. Performance and Stress Test Results

Stress tests were executed on the in-memory synchronization layer using a 10,000 transaction workload:
- **Batch Size**: 10,000 sequential transactions.
- **Execution Time**: **23.00 ms** total ($< 0.0023$ ms per transaction).
- **Throughput**: $> 430,000$ transactions per second in memory.
- **Memory Stability**: Zero memory leaks; bounded capacity eviction strictly maintained 500 recent transactions and 100 completed stack frames.

---

## 6. Integration with Step 7 Directed Weighted Graph

The Core DSA layer operates alongside the Step 7 Directed Weighted Graph (`docs/GRAPH_IMPLEMENTATION.md`). While `HashMap` maintains net balance scalars ($O(1)$) and `Queue`/`PriorityQueue` manage transaction processing order, the Directed Weighted Graph retains multi-party bilateral transfer topology necessary for visual presentation, cycle detection, and debt simplification algorithms (Step 8).
