# SettleSmart 2.0 — Authentication and Authorization Architecture Specification

## 1. Executive Summary and Security Principles

The **SettleSmart 2.0** Authentication and Authorization subsystem provides a multi-tenant, zero-trust security infrastructure designed for organizations and enterprises handling high-volume financial settlements.

### Core Security Mandates
1. **Multi-Tenant Isolation**: Every user, session, entity, and transaction is strictly bound to an `organization_id`. The backend exclusively obtains the authenticated tenant identity from verified server-side session context, never trusting client-supplied organization claims.
2. **Cryptographic Hashing for Passwords**: Zero storage of plaintext or weakly hashed passwords. Passwords are protected using Argon2id with cryptographically secure random salts and memory-hard configuration parameters.
3. **Opaque Token and Stored Hash Pattern**: Raw session and CSRF tokens are ephemeral secrets known only to the user agent. The database stores strictly SHA-256 hashes of these tokens. An unauthorized database read access cannot compromise active user sessions.
4. **Defense in Depth Against Web Vulnerabilities**: CSRF token validation protects all state-changing operations; cookies are restricted via `HttpOnly`, `SameSite=Lax`, and `Path=/`; origin validation enforces an explicit allowlist without wildcard CORS on credentialed requests; and timing attacks are eliminated using constant-time string comparisons.
5. **Administrative Invariant Protection**: The system guarantees that an organization can never become orphaned or lock itself out by enforcing transaction-safe Last Active Administrator Protection under concurrent operations.

---

## 2. Multi-Tenant Organization Isolation

SettleSmart 2.0 employs a single shared-database, shared-process multi-tenant architecture with logical isolation enforced at the database engine and application service boundaries.

```
+-----------------------------------------------------------------------+
|                         HTTP REST CLIENT / BROWSER                    |
+-----------------------------------------------------------------------+
                                   |
         Cookie: session_token=<raw> | Header: X-CSRF-Token=<raw>
                                   v
+-----------------------------------------------------------------------+
|                    HTTP TRANSPORT LAYER (Mongoose)                    |
|   1. Configurable Origin Allowlist Validation                         |
|   2. Cookie Parsing & Session Extraction                              |
|   3. Preflight OPTIONS Handling & Exact-Origin CORS                   |
|   4. CSRF Validation on State-Changing Methods                        |
+-----------------------------------------------------------------------+
                                   |
              Verified SessionContext (org_id, user_id, role)
                                   v
+-----------------------------------------------------------------------+
|                     APPLICATION AUTH SERVICE LAYER                    |
|   1. RBAC Privilege Checks (Admin / Finance_Manager / Viewer)         |
|   2. Transaction-Safe Last Active Admin Invariant (FOR UPDATE lock)   |
|   3. Mutex-Synchronized In-Memory Rate Limiting                       |
|   4. Tenant-Scoped SQL Query Generation                               |
+-----------------------------------------------------------------------+
                                   |
        Authoritative Tenant Queries (WHERE organization_id = %lld)
                                   v
+-----------------------------------------------------------------------+
|                         MYSQL DATABASE LAYER                          |
|   1. Composite Foreign Keys: (organization_id, user_id)               |
|   2. Composite Unique Keys: (organization_id, email)                  |
|   3. InnoDB ACID Transactions & Row-Level Exclusive Locks             |
+-----------------------------------------------------------------------+
```

### Verified Server-Side Tenant Context
When an HTTP request is received:
1. The transport layer extracts the `session_token` cookie.
2. The session module hashes the token with SHA-256 and queries the `sessions` table joined against `users` and `organizations`.
3. The verified `organization_id`, `user_id`, and `role` are stored in the server-side `SessionInfo` struct.
4. Subsequent operations strictly execute queries scoped to `session.organization_id`. Any request referencing foreign tenant resources is immediately rejected.

---

## 3. Password Security and Argon2id Hashing

### 3.1 Algorithm and Configuration Parameters
SettleSmart 2.0 uses **Argon2id** (version `0x13` / 19), the hybrid variant of Argon2 combining Argon2i's resistance against side-channel cache attacks with Argon2d's resistance against GPU cracking.

| Parameter | Value | Rationale |
| :--- | :--- | :--- |
| **Algorithm** | Argon2id | OWASP and NIST recommended standard |
| **Time Cost (\(t\))** | 2 iterations | Ensures high computational barrier against brute-force |
| **Memory Cost (\(m\))** | 65,536 KiB (64 MB) | Memory-hard configuration rendering GPU/ASIC attacks cost-prohibitive |
| **Parallelism (\(p\))** | 1 thread | Optimized for concurrent backend server workloads |
| **Salt Length** | 32 bytes (256 bits) | Cryptographically secure random bytes generated via OS CSPRNG |
| **Output Hash Length**| 32 bytes (256 bits) | Collision-resistant cryptographic hash digest |
| **Encoding Format** | Standard Modular Crypt Format (`$argon2id$v=19$m=65536,t=2,p=1$...`) | Standardized format supporting parameter verification |

