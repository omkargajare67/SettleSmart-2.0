# SettleSmart 2.0 — Real-Time Communication and Event Outbox

## 1. Architectural Overview

SettleSmart 2.0 provides sub-millisecond real-time event distribution to connected browser clients using WebSockets over `/ws/graph`. To ensure financial consistency across network disruptions and concurrent worker instances, the system implements the **Transactional Outbox Pattern** with strict monotonic sequential ordering.

```
 Client Browser              HTTP / WebSocket Engine            MySQL Database
       |                                |                             |
       |----- Connect: /ws/graph ------>|                             |
       |      (Cookie + Origin)         |--- Verify Session --------->|
       |<---- 101 Switching Protocols---|                             |
       |                                |                             |
       |                                |                             |
       |  [Transaction Creation / Reversal Process]                   |
       |                                |--- BEGIN ------------------>|
       |                                |--- Insert Transaction ----->|
       |                                |--- Lock Sequence Counter -->|
       |                                |    (SELECT FOR UPDATE)      |
       |                                |--- Append Event Outbox ---->|
       |                                |--- COMMIT ----------------->|
       |                                |                             |
       |<---- WebSocket Broadcast ------|                             |
       |      (TRANSACTION_CREATED)     |                             |
```

---

## 2. Handshake Security and Multi-Tenant Isolation

1. **Protocol Endpoint**: `/ws/graph`.
2. **Authentication**: Handshake requests must provide a valid `session_token` cookie. Missing or expired sessions are rejected with `401 Unauthorized`.
3. **CORS Origin Validation**: The request `Origin` header must strictly match the backend's configured allowlist (e.g., `http://localhost:3000`). Mismatches receive `403 Forbidden`.
4. **Multi-Tenant Registry**: Connected WebSockets are tracked in a connection registry (`src/websocket/websocket.h`). Each entry associates the network connection with the user's verified `organization_id`. Broadcasts filter exclusively by `organization_id`, guaranteeing absolute isolation between organizations.

---

## 3. Transactional Outbox Pattern and Monotonic Ordering

To prevent phantom events, missed events, or out-of-order broadcasts under concurrency:
1. **Atomic In-Transaction Persistence**:
   - Outbox rows are inserted within the same database transaction as the financial transaction creation or reversal.
   - The organization's sequence counter is locked using `SELECT event_sequence_counter FROM organizations WHERE id = ? FOR UPDATE`.
   - The sequence number is incremented monotonically and written to `event_outbox`:
     ```sql
     INSERT INTO event_outbox (organization_id, event_sequence, event_type, transaction_id, payload)
     VALUES (?, ?, 'TRANSACTION_CREATED', ?, ?);
     ```
2. **Monotonic Watermark Guarantee**: Every organization maintains its own contiguous stream `1, 2, 3, ... N` without gaps or duplicates, even under heavy concurrent multi-threaded writes.
3. **Post-Commit Broadcast**: The event is broadcast via WebSockets only after the database transaction successfully commits.

---

## 4. Replay and Reconnection Protocol

When a frontend client experiences temporary network disconnects or page refreshes, it can replay missed events from the persistent outbox:

1. **Snapshot Watermark**: When calling `GET /api/graph`, the response includes `event_sequence_watermark`.
2. **Replay Request**: Upon establishing or re-establishing the WebSocket connection, the client sends a text frame:
   ```
   REPLAY <last_seen_sequence>
   ```
   Example:
   ```
   REPLAY 1
   ```
3. **Outbox Query and Delivery**: The backend queries all outbox events for the organization where `event_sequence > ? ORDER BY event_sequence ASC` and streams them in order.

---

## 5. Event Message Specifications

Events are formatted as JSON text frames:

### TRANSACTION_CREATED Event
```json
{
  "event_type": "TRANSACTION_CREATED",
  "event_sequence": 2,
  "transaction_id": 105,
  "organization_id": 1,
  "sender_entity_id": 10,
  "receiver_entity_id": 20,
  "amount": "500.00",
  "currency": "INR",
  "status": "Completed",
  "created_at": "2026-11-16 07:15:00"
}
```

### TRANSACTION_REVERSED Event
```json
{
  "event_type": "TRANSACTION_REVERSED",
  "event_sequence": 3,
  "transaction_id": 106,
  "organization_id": 1,
  "sender_entity_id": 20,
  "receiver_entity_id": 10,
  "amount": "500.00",
  "currency": "INR",
  "status": "Reversed",
  "original_transaction_id": 105,
  "created_at": "2026-11-16 07:16:00"
}
```
