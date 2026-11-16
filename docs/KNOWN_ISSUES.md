# KNOWN ISSUES AND ARCHITECTURAL NOTES

## Current Status (Step 10 — React Frontend & Final Integration Completed)
All 56 backend test suites pass with 100% success rate. The pure C backend server (`server.exe`) runs reliably on port 8000 with complete RESTful HTTP routing, multi-tab CSRF protection, transactional outbox polling, Max-Heap priority processing, and WebSocket streaming. The React + Vite + TypeScript frontend in `frontend/` builds with 0 errors and provides full end-to-end integration across all pages.

---

## 1. Backend & Multi-Instance Integration Notes
1. **Multi-Instance Outbox Cursor Tracking**:
   - Each backend instance operates with a distinct `instance_id` (e.g. `inst_8000`, `server_8000`) and maintains its local progress in the `outbox_consumer_cursors` table.
   - Financial balances applied during outbox replay are protected against double application by enforcing pending-status initial states on created transactions.
2. **Graph Snapshot Watermark Consistency**:
   - The graph snapshot API captures `event_sequence_watermark` within the same database read transaction as the node and edge snapshots. Clients reconnecting to `/ws/graph` must supply `last_seen_sequence` equal to the snapshot watermark to prevent missing or duplicated events.
3. **Dual-Slot Multi-Tab CSRF Validation**:
   - CSRF tokens are rotated on each state-changing request or on demand via `POST /api/auth/csrf`. To prevent race conditions when a user operates in multiple tabs simultaneously, the backend session store maintains both `csrf_token_hash` and `prev_csrf_token_hash`. The frontend API client automatically refreshes tokens upon 403 CSRF mismatch.

---

## 2. Frontend Operational Notes
1. **Advisory Settlement Execution**:
   - Settlement recommendations computed via the Greedy algorithm are strictly advisory. The frontend presents clear advisory notices and deliberately avoids automatically executing settlements to adhere to financial compliance and human authorization principles.
2. **Paise-to-Rupee Currency Conversion**:
   - All financial amounts remain authoritatively integer paise in MySQL. The frontend never performs floating-point arithmetic for balances; all display formatting uses decimal-safe string manipulation via `formatPaiseToINR`.
3. **Local Development Port Alignment**:
   - The backend enforces CORS against `ALLOWED_ORIGIN`. For local development, `http://localhost:3000` is pre-configured in `.env`. Ensure that the frontend dev server runs on port 3000 (configured in `vite.config.ts`).