### 3.2 Password Policy
- **Minimum Length**: 12 characters.
- **Maximum Length**: 128 characters (prevents Denial of Service via overly large hash inputs).

### 3.3 Random Salt Generation
Salts are generated using OS-level cryptographically secure pseudo-random number generators (CSPRNG):
- **Windows**: `BCryptGenRandom(NULL, buf, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG)`
- **POSIX / Linux**: Direct read of `/dev/urandom`

---

## 4. Session Lifecycle and Management Architecture

### 4.1 Token Generation and Structure
Upon successful login:
1. **Raw Session Token**: 32 cryptographically secure random bytes, formatted as a 64-character lowercase hexadecimal string (256 bits of entropy).
2. **Raw CSRF Token**: 32 cryptographically secure random bytes, formatted as a 64-character lowercase hexadecimal string (256 bits of entropy).
3. **Token Hashing**:
   $$\text{token\_hash} = \text{SHA-256}(\text{raw\_session\_token})$$
   $$\text{csrf\_token\_hash} = \text{SHA-256}(\text{raw\_csrf\_token})$$
4. **Database Persistence**: The raw tokens are delivered once to the client (session token in `Set-Cookie`, CSRF token in the JSON login payload). Only `token_hash` and `csrf_token_hash` are inserted into the `sessions` table.

### 4.2 Expiration and Inactivity Policies
- **Absolute Lifetime**: 12 hours from creation (`expires_at = NOW() + INTERVAL 12 HOUR`). When `NOW() >= expires_at`, the session is permanently expired.
- **Inactivity Timeout (Sliding Window)**: 30 minutes (`TIMESTAMPDIFF(SECOND, last_seen_at, NOW()) > 1800`). If a user is idle for more than 30 minutes, the session is invalidated and marked revoked.
- **Activity Touch**: Every valid authenticated request touches `last_seen_at = NOW()`, sliding the 30-minute inactivity window forward up to the absolute 12-hour limit.

### 4.3 Revocation Mechanics
- **Individual Logout (`POST /api/auth/logout`)**: Sets `revoked_at = NOW()` for the session associated with the active token hash, and sends an expired `Set-Cookie` header.
- **All-Sessions Revocation (Password Change)**: When a user changes their password via `auth_change_password()`, all active sessions for that user across all devices are immediately revoked by setting `revoked_at = NOW()` where `user_id = %lld AND organization_id = %lld`.

---

## 5. Cookie Security, Origin Validation and CSRF Architecture

### 5.1 Cookie Security Attributes
The session token is delivered to the client inside a secure HTTP cookie:
```http
Set-Cookie: session_token=<raw_session_token>; Path=/; HttpOnly; SameSite=Lax; Secure
```
- **`HttpOnly`**: Mandatory in all environments. Prevents client-side JavaScript (`document.cookie`) from reading the session token, mitigating XSS token theft.
- **`Secure`**: Mandatory in production environments (`APP_ENV=production`). Transmitted strictly over HTTPS. Local HTTP development may omit `Secure` only when `APP_ENV=development` and explicitly configured. Production mode programmatically enforces `cookie_secure = 1` even if misconfigured in environment variables.
- **`SameSite=Lax`**: Restricts the cookie from being sent on cross-site state-changing requests, mitigating cross-site request forgery.
- **`Path=/`**: Restricts the cookie scope across the entire application API.

### 5.2 Origin Validation and CORS Allowlist
The server enforces a configurable comma-separated allowlist of trusted origins via `ALLOWED_ORIGINS` (e.g., `http://localhost:3000,https://app.settlesmart.com`):
1. **Preflight `OPTIONS` Requests**: Validates the `Origin` header. If approved, replies with `204 No Content`, echoes the exact origin in `Access-Control-Allow-Origin`, and includes `Access-Control-Allow-Credentials: true`. If unapproved, rejects with `403 Forbidden`.
2. **State-Changing Requests (`POST`, `PATCH`, `PUT`, `DELETE`)**: The `Origin` header is inspected on all incoming state-changing requests. Requests with missing or unapproved origins are immediately rejected with `403 Forbidden`.
3. **No Wildcard with Credentials**: In compliance with RFC 6454 and W3C Fetch specifications, `Access-Control-Allow-Origin: *` is strictly prohibited for credentialed requests. The exact matching approved origin is always echoed.

