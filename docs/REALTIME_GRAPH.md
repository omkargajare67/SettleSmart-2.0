# SettleSmart 2.0 — Real-Time Transaction Graph Architecture

## 1. Overview and Graphical Concept

The Real-Time Transaction Graph is a core capability of SettleSmart 2.0. Rather than viewing complex multi-party financial flows as static tabular ledger rows, the system renders an interactive, directed weighted graph:

- **Vertices ($V$)**: Represent entities (departments, vendors, clients, subsidiaries).
  - Node attributes: Entity ID, Entity Name, Entity Type, Current Net Balance stored with exact integer/decimal precision (positive = net creditor/green; negative = net debtor/red; zero = neutral/grey).
- **Directed Edges ($E$)**: Represent transactions transferring value from a sender to a receiver.
  - Edge attributes: Transaction ID, Transfer Amount (exact decimal/integer paise), Currency, UTC Timestamp, Reversal Status.
  - Direction: Arrow points strictly from **Sender $\rightarrow$ Receiver**.

---

## 2. Dynamic Graph Progression Example

Consider three sequential transactions within an organization:

### Transaction 1: Entity A sends ₹500.00 to Entity B
- Node A (Balance: -₹500.00)
- Node B (Balance: +₹500.00)
- Directed Edge: $A \xrightarrow{₹500.00} B$

```
[ Entity A ] ------------ (₹500.00) ------------> [ Entity B ]
 (Net: -500.00)                                   (Net: +500.00)
```

### Transaction 2: Entity B sends ₹300.00 to Entity C
- Node A (Balance: -₹500.00)
- Node B (Balance: +₹200.00)
- Node C (Balance: +₹300.00)
- The graph animates the new directed line from B to C.

```
[ Entity A ] ---> (₹500.00) ---> [ Entity B ] ---> (₹300.00) ---> [ Entity C ]
 (Net: -500.00)                  (Net: +200.00)                   (Net: +300.00)
```

### Transaction 3: Entity A sends ₹150.00 to Entity D
- The graph expands with a branch from A to D:

```
                                [ Entity B ] ---> (₹300.00) ---> [ Entity C ]
                               ^
                              /
                             / (₹500.00)
                            /
[ Entity A ] --------------+
 (Net: -650.00)             \
                             \ (₹150.00)
                              v
                                [ Entity D ]
                                 (Net: +150.00)
```

---

## 3. Scalability Architecture: MySQL Source of Truth vs. Windowed RAM Graph

To prevent memory exhaustion in high-volume enterprise organizations:

1. **MySQL Retains Complete Historical Ledger**:
   - Every entity and transaction across all years is stored durably in MySQL.
   - The backend **must not** attempt to load an organization's entire multi-year transaction history (hundreds of thousands of transactions) into in-memory RAM simultaneously.
2. **Windowed / Filtered Active In-Memory Graph**:
   - The in-memory graph in the C backend is scoped to an **active working window** (e.g., current financial quarter, active entities, or recent $N$ transactions).
   - This keeps memory usage predictable and bounded (typically under a few megabytes per organization).
3. **On-Demand Historical Graph Queries**:
   - When a user in the React frontend requests a graph for a past date range (e.g., "Q1 2025" or "Transactions > ₹100,000"), the C backend executes an indexed SQL query against MySQL:
     ```sql
     SELECT id, sender_entity_id, receiver_entity_id, amount, status, created_at
     FROM transactions
     WHERE organization_id = ?
       AND created_at BETWEEN ? AND ?
     ORDER BY created_at ASC;
     ```
   - The backend constructs a temporary graph response stream directly from the database result set.
4. **Live Incremental Updates (WebSockets)**:
   - Live WebSocket updates are emitted **only after** MySQL commits the transaction.
   - Connected clients receive incremental `TRANSACTION_CREATED` events and animate the new directed edge into their local browser canvas.

---

## 4. WebSocket Communication Protocol

### 4.1 Connection & Handshake
- **Connection Endpoint**: `ws://<server_host>:<websocket_port>/ws`
- **Authentication**: Provided during connection initiation via validated token:
  `ws://localhost:8081/ws?token=<session_token>`
- **Organization Channeling**: The C backend associates each socket with its verified server-side `organization_id`. Sockets only receive broadcasts for their organization.

### 4.2 Event Payloads

#### Event: `TRANSACTION_CREATED`
Broadcast when a transaction is committed to MySQL:
```json
{
  "event": "TRANSACTION_CREATED",
  "organization_id": 1,
  "payload": {
    "transaction_id": 104,
    "sender_entity_id": 2,
    "receiver_entity_id": 4,
    "amount": "75.00",
    "currency": "INR",
    "timestamp": "2026-11-15T09:35:12Z",
    "sender_balance": "125.00",
    "receiver_balance": "225.00"
  }
}
```

#### Event: `ENTITY_CREATED`
Broadcast when a new entity is created:
```json
{
  "event": "ENTITY_CREATED",
  "organization_id": 1,
  "payload": {
    "entity_id": 5,
    "name": "Packaging Depot",
    "entity_type": "Vendor",
    "net_balance": "0.00"
  }
}
```

---

## 5. React Frontend Integration Strategy (Step 10)

In Step 10, the React frontend will implement the visual graph interface using an optimized canvas/SVG visualization pipeline:

1. **Interactive Controls**:
   - **Pan & Zoom**: Smooth mouse wheel zoom and canvas dragging.
   - **Entity Selection**: Clicking a node highlights incoming (green) and outgoing (red) edges, dimming unrelated nodes.
   - **Edge Inspection**: Clicking a directed edge displays transaction ID, exact amount, timestamp, recording user, and notes.
2. **Animation Mechanics**:
   - When a new transaction event arrives via WebSockets:
     - An animated dashed path or pulsing particle travels from the sender node to the receiver node.
     - The sender and receiver node balance badges animate their numeric change.
3. **Bounded Graph Filtering**:
   - Filter by date range (e.g., active billing month).
   - Filter by amount threshold (e.g., high-value flows only).
