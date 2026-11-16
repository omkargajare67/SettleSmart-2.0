# SettleSmart 2.0 — Directed Weighted Graph Implementation

## 1. Architectural Overview

The Directed Weighted Graph in SettleSmart 2.0 models financial debt relationships between business entities within an organization. Each node represents an entity (e.g., department, vendor, subsidiary, client, partner), and each directed edge represents a financial obligation or transfer of funds from sender to receiver.

MySQL remains the permanent, authoritative source of truth. The in-memory graph functions as an active working cache and calculation topology for cycle detection and greedy debt settlement (introduced in Step 8).

```
                      +-----------------------------+
                      |   MySQL Database (Source)   |
                      |   - entities                |
                      |   - transactions            |
                      +--------------+--------------+
                                     |
                         Database Recovery / Cache Init
                                     v
                      +-----------------------------+
                      | In-Memory Graph Cache (C11) |
                      |   - Adjacency List          |
                      |   - Bounded Edge Eviction   |
                      |   - Isolated Node Pruning   |
                      +--------------+--------------+
                                     |
               +---------------------+---------------------+
               |                                           |
               v                                           v
    REST Snapshot API                           Real-Time WebSocket
    GET /api/graph                              /ws/graph (Streaming)
```

---

## 2. Graph Data Structures and Memory Topology

The graph is implemented in pure C11 using an Adjacency List representation (`src/graph/graph.h`, `src/graph/graph.c`):

### GraphEdge
- `transaction_id`: Unique identifier referencing MySQL `transactions.id`.
- `sender_entity_id`: Node identifier for fund sender.
- `receiver_entity_id`: Node identifier for fund receiver.
- `amount_paise`: Exact `int64_t` currency representation in lowest units (e.g., paise). Zero floating-point math.
- `currency`: 3-character ISO currency code (e.g., `INR`, `USD`).
- `status`: Lifecycle state (`Pending`, `Completed`, `Reversed`, `Disputed`).
- `created_at`: Creation timestamp string.
- `next`: Pointer to subsequent outgoing edge in the node's singly linked adjacency list.

### GraphNode
- `entity_id`: Unique identifier referencing MySQL `entities.id`.
- `entity_name`: String label of the entity.
- `first_edge`: Head pointer to the linked list of outgoing `GraphEdge` elements.

### Graph
- `organization_id`: Organization scope guaranteeing strict multi-tenant isolation.
- `nodes`: Dynamically reallocated array of `GraphNode` pointers (`GraphNode **`).
- `node_count`: Number of active nodes currently in the graph.
- `node_capacity`: Allocated capacity of the node pointer array.
- `edge_count`: Total count of active edges across all nodes.
- `edge_capacity`: Maximum allowable edge count before eviction triggers.
- `head_edge_order` / `tail_edge_order`: Pointers tracking edge insertion order for FIFO cache eviction.

---

## 3. Parallel Edge Preservation and Reversals

In real-world business scenarios, entities regularly conduct multiple distinct transfers with each other:
1. **Parallel Edges**: The graph explicitly preserves multiple independent transfers between identical sender and receiver entities as separate edges, avoiding premature lossy balance merging.
2. **Reversal Representation**: Financial reversals are recorded as distinct edges pointing in the opposite direction (from receiver back to sender) while marking the original edge status as `Reversed`. Both edges coexist to maintain full auditability.

---

## 4. In-Memory Bounded Cache and Automatic Pruning

To operate reliably in memory-constrained environments:
- **Bounded Edge Eviction**: When `edge_count >= edge_capacity`, inserting a new edge automatically evicts the oldest edge (`head_edge_order`).
- **Isolated Node Pruning**: When an edge is removed or evicted, if either the sender or receiver node has zero remaining incoming or outgoing edges, the graph prunes the node and shrinks the node array, preventing memory leaks and orphaned vertices.

---

## 5. Database Recovery and Synchronization

Upon startup or cache invalidation, `graph_service_recover_from_db` rebuilds the graph state directly from MySQL:
1. Queries all active entities for the organization:
   ```sql
   SELECT id, name FROM entities WHERE organization_id = ? AND status = 'Active';
   ```
2. Queries the most recent non-reversed transactions up to the cache capacity:
   ```sql
   SELECT id, sender_entity_id, receiver_entity_id, amount, currency, status, created_at 
   FROM transactions 
   WHERE organization_id = ? AND status IN ('Pending', 'Completed') 
   ORDER BY id DESC LIMIT ?;
   ```
3. Parses exact decimal strings into `int64_t` paise and inserts edges into the graph.

---

## 6. REST Snapshot API (`GET /api/graph`)

The backend exposes `GET /api/graph` for client initialization:
- **Authentication**: Requires valid HttpOnly `session_token` cookie and matching `Origin`.
- **Query Parameters**:
  - `limit`: Maximum edges per page (default: 50, max: 200).
  - `page`: Page index (1-based).
  - `entity_id`: Filter edges involving a specific entity.
  - `status`: Filter by status (`Pending`, `Completed`, `Reversed`, `Disputed`).
  - `from_date` / `to_date`: Timestamp range filtering.
- **Response Format**:
  ```json
  {
    "status": "success",
    "organization_id": 1,
    "event_sequence_watermark": 42,
    "pagination": {
      "total_edges": 2,
      "page": 1,
      "limit": 50,
      "total_pages": 1
    },
    "nodes": [
      {"id": 10, "label": "Engineering"},
      {"id": 20, "label": "Vendor Corp"}
    ],
    "edges": [
      {
        "id": 1001,
        "from": 10,
        "to": 20,
        "amount": "5000.00",
        "currency": "INR",
        "status": "Completed",
        "created_at": "2026-11-16 07:00:00"
      }
    ]
  }
  ```