### 5.3 CSRF Token Delivery Architecture and Double-Submit Pattern
The system employs an opaque token pair pattern:
- **Session Token**: Transported exclusively via HTTP-only cookie to prevent JavaScript access.
- **CSRF Token**: Returned in the JSON body of the successful login response (`{"csrf_token": "..."}`). The frontend stores this token in memory (or React state) and attaches it to the custom HTTP header `X-CSRF-Token` for every state-changing request.
- **Validation**:
  1. The server hashes the header value: $H = \text{SHA-256}(\text{header\_value})$.
  2. The server compares $H$ against `session->csrf_token_hash` using `crypto_constant_time_equals()`.
  3. If missing, tampered, or mismatched, the request is rejected with `403 Forbidden`.
- **Rationale for Header vs Cookie**: Delivering the CSRF token in the response body rather than a readable cookie completely decouples CSRF defense from the browser's cookie jar and prevents subdomain cookie injection attacks.

### 5.4 CSRF Token Recovery Architecture (`POST /api/auth/csrf`)
When a user refreshes or re-opens their browser tab, the in-memory CSRF token held in JavaScript state is discarded while the `HttpOnly` session cookie persists in the browser. To recover a fresh CSRF token without requiring the user to re-enter their password:
1. The frontend issues a `POST /api/auth/csrf` request.
2. The server extracts the `session_token` cookie and verifies that the session is active and unexpired.
3. The server validates that the request `Origin` matches the configured allowlist.
4. The server calls `session_rotate_csrf_token()`, generating a fresh 256-bit random token and replacing `csrf_token_hash` in the database with its SHA-256 digest.
5. The server returns `{"success": true, "csrf_token": "<new_raw_token>"}` without exposing the session token.
6. The client stores the fresh token in memory for subsequent state-changing operations.
7. Any previously issued CSRF token for that session is invalidated immediately.

### 5.5 Public Registration Control
Public organization registration (`POST /api/auth/register-organization`) is controlled via the `ENABLE_PUBLIC_REGISTRATION` environment variable.
- In **development**, it defaults to enabled (`true`) to facilitate onboarding.
- In **production**, it defaults to disabled (`false`) to prevent unauthorized organization provisioning in enterprise environments. When disabled, registration attempts receive `403 Forbidden`.

---

## 6. Rate Limiting Architecture

To defend against brute-force password cracking and credential stuffing attacks, SettleSmart 2.0 incorporates a dedicated rate limiting mechanism:

### 6.1 In-Memory Architecture and Keying Strategy
- **Keying**: Failed attempts are tracked per `(organization_id, normalized_email)` pair.
- **Window and Threshold**: Maximum 5 failed attempts within a 15-minute sliding window.
- **Data Structure**: In-memory fixed-size hashtable (`MAX_RATE_LIMIT_ENTRIES = 1024`) with LRU/oldest entry eviction.
- **Thread Safety**: Synchronized via OS-level mutex primitives (`CRITICAL_SECTION` on Windows, `pthread_mutex_t` on POSIX).
- **Reset Policy**: Upon any successful authentication, the failed attempt counter for that key is immediately cleared to 0.

### 6.2 Multi-Instance Limitations and Production Scaling
In a multi-instance horizontally scaled deployment behind a load balancer:
- In-memory rate limiting is local to each backend process. An attacker rotating requests across instances could attempt \(5 \times N\) attempts before lockout unless sticky sessions are configured.
- **Production Recommendation**: In multi-instance deployments, the rate limiter should be backed by a centralized in-memory store (such as Redis) or persisted directly in MySQL with row-level atomic counters and expiration timestamps.

---

## 7. Role-Based Access Control (RBAC) Specification

SettleSmart 2.0 defines three non-overlapping user roles:

| Capability / Resource | `Admin` | `Finance_Manager` | `Viewer` |
| :--- | :---: | :---: | :---: |
| **Manage Users (Create, Deactivate, Update Role)** | Yes | No | No |
| **View Users within Organization** | Yes | No | No |
| **Create Transactions & Reversals (Step 5)** | Yes | Yes | No |
| **Execute Settlements (Step 6)** | Yes | Yes | No |
| **View Ledgers, Reports & Balances** | Yes | Yes | Yes |
| **Change Own Password** | Yes | Yes | Yes |

---

## 8. Concurrent Last Active Administrator Protection

### The Problem
If two administrators concurrently attempt to deactivate each other or if an administrator attempts simultaneous deactivations across multiple threads, an application-level check without database concurrency control could suffer a race condition, leaving an organization with zero active administrators.

