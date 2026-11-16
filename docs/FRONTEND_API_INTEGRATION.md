# FRONTEND API INTEGRATION MAP — SETTLESMART 2.0

All frontend API calls communicate with the pure C backend server via standard JSON HTTP routes and WebSocket upgrades.

## 1. Authentication Endpoints

| Method | Endpoint | Headers | Request Body | Response Body | Role Access |
|---|---|---|---|---|---|
| `POST` | `/api/auth/register-organization` | `Content-Type: application/json` | `{"organization_name":"...", "admin_email":"...", "admin_password":"..."}` | `{"success":true, "organization_id":N, "admin_user_id":M}` | Public (when enabled) |
| `POST` | `/api/auth/login` | `Content-Type: application/json` | `{"organization_id":N, "email":"...", "password":"..."}` | `{"success":true, "csrf_token":"...", "user":{...}}` + `Set-Cookie: session_token=...` | Public |
| `POST` | `/api/auth/logout` | `Cookie`, `X-CSRF-Token` | None | `{"success":true, "message":"..."}` | Authenticated |
| `GET` | `/api/auth/me` | `Cookie` | None | `{"success":true, "user":{"id":..., "organization_id":..., "email":"...", "role":"..."}}` | Authenticated |
| `POST` | `/api/auth/csrf` | `Cookie`, `Origin` | None | `{"success":true, "csrf_token":"..."}` | Authenticated |
| `POST` | `/api/auth/change-password` | `Cookie`, `X-CSRF-Token` | `{"old_password":"...", "new_password":"..."}` | `{"success":true, "message":"..."}` | Authenticated |

---

## 2. Entity Management Endpoints

| Method | Endpoint | Headers | Query / Body | Response Body | Role Access |
|---|---|---|---|---|---|
| `GET` | `/api/entities` | `Cookie` | Query: `page`, `limit`, `search`, `type`, `status`, `sort_by`, `order` | `{"success":true, "data":[...], "pagination":{...}}` | All Roles |
| `GET` | `/api/entities/:id` | `Cookie` | None | `{"success":true, "entity":{...}}` | All Roles |
| `POST` | `/api/entities` | `Cookie`, `X-CSRF-Token` | `{"name":"...", "entity_type":"Department\|Vendor\|..."}` | `{"success":true, "entity":{...}}` | Admin, Finance Manager |
| `PATCH` | `/api/entities/:id` | `Cookie`, `X-CSRF-Token` | `{"name":"...", "entity_type":"..."}` | `{"success":true, "entity":{...}}` | Admin, Finance Manager |
| `PATCH` | `/api/entities/:id/status` | `Cookie`, `X-CSRF-Token` | `{"status":"Active\|Inactive"}` | `{"success":true, "entity":{...}}` | Admin, Finance Manager |

---

## 3. Transaction Management Endpoints

| Method | Endpoint | Headers | Query / Body | Response Body | Role Access |
|---|---|---|---|---|---|
| `GET` | `/api/transactions` | `Cookie` | Query: `page`, `limit`, `id`, `entity_id`, `status`, `sort_by`, `order` | `{"success":true, "data":[...], "pagination":{...}}` | All Roles |
| `GET` | `/api/transactions/recent` | `Cookie` | Query: `limit` (default 20) | `{"success":true, "count":N, "data":[...]}` | All Roles |
| `GET` | `/api/transactions/:id` | `Cookie` | None | `{"success":true, "transaction":{...}}` | All Roles |
| `POST` | `/api/transactions` | `Cookie`, `X-CSRF-Token`, `Idempotency-Key` (UUIDv4) | `{"sender_entity_id":N, "receiver_entity_id":M, "amount":"100.50", "currency":"INR"}` | `{"success":true, "idempotent_replay":bool, "transaction":{...}}` | Admin, Finance Manager |
| `POST` | `/api/transactions/:id/reversal` | `Cookie`, `X-CSRF-Token`, `Idempotency-Key` (UUIDv4) | None | `{"success":true, "idempotent_replay":bool, "transaction":{...}}` | Admin, Finance Manager |

---