### The Locking Strategy
To guarantee transactional safety under concurrent execution:
1. Begin InnoDB transaction: `START TRANSACTION;`
2. **Acquire Exclusive Row Lock on Organization**:
   ```sql
   SELECT id FROM organizations WHERE id = %lld FOR UPDATE;
   ```
   This locks the organization record exclusively. Any concurrent transaction attempting to modify users for the same organization will block at this statement until the first transaction commits or rolls back.
3. **Count Active Administrators within Locked Transaction**:
   ```sql
   SELECT COUNT(*) 
   FROM users 
   WHERE organization_id = %lld 
     AND role = 'Admin' 
     AND status = 'Active' 
     AND id <> %lld;
   ```
4. If the count is 0, rollback transaction and return error:
   `"Cannot deactivate the last active administrator of the organization"` (on deactivation) or
   `"Cannot demote the last active administrator of the organization"` (on role demotion).
5. If count \(\ge 1\), execute status/role update, commit transaction, and release the lock.

---

## 9. REST API Specification

### 1. Register Organization & Initial Administrator
- **Method**: `POST`
- **URI**: `/api/auth/register-organization`
- **Authentication**: None (Public, gated by `ENABLE_PUBLIC_REGISTRATION`)
- **Headers**: `Origin: <approved_origin>`
- **Request Body**:
  ```json
  {
    "organization_name": "Acme Global Financials",
    "admin_email": "admin@acmeglobal.com",
    "admin_password": "MasterSecurePassword#2026"
  }
  ```
- **Responses**:
  - `201 Created`: `{"success": true, "organization_id": 1, "admin_user_id": 1}`
  - `400 Bad Request`: `{"success": false, "error": "Password must be between 12 and 128 characters"}`
  - `403 Forbidden`: `{"success": false, "error": "Public registration is disabled"}`

### 2. Login
- **Method**: `POST`
- **URI**: `/api/auth/login`
- **Authentication**: None (Public)
- **Headers**: `Origin: <approved_origin>`
- **Request Body**:
  ```json
  {
    "organization_id": 1,
    "email": "admin@acmeglobal.com",
    "password": "MasterSecurePassword#2026"
  }
  ```
- **Responses**:
  - `200 OK`:
    - Headers: `Set-Cookie: session_token=<hex>; Path=/; HttpOnly; SameSite=Lax; [Secure]`
    - Body:
      ```json
      {
        "success": true,
        "csrf_token": "9a8b7c6d5e4f...",
        "user": {
          "id": 1,
          "organization_id": 1,
          "email": "admin@acmeglobal.com",
          "role": "Admin",
          "status": "Active"
        }
      }
      ```
  - `401 Unauthorized`: `{"success": false, "error": "Invalid organization ID, email or password"}`
  - `401 Unauthorized (Rate Limited)`: `{"success": false, "error": "Too many failed login attempts. Please try again later."}`

### 3. Recover CSRF Token
- **Method**: `POST`
- **URI**: `/api/auth/csrf`
- **Authentication**: Required (`Cookie: session_token=...`)
- **Headers**: `Origin: <approved_origin>`
- **Request Body**: None
- **Responses**:
  - `200 OK`:
    ```json
    {
      "success": true,
      "csrf_token": "a1b2c3d4e5f6..."
    }
    ```
  - `401 Unauthorized`: `{"success": false, "error": "Unauthorized or session expired"}`
  - `403 Forbidden`: `{"success": false, "error": "Origin not allowed"}`

### 4. Logout
- **Method**: `POST`
- **URI**: `/api/auth/logout`
- **Authentication**: Required (`Cookie: session_token=...`)
- **Headers**: `Origin: <approved_origin>`, `X-CSRF-Token: <raw_csrf_token>`
- **Responses**:
  - `200 OK`:
    - Headers: `Set-Cookie: session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; SameSite=Lax`
    - Body: `{"success": true, "message": "Logged out successfully"}`
  - `403 Forbidden`: `{"success": false, "error": "Invalid or missing CSRF token"}`

### 5. Current User Session (`Me`)
- **Method**: `GET`
- **URI**: `/api/auth/me`
- **Authentication**: Required (`Cookie: session_token=...`)
- **Responses**:
  - `200 OK`:
    ```json
    {
      "success": true,
      "user": {
        "id": 1,
        "organization_id": 1,
        "email": "admin@acmeglobal.com",
        "role": "Admin",
        "status": "Active"
      }
    }
    ```
  - `401 Unauthorized`: `{"success": false, "error": "Unauthorized or session expired"}`

### 6. Change Password
- **Method**: `POST`
- **URI**: `/api/auth/change-password`
- **Authentication**: Required (`Cookie: session_token=...`)
- **Headers**: `Origin: <approved_origin>`, `X-CSRF-Token: <raw_csrf_token>`
- **Request Body**:
  ```json
  {
    "old_password": "MasterSecurePassword#2026",
    "new_password": "NewMasterSecurePassword#2026"
  }
  ```
- **Responses**:
  - `200 OK`: `{"success": true, "message": "Password updated successfully"}`
  - `400 Bad Request`: `{"success": false, "error": "Current password does not match"}`

### 7. List Organization Users
- **Method**: `GET`
- **URI**: `/api/organization/users`
- **Authentication**: Required (`Cookie: session_token=...`, Role must be `Admin`)
- **Responses**:
  - `200 OK`:
    ```json
    {
      "success": true,
      "users": [
        {"id": 1, "email": "admin@acmeglobal.com", "role": "Admin", "status": "Active"},
        {"id": 2, "email": "finance@acmeglobal.com", "role": "Finance_Manager", "status": "Active"}
      ]
    }
    ```

### 8. Create Organization User
- **Method**: `POST`
- **URI**: `/api/organization/users`
- **Authentication**: Required (`Cookie: session_token=...`, Role must be `Admin`)
- **Headers**: `Origin: <approved_origin>`, `X-CSRF-Token: <raw_csrf_token>`
- **Request Body**:
  ```json
  {
    "email": "finance@acmeglobal.com",
    "password": "FinanceManagerPassword#2026",
    "role": "Finance_Manager"
  }
  ```
- **Responses**:
  - `201 Created`: `{"success": true, "user_id": 2}`
  - `403 Forbidden`: `{"success": false, "error": "Permission denied: Administrator role required to manage users"}`

### 9. Update User Status or Role
- **Method**: `PATCH`
- **URI**: `/api/organization/users/:id`
- **Authentication**: Required (`Cookie: session_token=...`, Role must be `Admin`)
- **Headers**: `Origin: <approved_origin>`, `X-CSRF-Token: <raw_csrf_token>`
- **Request Body**:
  ```json
  {
    "status": "Deactivated",
    "role": "Viewer"
  }
  ```
- **Responses**:
  - `200 OK`: `{"success": true}`
  - `400 Bad Request`: `{"success": false, "error": "Cannot deactivate the last active administrator of the organization"}`
  - `400 Bad Request`: `{"success": false, "error": "Cannot demote the last active administrator of the organization"}`
  - `403 Forbidden`: `{"success": false, "error": "Permission denied: Administrator role required to manage users"}`
  - `404 Not Found`: `{"success": false, "error": "User not found"}` (returned consistently for nonexistent or cross-organization target users, preventing resource enumeration)

---

## 10. Verification and Test Suite Results

All 12 test suites pass cleanly via `mingw32-make test`:

| Test Suite | Group / Purpose | Checks Verified | Status |
| :--- | :--- | :---: | :---: |
| `tests/env_loader_test` | Configuration file parsing and environment defaults | 9 | **PASSED** |
| `tests/db_connection_test` | MySQL connection, TLS verification, error handling | 6 | **PASSED** |
| `tests/db_dummy_table_test` | CRUD operations, transactions, rollbacks, DECIMAL precision | 9 | **PASSED** |
| `tests/db_integrity_test` | Composite foreign keys, entity & creator isolation (Tests A-D) | 10 | **PASSED** |
| `tests/auth_password_test` | Argon2id formatting, salt uniqueness, length validation | 7 | **PASSED** |
| `tests/auth_registration_test`| Atomic org + admin onboarding, rollback on failure | 7 | **PASSED** |
| `tests/auth_login_test` | Credential verification, rate limiting lockout and reset | 9 | **PASSED** |
| `tests/auth_session_test` | SHA-256 hash storage, 30m inactivity, 12h expiration, revocation | 9 | **PASSED** |
| `tests/auth_authorization_test`| RBAC permission matrix, tenant isolation, single-thread last admin | 11 | **PASSED** |
| `tests/auth_concurrent_admin_test`| Real concurrent threads testing simultaneous deactivations & demotions with row locking | 6 | **PASSED** |
| `tests/auth_csrf_test` | 5 origin scenarios, OPTIONS preflight, CSRF recovery, refresh simulation, cookie security | 20 | **PASSED** |
| `tests/auth_http_endpoints_test`| 8 endpoints, session revocation, consistent 404s, and HTTP RBAC role updates | 18 | **PASSED** |
| **TOTAL** | **Comprehensive Full System Verification** | **121** | **ALL PASSED** |