## 4. Processing Engine & Algorithms Endpoints

| Method | Endpoint | Headers | Body / Query | Response Body | Role Access |
|---|---|---|---|---|---|
| `POST` | `/api/transactions/process-next` | `Cookie`, `X-CSRF-Token` | None | `{"success":true, "transaction":{...}}` | Admin, Finance Manager |
| `POST` | `/api/transactions/process-batch` | `Cookie`, `X-CSRF-Token` | `{"batch_size": 10}` | `{"success":true, "processed_count":N}` | Admin, Finance Manager |
| `GET` | `/api/transactions/processing/status` | `Cookie` | None | `{"success":true, "status":{"pending_count":..., "completed_count":..., "reversed_count":...}}` | All Roles |
| `GET` | `/api/transactions/pending/priority` | `Cookie` | Query: `limit` (default 50) | `{"success":true, "count":N, "transactions":[...]}` | All Roles |
| `GET` | `/api/entities/balances` | `Cookie` | None | `{"success":true, "count":N, "balances":[{"entity_id":..., "balance_paise":..., "balance_str":"..."}]}` | All Roles |
| `GET` | `/api/settlements/recommendations` | `Cookie` | None | `{"success":true, "total_settled_paise":..., "recommendations":[...]}` | All Roles |
| `GET` | `/api/transactions/recent/sorted` | `Cookie` | Query: `sort_by=amount\|timestamp\|id`, `order=asc\|desc`, `limit=50` | `{"success":true, "transactions":[...]}` | All Roles |
| `GET` | `/api/transactions/recent/search` | `Cookie` | Query: `id=N` | `{"success":true, "transaction":{...}}` | All Roles |

---

## 5. Organization Team Management Endpoints

| Method | Endpoint | Headers | Body / Query | Response Body | Role Access |
|---|---|---|---|---|---|
| `GET` | `/api/organization/users` | `Cookie` | None | `{"success":true, "users":[...]}` | All Roles |
| `POST` | `/api/organization/users` | `Cookie`, `X-CSRF-Token` | `{"email":"...", "password":"...", "role":"..."}` | `{"success":true, "user_id":N}` | Admin Only |
| `PATCH` | `/api/organization/users/:id` | `Cookie`, `X-CSRF-Token` | `{"role":"...", "status":"Active\|Inactive"}` | `{"success":true}` | Admin Only |

---

## 6. Graph & Real-Time WebSocket Protocol

### HTTP Snapshot: `GET /api/graph`
- **Query Params**: `page=1`, `limit=100`, `status=Completed|Pending|Reversed`
- **Response**:
```json
{
  "event_sequence_watermark": 12,
  "nodes": [
    { "id": 1, "name": "Alpha Corp", "type": "Department", "status": "Active" }
  ],
  "edges": [
    {
      "id": 101,
      "sender_id": 1,
      "receiver_id": 2,
      "amount_paise": 250000,
      "amount_formatted": "2500.00",
      "currency": "INR",
      "status": "Completed",
      "created_at": "2026-11-16 04:35:00"
    }
  ],
  "pagination": { "page": 1, "limit": 100, "total_edges": 1, "total_pages": 1 }
}
```

### WebSocket: `GET /ws/graph`
- **Authentication**: Sent during HTTP Upgrade handshake via `Cookie: session_token=...` and validated against MySQL session store.
- **Client Heartbeat**: Client transmits `{"type":"PING"}` every 25 seconds; server responds with `{"type":"PONG"}`.
- **Client Subscription & Replay**: Upon connection, client transmits `{"type":"SUBSCRIBE_GRAPH", "last_seen_sequence": N}`. The server automatically replays all committed outbox events having `event_sequence > N`.
- **Server Broadcasts**:
```json
{
  "event_type": "TRANSACTION_CREATED",
  "sequence": 13,
  "payload": {
    "transaction_id": 102,
    "organization_id": 1,
    "sender_entity_id": 1,
    "receiver_entity_id": 2,
    "amount": "1500.00",
    "currency": "INR",
    "status": "Pending",
    "created_at": "2026-11-16 04:36:00"
  }
}
```
