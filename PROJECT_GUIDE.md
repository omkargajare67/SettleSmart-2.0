# SettleSmart 2.0 — Central Project Guide

## Overview and Purpose of this Document

`PROJECT_GUIDE.md` is the central engineering guide for SettleSmart 2.0. As the application develops through all 10 milestones, this document records the rationale, responsibilities, and structural fit of every file and major module in the repository.

Instead of repeating internal line-by-line code logic, this guide explains:
1. **WHY** each file or directory exists.
2. **WHAT** responsibilities it carries.
3. **WHY** its major sections or structures are essential.
4. **HOW** it integrates into the complete multi-tier SettleSmart 2.0 architecture.

---

## Directory Structure Map (After Step 8)

```
SettleSmart-2.0/
├── backend/
│   ├── src/
│   │   ├── auth/                      # Authentication, password hashing, sessions & RBAC
│   │   ├── config/                    # Configuration and .env loader
│   │   ├── database/                  # MySQL C API wrapper and transactional helpers
│   │   ├── entities/                  # Entity domain model, CRUD service & search
│   │   ├── transactions/              # Transaction model, idempotency, service & processor (Step 8)
│   │   │   ├── transaction.h / .c     # Transaction core validation & parsing
│   │   │   ├── transaction_service.h/.c# Transaction CRUD and reversal insertion
│   │   │   └── transaction_processor.h/.c# Central processing engine & state transitions (Step 8)
│   │   ├── dsa/                       # In-memory Core Data Structures (Step 6)
│   │   │   ├── linked_list.h / .c     # Bounded Singly Linked List
│   │   │   ├── hashmap.h / .c         # HashMap with separate chaining for balances
│   │   │   ├── queue.h / .c           # FIFO Queue for chronological transactions
│   │   │   ├── stack.h / .c           # LIFO Stack for recent activity inspection
│   │   │   ├── priority_queue.h / .c  # Max-Heap Priority Queue for high-value monitoring
│   │   │   └── dsa_state.h / .c       # Organization-scoped state container & recovery
│   │   ├── graph/                     # Directed Weighted Graph & snapshot service (Step 7)
│   │   │   ├── graph.h / .c           # Adjacency list, parallel edges, bounded eviction
│   │   │   └── graph_service.h / .c   # MySQL recovery & JSON snapshot generation
│   │   ├── websocket/                 # Real-time WebSocket & event outbox (Step 7)
│   │   │   ├── event_dispatcher.h / .c# Transactional outbox persistence & event builders
│   │   │   └── websocket.h / .c       # WebSocket connection registry, auth & replay
│   │   ├── algorithms/                # Financial and data processing algorithms (Step 8)
│   │   │   ├── bubble_sort.h / .c     # In-memory stable sorting by amount, timestamp, ID
│   │   │   ├── binary_search.h / .c   # O(log n) random-access lookup for transactions & entities
│   │   │   └── settlement.h / .c      # Greedy settlement recommendation algorithm
│   │   └── http/                      # REST API routing, CORS, CSRF, graph & entity routes
│   │       ├── http_server.h / .c     # HTTP server engine & WebSocket integration
│   │       ├── entity_routes.h / .c   # REST handlers for entity endpoints
│   │       ├── transaction_routes.h/.c# REST handlers for transaction endpoints
│   │       ├── graph_routes.h / .c    # REST handlers for GET /api/graph snapshot
│   │       └── processing_routes.h/.c # REST handlers for processing, priority & settlements (Step 8)
│   ├── tests/                         # 50 Comprehensive C test suites (933+ checks)
│   ├── third_party/                   # Vendored dependencies (Argon2, Mongoose)
│   └── Makefile                       # Build automation script (MinGW-w64 64-bit)
├── frontend/                          # Dedicated workspace for React web client (Step 10)
├── database/                          # MySQL DDL schema and migrations
│   ├── schema.sql                     # Authoritative MySQL physical DDL schema
│   └── migrations/                    # Database migration scripts (003, 004, 005, 006)
└── docs/                              # System documentation & technical specifications
```

---

## Step 2 Files and Modules Breakdown

### 1. `backend/src/config/env_loader.h`
- **WHY it is being added**:
  Provides a clean header interface for reading application configuration without hardcoding database credentials or system ports anywhere in the backend source code.
- **WHAT responsibility it handles**:
  Exposes the core configuration functions: `load_env_file()`, `get_env_value()`, and `get_env_int()`.
- **WHY its major sections are necessary**:
  - `load_env_file(filepath)`: Allows the application to load a specific configuration file on startup.
  - `get_env_value(key, default_value)`: Safely fetches string settings with fallback protection.
  - `get_env_int(key, default_value)`: Parses integer configurations safely.
- **HOW it fits into SettleSmart 2.0**:
  Every backend module needing network addresses, database credentials, or secret keys depends on this header rather than hardcoded definitions.

---

### 2. `backend/src/config/env_loader.c`
- **WHY it is being added**:
  Standard C runtimes do not automatically parse `.env` files. This file implements the complete cross-platform environment-loading logic for Windows and Linux.
- **WHAT responsibility it handles**:
  Parses `.env` files line by line, removes whitespace and quotes, ignores comments and blank lines, populates the process environment, and respects existing system environment variables.
- **WHY its major sections are necessary**:
  - *Whitespace trimming and quote stripping*: Normalizes user inputs so trailing spaces or surrounding quotes do not corrupt connection strings.
  - *Pre-existing variable check*: Ensures cloud environments (Docker, Kubernetes) can inject system variables that take precedence over local files.
  - *Cross-platform environment setting*: Utilizes `_putenv_s()` on Windows and `setenv()` on POSIX platforms.
  - *Directory fallback*: If `.env` is not in the immediate working directory, falls back to `../.env` so tests can run cleanly from both root and subdirectories.
- **HOW it fits into SettleSmart 2.0**:
  Runs at the very beginning of application startup, ensuring that the database client and server routers have valid parameters before initiating network connections.

---

### 3. `backend/src/database/db_client.h`
- **WHY it is being added**:
  Defines the authoritative MySQL connection and query interface for the C backend, encapsulating the raw MySQL C API within straightforward, modular functions.
- **WHAT responsibility it handles**:
  Declares the `DBClient` state structure, the `DBConfig` structure, connection lifecycle functions, query runners, transaction controllers, prepared statement wrappers, and SSL mode options.
- **WHY its major sections are necessary**:
  - `DBClient` & `DBConfig`: Holds the connection pointer, SSL configuration, and an isolated error buffer so functions can return descriptive messages without exposing sensitive passwords.
  - *Connection and Ping functions*: Manages connecting, closing, and verifying live connectivity.
  - *Transaction functions (`db_client_begin`, `db_client_commit`, `db_client_rollback`)*: Provides explicit transaction boundaries for financial atomic operations.
  - *Prepared Statement API (`db_client_prepare`, `db_client_execute_stmt`)*: Prevents SQL injection when handling dynamic input values.
- **HOW it fits into SettleSmart 2.0**:
  Serves as the universal database adapter used by all future backend services (authentication in Step 3, entities in Step 4, and transactions in Step 5).

---

### 4. `backend/src/database/db_client.c`
- **WHY it is being added**:
  Implements the database operations declared in `db_client.h` using standard MySQL C API calls.
- **WHAT responsibility it handles**:
  Initializes MySQL handles, configures client TLS options via `MYSQL_OPT_SSL_MODE`, establishes authenticated TCP connections, configures UTC session timezone, executes queries, manages result sets, controls transactions, and executes prepared statements.
- **WHY its major sections are necessary**:
  - *TLS configuration*: Maps `config->ssl_mode` to `MYSQL_OPT_SSL_MODE` (`SSL_MODE_PREFERRED`, `SSL_MODE_REQUIRED`, etc.) to enforce encrypted transport.
  - *Connection establishment with UTC setting*: Executes `SET time_zone = '+00:00'` immediately upon connecting to guarantee consistent global financial timestamps.
  - *Error handling without secrets*: Extracts MySQL error strings into `client->last_error` while keeping passwords out of output streams.
  - *Atomic transaction methods*: Maps directly to SQL commands (`START TRANSACTION`, `COMMIT`, `ROLLBACK`) for ACID compliance.
  - *Last insert ID retrieval*: Calls `mysql_insert_id()` to capture auto-generated primary keys without secondary queries.
- **HOW it fits into SettleSmart 2.0**:
  Connects the high-level C application logic directly to the MySQL persistent storage layer.

---

### 5. `database/schema.sql`
- **WHY it is being added**:
  Provides the physical, authoritative MySQL schema definition for the foundational SettleSmart 2.0 platform.
- **WHAT responsibility it handles**:
  Creates the four core relational tables: `organizations`, `users`, `entities`, and `transactions` with exact data types, constraints, and indexes.
- **WHY its major sections are necessary**:
  - `organizations`: Establishes tenant identities with auto-generated primary keys.
  - `users`: Stores organization-scoped accounts with unique email constraints per organization and a composite unique key `(organization_id, id)`.
  - `entities`: Stores departments, vendors, and clients, with a composite unique key `(organization_id, id)` enabling composite foreign key validation.
  - `transactions`: Stores immutable financial ledger records with exact `DECIMAL(15, 2)` amounts, composite foreign keys `(organization_id, sender_entity_id)`, `(organization_id, receiver_entity_id)`, `(organization_id, created_by_user_id)`, and `(reversal_of_id, organization_id)`.
- **HOW it fits into SettleSmart 2.0**:
  Acts as the foundational database blueprint instantiated in MySQL, ensuring the database engine itself guarantees multi-tenant isolation and financial consistency.

---

### 6. `backend/tests/env_loader_test.c`
- **WHY it is being added**:
  Implements the dummy-testing-first methodology for the configuration loader before integrating it with database code.
- **WHAT responsibility it handles**:
  Tests valid parsing, quote stripping, whitespace handling, missing files, integer conversion, and pre-existing environment variable precedence.
- **HOW it fits into SettleSmart 2.0**:
  Guarantees that the configuration module is robust and free of memory errors or parsing bugs.

---

### 7. `backend/tests/db_connection_test.c`
- **WHY it is being added**:
  Verifies actual connectivity between the C backend and a live MySQL instance before writing table operations.
- **WHAT responsibility it handles**:
  Connects using loaded configuration, executes `SELECT 1`, reads MySQL version, reports active TLS cipher, cleanly closes, and tests error handling for wrong passwords, bad hostnames, and unavailable ports.
- **HOW it fits into SettleSmart 2.0**:
  Confirms that the MySQL C API, client libraries, network sockets, credentials, and TLS cipher negotiation function properly together.

---

### 8. `backend/tests/db_dummy_table_test.c`
- **WHY it is being added**:
  Tests all fundamental database operations against an isolated temporary table (`test_financial_records`) before deploying the production application schema.
- **WHAT responsibility it handles**:
  Verifies insertion, retrieval, updates, deletions, auto-increment ID generation, prepared statements with bound parameters, transaction commit, transaction rollback, and exact `DECIMAL(15, 2)` retrieval without floating-point distortion.
- **HOW it fits into SettleSmart 2.0**:
  Validates that every database primitive required by future steps operates with strict financial accuracy and ACID integrity.

---

### 9. `backend/tests/db_integrity_test.c`
- **WHY it is being added**:
  Stress-tests the physical application schema against multi-tenant isolation rules, composite foreign keys, and idempotency constraints.
- **WHAT responsibility it handles**:
  Runs the full integrity test suite:
  1. Automatic ID generation for organizations and entities.
  2. Structural rejection of cross-organization entities (`fk_trans_receiver_comp`).
  3. Rejection of duplicate idempotency keys within an organization.
  4. Exact `DECIMAL(15, 2)` financial precision verification.
  5. Foreign key rejection of nonexistent entities.
  6. **TEST A**: Cross-organization creator user rejection (`fk_trans_user_comp`).
  7. **TEST B**: Cross-organization reversal reference rejection (`fk_trans_reversal_comp`).
  8. **TEST C**: Valid transaction with same-organization creator user.
  9. **TEST D**: Valid reversal transaction referencing an original transaction within the same organization.
  10. **TEST 7**: Verification that rolled-back transactions do not persist.
- **HOW it fits into SettleSmart 2.0**:
  Provides complete proof that the database layer guarantees tenant isolation, idempotency, and financial integrity prior to implementing business logic in Step 3.

---

### 10. `backend/Makefile`
- **WHY it is being added**:
  Provides clean, automated compilation and testing for both Windows (MinGW-w64) and Linux/cloud environments without complex build frameworks.
- **WHAT responsibility it handles**:
  Defines build targets for all test executables, links the MySQL client library (`-llibmysql` on Windows or `-lmysqlclient` on Linux), and provides `make test` and `make clean`.
- **HOW it fits into SettleSmart 2.0**:
  Enables any developer or automated pipeline to build and verify the C backend with a single command.

---

## Step 3 Files and Modules Breakdown (Authentication & Organization Management)

### 11. `backend/src/auth/crypto_utils.h` and `crypto_utils.c`
- **WHY it was necessary**:
  Standard C standard library functions like `rand()` and `strcmp()` are cryptographically insecure, vulnerable to predictability and timing attacks. This module provides cryptographically secure primitives.
- **WHAT responsibilities it handles**:
  - `crypto_random_bytes()`: Generates CSPRNG random bytes via OS-native APIs (`BCryptGenRandom` on Windows, `/dev/urandom` on POSIX).
  - `crypto_to_hex()`: Formats byte arrays to lowercase hex digests.
  - `crypto_generate_token()`: Produces 64-character high-entropy hex tokens for session and CSRF identifiers.
  - `crypto_sha256_hex()`: Computes deterministic SHA-256 digests using Mongoose's internal cryptographic hashing implementation.
  - `crypto_constant_time_equals()`: Compares authentication strings in constant time (\(O(N)\) independent of mismatch position) to eliminate side-channel timing attacks.
- **WHY this approach was chosen**:
  Leverages native OS CSPRNG for zero-dependency portability and cryptographic soundness while avoiding third-party random number libraries.
- **HOW it fits into SettleSmart 2.0**:
  Forms the foundational security bedrock supporting password salts, session token generation, token storage hashing, and CSRF header validation.

---

### 12. `backend/src/auth/password.h` and `password.c`
- **WHY it was necessary**:
  Legacy password algorithms (MD5, SHA-256) are vulnerable to GPU and ASIC brute-force attacks. SettleSmart 2.0 requires modern, memory-hard password hashing.
- **WHAT responsibilities it handles**:
  - `auth_validate_password_length()`: Enforces a strict password length policy between 12 and 128 characters.
  - `auth_hash_password()`: Hashes plaintext passwords using Argon2id with 32-byte unique random salts, 2 iterations (`t=2`), 64 MB memory cost (`m=65536`), and 1 thread (`p=1`).
  - `auth_verify_password()`: Verifies candidate passwords against stored standard `$argon2id$` encoded strings.
- **WHY this approach was chosen**:
  Argon2id is the winner of the Password Hashing Competition (PHC) and the OWASP/NIST gold standard, offering hybrid resistance against both side-channel and GPU-based attacks.
- **HOW it fits into SettleSmart 2.0**:
  Protects all user credentials in the `users` table across all organizations.

---

### 13. `backend/src/auth/authorization.h` and `authorization.c`
- **WHY it was necessary**:
  Multi-tenant enterprise platforms require centralized role-based access control (RBAC) and structural safeguards preventing administrative lockout.
- **WHAT responsibilities it handles**:
  - Defines the three platform roles: `Admin`, `Finance_Manager`, and `Viewer`.
  - Maps role strings to strongly-typed enum values.
  - Exposes privilege check predicates: `auth_can_manage_users()`, `auth_can_create_transactions()`, and `auth_can_view_data()`.
  - Implements the **Last Active Administrator Protection** query (`auth_check_is_last_active_admin()`): verifies whether any other active administrators exist before allowing deactivation or role demotion.
- **WHY this approach was chosen**:
  Centralizing authorization logic in a dedicated module avoids scattered inline checks and ensures identical security enforcement across CLI, internal C services, and REST endpoints.
- **HOW it fits into SettleSmart 2.0**:
  Governs all administrative endpoints, user management flows, and will govern transaction creation permissions in Step 5.

---

### 14. `backend/src/auth/session.h` and `session.c`
- **WHY it was necessary**:
  Stateful sessions must securely track logged-in users while enforcing tenant boundaries and preventing session hijacking.
- **WHAT responsibilities it handles**:
  - `session_create()`: Generates random session and CSRF tokens, hashes both with SHA-256, and stores only the hashes in the MySQL `sessions` table with an absolute 12-hour expiration.
  - `session_validate()`: Hashes the incoming raw session token, checks for revocation and tenant status, validates the 30-minute inactivity window, and updates `last_seen_at` on active requests.
  - `session_validate_csrf()`: Validates request CSRF tokens against the session's stored hash using constant-time comparison.
  - `session_rotate_csrf_token()`: Generates a fresh random CSRF token, hashes it with SHA-256, updates the session's hash in MySQL, and returns the raw token for client recovery without exposing the session token.
  - `session_revoke()` & `session_revoke_all_user()`: Revokes individual sessions (logout) or all active sessions for a user (password change).
- **WHY this approach was chosen**:
  Storing only SHA-256 hashes in the database ensures that a read-only database leak does not expose active session tokens or allow attacker impersonation. Sliding 30-minute inactivity combined with a 12-hour hard limit strictly confines session exposure windows.
- **HOW it fits into SettleSmart 2.0**:
  Authenticates every incoming HTTP request, providing the verified server-side `organization_id`, `user_id`, and `role` context.

---

### 15. `backend/src/auth/auth_service.h` and `auth_service.c`
- **WHY it was necessary**:
  Encapsulates core business workflows for multi-tenant onboarding, credential verification, rate limiting, and administrator user CRUD.
- **WHAT responsibilities it handles**:
  - `auth_register_organization()`: Executes an atomic MySQL transaction that provisions the organization and its initial active `Admin` user with rollback on any failure.
  - `auth_login()`: Authenticates users using `organization_id`, `email`, and `password`. Returns generic error messages on any failure.
  - Failed attempt rate limiting: Blocks brute-force attacks after 5 consecutive failures within a 15-minute window.
  - `auth_change_password()`: Validates current credentials, updates the password hash, and revokes all active sessions for that user.
  - `auth_create_user()`, `auth_update_user_status()`, `auth_update_user_role()`: Admin-scoped user management enforcing organization isolation and last active admin protection.
- **WHY this approach was chosen**:
  Keeping business logic decoupled from the HTTP transport layer allows the exact same authentication service to be used in headless scripts, background workers, or REST handlers.
- **HOW it fits into SettleSmart 2.0**:
  Coordinates between the database layer, password hasher, session manager, and HTTP server.

---

### 16. `backend/src/http/http_server.h` and `http_server.c`
- **WHY it was necessary**:
  Provides the REST API transport layer connecting web clients to the C backend services.
- **WHAT responsibilities it handles**:
  - Manages Mongoose HTTP event loop and TCP listeners.
  - Enforces CORS policies, handling OPTIONS preflights and setting `Access-Control-Allow-Origin` and `Access-Control-Allow-Credentials`.
  - Parses incoming `Cookie: session_token=...` headers.
  - Formats outgoing `Set-Cookie` headers with `HttpOnly; SameSite=Lax; Path=/`.
  - Enforces CSRF token header (`X-CSRF-Token`) on all state-changing methods (`POST`, `PATCH`, `DELETE`).
  - Routes requests to `/api/auth/register-organization`, `/api/auth/login`, `/api/auth/csrf` (token recovery), `/api/auth/logout`, `/api/auth/me`, `/api/auth/change-password`, and `/api/organization/users`.
  - Implements consistent 404 responses for cross-organization and nonexistent target users to prevent resource enumeration.
- **WHY this approach was chosen**:
  Mongoose provides a single-file, event-driven HTTP architecture in C with integrated JSON parsing and zero external web framework overhead.
- **HOW it fits into SettleSmart 2.0**:
  Serves as the gateway for the future Step 10 React frontend and external API clients.

---

### 17. `database/migrations/003_authentication.sql`
- **WHY it was necessary**:
  Provides a reproducible, auditable database migration creating the `sessions` table with multi-tenant foreign keys and index acceleration.
- **WHAT responsibilities it handles**:
  - Creates the `sessions` table storing `token_hash` (CHAR 64 / VARCHAR 64), `csrf_token_hash`, `expires_at`, `last_seen_at`, and `revoked_at`.
  - Establishes composite foreign key `(organization_id, user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT`.
  - Defines composite index `(organization_id, user_id)` and expiration index `(expires_at)`.
- **HOW it fits into SettleSmart 2.0**:
  Enables clean database schema versioning across development, test, and production environments.

---

### 18. Test Suites (Groups A through I & Security Review)
- **`backend/tests/auth_password_test.c`**: Verifies Group B (Argon2id format, salt uniqueness, password verification, length boundaries).
- **`backend/tests/auth_registration_test.c`**: Verifies Groups A & H (Atomic onboarding transaction, Admin role assignment, invalid parameter rejection, duplicate name rejection, rollback consistency).
- **`backend/tests/auth_login_test.c`**: Verifies Group C (Valid credentials, generic error responses, cross-organization login prevention, deactivated user rejection, suspended organization rejection, rate limit lockout and reset).
- **`backend/tests/auth_session_test.c`**: Verifies Group D (SHA-256 hash storage verification, 30-min inactivity timeout, 12-hour expiration, session touch, single-session revocation, all-sessions revocation on password change).
- **`backend/tests/auth_authorization_test.c`**: Verifies Groups E, F, & G (Strict tenant isolation, Admin vs Finance Manager vs Viewer permissions, last active admin protection preventing self-lockout or demotion).
- **`backend/tests/auth_concurrent_admin_test.c`**: Spawns concurrent threads executing simultaneous deactivations and simultaneous demotions against two administrators in the same organization. Verifies that InnoDB row locking (`SELECT ... FOR UPDATE`) guarantees exactly one deactivation/demotion succeeds and the other is safely rejected, leaving exactly one active administrator in both cases.
- **`backend/tests/auth_csrf_test.c`**: Verifies Group I & HTTP Security (5 Origin validation scenarios including approved dev, approved prod HTTPS, unapproved prod, missing origin, and invalid origin; OPTIONS preflights; CSRF token recovery via `POST /api/auth/csrf` with simulated refresh; rejection of discarded old tokens; production Secure cookie enforcement; and public registration gating).
- **`backend/tests/auth_http_endpoints_test.c`**: Full end-to-end integration test exercising all REST endpoints over live network sockets: `/register-organization`, `/login`, `/me`, `GET/POST /organization/users`, `PATCH /organization/users/:id`, `/change-password`, and `/logout`, verifying cookie passing, header handling, session revocation, consistent 404 responses on inaccessible target records, role updates by Admin, and rejection of role updates by Finance Manager or Viewer.

---

### 19. `database/migrations/004_entity_management.sql`
- **WHY it was necessary**:
  Provides a reproducible, atomic database migration updating the `entities` table to support active/inactive status lifecycle, audit trail attribution, and organization-scoped foreign key references to the `users` table.
- **WHAT responsibilities it handles**:
  - Modifies `status` to `ENUM('Active', 'Inactive') NOT NULL DEFAULT 'Active'`.
  - Adds audit columns `created_by_user_id BIGINT NULL`, `updated_by_user_id BIGINT NULL`, and `updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP`.
  - Establishes composite foreign keys:
    - `CONSTRAINT fk_entities_created_by FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT`
    - `CONSTRAINT fk_entities_updated_by FOREIGN KEY (organization_id, updated_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT`
- **HOW it fits into SettleSmart 2.0**:
  Ensures database engine-enforced integrity so that entity creators and modifiers must belong to the exact same organization as the entity itself.

---

### 20. `backend/src/entities/entity.h` & `backend/src/entities/entity.c`
- **WHY it was necessary**:
  Defines the core data structures, enumerated types, and validation rules for financial entities.
- **WHAT responsibilities it handles**:
  - Defines `EntityType` enum (`Department`, `Vendor`, `Subsidiary`, `Client`, `Partner`).
  - Defines `EntityStatus` enum (`Active`, `Inactive`).
  - Defines the `Entity` struct encapsulating record ID, organization ID, name, type, status, audit user IDs, and timestamps.
  - Implements bi-directional string conversion functions (`entity_type_to_string`, `entity_type_from_string`, `entity_status_to_string`, `entity_status_from_string`).
  - Implements input validation rules: whitespace trimming, 1-150 character bounds checking, and enum range verification.
- **HOW it fits into SettleSmart 2.0**:
  Acts as the foundational data model for entities utilized across the service layer, HTTP routes, and downstream transaction and graph subsystems.

---

### 21. `backend/src/entities/entity_service.h` & `backend/src/entities/entity_service.c`
- **WHY it was necessary**:
  Implements the core business logic and SQL database operations for entity management, isolated from HTTP transport details.
- **WHAT responsibilities it handles**:
  - `entity_service_create`: Inserts new entity records using MySQL `AUTO_INCREMENT` IDs, validates input parameters, escapes strings, maps MySQL error `1062` to duplicate conflict (`ENTITY_ERR_CONFLICT`), and fetches the created record.
  - `entity_service_get_by_id`: Retrieves an entity by ID scoped strictly by `organization_id`. Returns `ENTITY_ERR_NOT_FOUND` if nonexistent or belonging to another organization.
  - `entity_service_list`: Executes dynamic parameterized queries with count calculations, pagination (`page`, `limit`), case-insensitive name/numeric ID search, entity type filter, status filter, and allowlisted column sorting (`id`, `name`, `created_at` ASC/DESC).
  - `entity_service_update`: Updates entity name and/or entity type, recording the modifying user ID, while rejecting duplicate name collisions with `ENTITY_ERR_CONFLICT`.
  - `entity_service_update_status`: Toggles entity status between `Active` and `Inactive`, recording the modifying user ID.
- **HOW it fits into SettleSmart 2.0**:
  Encapsulates all database interactions for entities, ensuring tenant isolation and SQL injection protection.

---

### 22. `backend/src/http/entity_routes.h` & `backend/src/http/entity_routes.c`
- **WHY it was necessary**:
  Connects REST API requests to the entity service layer with strict authentication, CSRF validation, origin checking, and role-based authorization.
- **WHAT responsibilities it handles**:
  - `POST /api/entities`: Validates active session, verifies Admin or Finance Manager role, enforces CSRF token and Origin, creates entity, and returns 201 Created or 409 Conflict.
  - `GET /api/entities`: Validates session (accessible to Admin, Finance Manager, and Viewer), parses query string parameters via `mg_http_get_var`, and returns 200 OK with data array and pagination metadata.
  - `GET /api/entities/:id`: Validates session (all roles), fetches entity, and returns 200 OK or 404 Not Found.
  - `PATCH /api/entities/:id`: Validates Admin or Finance Manager role and CSRF, updates details, and returns 200 OK, 404 Not Found, or 409 Conflict.
  - `PATCH /api/entities/:id/status`: Validates Admin or Finance Manager role and CSRF, updates status, and returns 200 OK or 404 Not Found.
  - Prohibits physical deletion: Rejects `DELETE /api/entities/:id` with 405 Method Not Allowed.
- **HOW it fits into SettleSmart 2.0**:
  Exposes the entity management API consumed by clients while upholding all security and isolation invariants.

---

### 23. Step 4 Entity Management Test Suites (132 checks)
- **`backend/tests/entity_validation_test.c`** (36 checks): Validates name trimming, whitespace rejection, 1-150 character bounds, enum mappings, and status validations.
- **`backend/tests/entity_crud_test.c`** (32 checks): Tests automatic ID generation, sequential auto-incrementing, intra-org unique name conflicts, inter-org name reuse independence, retrieval by ID, cross-tenant isolation, detail updates, status toggling, and input validation.
- **`backend/tests/entity_pagination_test.c`** (27 checks): Tests pagination math across 15 seeded entities, page boundaries, substring searches, numeric ID searches, type filtering, status filtering, and ASC/DESC sorting.
- **`backend/tests/entity_concurrency_test.c`** (3 checks): Launches 4 concurrent threads with independent database connections attempting identical entity creation simultaneously. Verifies exactly 1 thread succeeds and 3 receive conflict errors, resulting in exactly 1 record in MySQL.
- **`backend/tests/entity_http_test.c`** (34 checks): Exercises HTTP endpoints over loopback TCP sockets: verifies role permissions (Admin and Finance Manager can create/modify; Viewer is read-only and rejected with 403), CSRF enforcement, cross-tenant 404 isolation, soft deactivation, and physical deletion rejection (405 Method Not Allowed).

---

---

## Step 5 Files and Modules Breakdown (Transaction Management and Idempotency)

### 25. `database/migrations/005_transaction_management.sql`
- **WHY it was necessary**:
  Upgrades the database schema for transaction lifecycle tracking, compensation reversals, and multi-tenant idempotency protection.
- **WHAT responsibilities it handles**:
  - Updates `status` enum to include `Pending`, `Completed`, `Reversed`, `Disputed` (default: `Pending`).
  - Adds `uq_trans_reversal_single` unique index on `(organization_id, reversal_of_id)`.
  - Adds performance indexes `idx_trans_org_status` and `idx_trans_org_amount`.
- **HOW it fits into SettleSmart 2.0**:
  Ensures MySQL acts as the permanent, authoritative data store for all transaction records.

---

### 26. `backend/src/transactions/transaction.h` & `backend/src/transactions/transaction.c`
- **WHY it was necessary**:
  Defines the fundamental data models, financial precision routines, and validation logic for financial transactions.
- **WHAT responsibilities it handles**:
  - `TransactionStatus` & `TransactionType` enums.
  - `Transaction` struct containing `int64_t amount_paise`, `amount_str`, `idempotency_key`, `request_fingerprint`, `reversal_of_id`, etc.
  - `transaction_parse_amount`: Parses string amounts into integer paise and normalized decimal strings (`%lld.%02lld`) with zero floating-point arithmetic.
  - `transaction_validate_currency`: Strictly enforces currency support (`INR`).
  - `transaction_validate_idempotency_key`: Validates UUID v4 formatting (36 characters, dashes at 8, 13, 18, 23, version '4', variant '8'/'9'/'a'/'b').
  - `transaction_compute_fingerprint`: Computes deterministic SHA-256 fingerprint over `org:sender:receiver:amount:currency:type`.
- **HOW it fits into SettleSmart 2.0**:
  Provides strict financial precision and validation for all transactions.

---

### 27. `backend/src/transactions/transaction_service.h` & `backend/src/transactions/transaction_service.c`
- **WHY it was necessary**:
  Implements core transaction ingestion, multi-tenant idempotency check/replay, deadlock-free row locking, dynamic history search, and compensatory reversals.
- **WHAT responsibilities it handles**:
  - `transaction_service_create`: Implements the pre-insert idempotency lookup, deadlock-free entity row locking in primary key order (`ORDER BY id FOR UPDATE`), active entity checks, parameterized INSERT, auto-increment ID retrieval, and race-condition recovery.
  - `transaction_service_get_by_id`: Organization-scoped lookup returning `TRANSACTION_ERR_NOT_FOUND` on cross-tenant access.
  - `transaction_service_list`: Dynamic query builder supporting pagination (`page`, `limit`), ID search, participant entity filter (`incoming`/`outgoing`), date range, amount range, status, and allowlisted sorting (`id`, `amount`, `created_at` ASC/DESC with `id DESC` tie-breaker).
  - `transaction_service_get_recent`: Retrieves latest transactions sorted by `created_at DESC, id DESC`.
  - `transaction_service_create_reversal`: Validates original transaction is `Completed`, checks idempotency replay, verifies no prior reversal exists, swaps sender and receiver entities, and records reversal with `reversal_of_id`.
- **HOW it fits into SettleSmart 2.0**:
  Houses the complete financial business logic and database transaction management.

---

### 28. `backend/src/http/transaction_routes.h` & `backend/src/http/transaction_routes.c`
- **WHY it was necessary**:
  Provides the HTTP transport layer for transaction endpoints with RBAC, CSRF, origin check, and idempotency header validation.
- **WHAT responsibilities it handles**:
  - `POST /api/transactions`: Requires Admin or Finance Manager role, CSRF token, and UUID v4 `Idempotency-Key` header. Returns `201 Created` for first insert or `200 OK` for idempotent replay (`"idempotent_replay": true`). Rejects payload mismatch with `409 Conflict`.
  - `GET /api/transactions`: Accessible to Admin, Finance Manager, and Viewer. Parses query parameters, executes paginated search, and returns JSON array with metadata.
  - `GET /api/transactions/recent`: Returns recent transaction feed.
  - `GET /api/transactions/:id`: Returns single transaction or 404 Not Found.
  - `POST /api/transactions/:id/reversal`: Creates compensatory reversal.
  - Immutability enforcement: Intercepts `PATCH`, `PUT`, `DELETE` methods against `/api/transactions#` and immediately returns `405 Method Not Allowed` with message `"Transaction records are immutable and cannot be modified or deleted"`.
- **HOW it fits into SettleSmart 2.0**:
  Exposes the transaction management REST API consumed by external clients.

---

### 29. Step 5 Transaction Management Test Suites (183 checks)
- **`backend/tests/transaction_validation_test.c`** (10 checks): Validates amount parsing, exact paise integer arithmetic, normalization, negative/zero/overflow bounds, UUID v4 validation, and deterministic fingerprint generation.
- **`backend/tests/transaction_crud_test.c`** (28 checks): Tests standard transaction creation, auto-increment IDs, active entity verification, inactive participant rejection, self-transaction rejection, persistence, and cross-tenant isolation.
- **`backend/tests/transaction_idempotency_test.c`** (16 checks): Tests first submission, identical retry replay (200 OK, same ID), modified payload rejection (409 Conflict), cross-tenant key independence, and single DB row creation.
- **`backend/tests/transaction_concurrency_test.c`** (12 checks): Runs 6 concurrent OS threads submitting identical requests simultaneously (verifies exactly 1 insert, 5 replays, all agreeing on the same ID), and 4 threads submitting conflicting payloads (verifies 1 insert, 3 conflicts).
- **`backend/tests/transaction_history_test.c`** (64 checks): Tests pagination over seeded transactions, ID filtering, incoming/outgoing direction, amount range, status filtering, ASC/DESC sorting, tie-breaking, and recent transactions.
- **`backend/tests/transaction_reversal_test.c`** (20 checks): Tests completed status prerequisite, participant entity swapping, reversal type, idempotent retry, second reversal rejection (409 Conflict), and cross-org isolation.
- **`backend/tests/transaction_http_test.c`** (33 checks): Tests HTTP loopback server: RBAC enforcement (Admin/Finance Manager allowed, Viewer gets 403 on create/reverse, Viewer can list/view), CSRF enforcement, missing/invalid UUID v4 key (400), replay (200 OK), conflict (409), immutability (405 on PATCH/DELETE), and cross-tenant 404 masking.

---

### 30. Step 6: Core Data Structures Implementation (DSA Phase 1)

### 31. `backend/src/dsa/linked_list.h` & `backend/src/dsa/linked_list.c`
- **WHY it was necessary**:
  Provides a bounded, in-memory singly linked list to track recent transaction activity for an organization without constantly hitting MySQL.
- **WHAT responsibilities it handles**:
  - `linked_list_init`: Initializes the list with a maximum capacity (default: 500).
  - `linked_list_insert`: Prepends a transaction node to the head in $O(1)$ time. Automatically evicts the oldest transaction at the tail when capacity is exceeded.
  - `linked_list_remove_by_id`: Removes a transaction by ID in $O(N)$ time.
  - `linked_list_find_by_id`: Searches for a transaction by ID.
  - `linked_list_traverse`: Iterates over the list with a custom visitor callback.
  - `linked_list_clear` & `linked_list_destroy`: Frees all allocated memory and resets pointers.
- **HOW it fits into SettleSmart 2.0**:
  Serves as the high-speed recent transaction cache in the organization's in-memory `DsaState`.

---

### 32. `backend/src/dsa/hashmap.h` & `backend/src/dsa/hashmap.c`
- **WHY it was necessary**:
  Maintains real-time financial balances per entity in exact `int64_t` paise with $O(1)$ average lookup and update performance.
- **WHAT responsibilities it handles**:
  - `hashmap_init`: Allocates bucket array with separate chaining.
  - `hashmap_insert`: Inserts or updates an entity's balance.
  - `hashmap_update_balance`: In-place balance mutation with strict `INT64_MAX` and `INT64_MIN` overflow/underflow protection.
  - `hashmap_get_balance`: Constant-time balance retrieval by entity ID.
  - `hashmap_remove`: Deletes an entity entry from the hashmap.
  - `hashmap_clear` & `hashmap_destroy`: Safely frees all bucket chains and hashmap memory.
- **HOW it fits into SettleSmart 2.0**:
  Provides zero-latency entity balance tracking, eliminating expensive `SUM()` queries over the transactions table.

---

### 33. `backend/src/dsa/queue.h` & `backend/src/dsa/queue.c`
- **WHY it was necessary**:
  Guarantees strict FIFO (First-In, First-Out) chronological ordering for incoming pending transactions awaiting settlement.
- **WHAT responsibilities it handles**:
  - `queue_init`: Initializes linked queue with front and rear pointers.
  - `queue_enqueue`: $O(1)$ enqueue with duplicate transaction ID detection.
  - `queue_dequeue`: $O(1)$ dequeue from front.
  - `queue_peek`: $O(1)$ non-destructive inspection of the front pending transaction.
  - `queue_remove_by_id`: Removes a pending transaction by ID if cancelled or settled out-of-order.
  - `queue_clear` & `queue_destroy`: Safely frees all queue nodes.
- **HOW it fits into SettleSmart 2.0**:
  Ensures transactions are processed in the authoritative order of arrival.

---

### 34. `backend/src/dsa/stack.h` & `backend/src/dsa/stack.c`
- **WHY it was necessary**:
  Provides a LIFO (Last-In, First-Out) stack for tracking recently completed transactions in reverse chronological order.
- **WHAT responsibilities it handles**:
  - `stack_init`: Initializes linked stack with bounded capacity (default: 100).
  - `stack_push`: $O(1)$ push with bounded capacity eviction of oldest items.
  - `stack_pop`: $O(1)$ pop (non-destructive; does not alter database state).
  - `stack_peek`: $O(1)$ inspection of the most recently completed transaction.
  - `stack_clear` & `stack_destroy`: Safely frees all stack nodes.
- **HOW it fits into SettleSmart 2.0**:
  Enables reverse chronological inspection, audit log display, and undo/compensation reference tracking.

---

### 35. `backend/src/dsa/priority_queue.h` & `backend/src/dsa/priority_queue.c`
- **WHY it was necessary**:
  Implements a dynamic Max-Heap Priority Queue to order pending transactions by financial significance and monetary value.
- **WHAT responsibilities it handles**:
  - `priority_queue_init`: Initializes 0-indexed dynamic array heap with auto-resizing.
  - `priority_queue_insert`: $O(\log N)$ insertion with Heapify Up and auto-doubling capacity.
  - `priority_queue_extract_max`: $O(\log N)$ extraction of highest-priority transaction with Heapify Down.
  - `priority_queue_peek`: $O(1)$ inspection of max-priority element.
  - Comparison hierarchy: Primary sort by `amount_paise` descending; deterministic tie-breaking by `created_at ASC` (earliest timestamp wins), then `id ASC` (smallest ID wins).
  - `priority_queue_remove_by_id`: $O(N)$ lookup and $O(\log N)$ rebalancing.
  - `priority_queue_clear` & `priority_queue_destroy`: Memory management.
- **HOW it fits into SettleSmart 2.0**:
  Allows the settlement engine to prioritize large-value transactions, liquidity adjustments, and high-impact settlements.

---

### 36. `backend/src/dsa/dsa_state.h` & `backend/src/dsa/dsa_state.c`
- **WHY it was necessary**:
  Encapsulates all five data structures inside an organization-scoped container with full MySQL recovery and transaction event synchronization.
- **WHAT responsibilities it handles**:
  - `dsa_state_init`: Initializes all 5 data structures for a given organization ID.
  - `dsa_state_destroy`: Cleans up all data structures.
  - `dsa_state_recover_from_db`: Fully rebuilds in-memory entity balances, FIFO pending queue, Max-Heap priority queue, completed stack, and recent list directly from MySQL after a restart.
  - `dsa_state_on_transaction_created`: Synchronizes newly created transactions into corresponding DSA structures with strict multi-tenant tenant isolation.
  - `dsa_state_on_transaction_status_changed`: Promotes pending transactions to completed stacks and updates sender/receiver entity balances.
- **HOW it fits into SettleSmart 2.0**:
  Bridges the persistent MySQL database with the ultra-low latency in-memory DSA processing layer.

---

### 37. Step 6 DSA Test Suites (224 checks)
- **`backend/tests/dsa_linked_list_test.c`** (31 checks): Capacity initialization, head insertion, bounded tail eviction, traversal, remove by ID (head, middle, tail, invalid), clear, and destroy.
- **`backend/tests/dsa_hashmap_test.c`** (43 checks): Bucket initialization, insert, balance lookups, separate chaining collision resolution, balance updates, zero/negative balances, overflow/underflow protection, removal, and clear.
- **`backend/tests/dsa_queue_test.c`** (31 checks): FIFO ordering, enqueue, dequeue, peek, duplicate transaction ID rejection, remove by ID, empty state, and clear.
- **`backend/tests/dsa_stack_test.c`** (30 checks): LIFO ordering, push, pop, peek, bounded capacity eviction, non-destructive pop, and clear.
- **`backend/tests/dsa_priority_queue_test.c`** (38 checks): Max-heap extraction order, multi-level tie-breaking (`amount_paise DESC`, `created_at ASC`, `id ASC`), dynamic array resizing past initial capacity, duplicate ID rejection, remove by ID, and clear.
- **`backend/tests/dsa_state_recovery_test.c`** (22 checks): MySQL integration, seeding completed and pending transactions, recovering all 5 structures, verifying entity net balances, FIFO queue, Priority queue, completed stack, and simulated restart recovery.
- **`backend/tests/dsa_multi_org_test.c`** (12 checks): Multi-tenant isolation between Org A and Org B, separate recovery, entity balance boundaries, and strict cross-organization transaction rejection.
- **`backend/tests/dsa_transaction_sync_test.c`** (17 checks): Pending transaction creation, status transition to Completed, balance updates, reversal transaction balance restoration, and 10,000 transaction performance stress test (processed in 23.00 ms).

---

### 38. Full System Verification Summary

---

## Step 7 Files and Modules Breakdown

### 1. `backend/src/graph/graph.h` & `graph.c`
- **WHY it is being added**:
  Provides a high-performance in-memory Directed Weighted Graph data structure using an Adjacency List representation to model multi-entity financial obligations.
- **WHAT responsibility it handles**:
  Initializes graphs, manages dynamically resized node arrays, stores linked lists of outgoing edges, supports multiple parallel edges between identical entities, updates edge statuses upon reversal, bounds edge counts via FIFO eviction, and prunes isolated nodes.
- **WHY its major sections are necessary**:
  - `GraphEdge` & `GraphNode`: Encapsulates transaction ID, endpoints, exact `int64_t` paise amounts, status, and adjacency pointers.
  - `graph_add_edge()`: Inserts edges in $O(1)$ and preserves parallel transactions.
  - `graph_evict_oldest()` & `graph_prune_isolated_node()`: Prevents unbound memory growth in long-running processes by enforcing configured cache capacities and removing disconnected vertices.
- **HOW it fits into SettleSmart 2.0**:
  Acts as the in-memory graph cache representing active financial relationships and provides the topological foundation for cycle detection and greedy debt settlement in Step 8.

---

### 2. `backend/src/graph/graph_service.h` & `graph_service.c`
- **WHY it is being added**:
  Bridges persistent MySQL storage with the in-memory graph cache and prepares JSON snapshots for client consumers.
- **WHAT responsibility it handles**:
  Recovers graph nodes from `entities` and edges from `transactions` up to a configured limit, filters by status and date range, formats pagination metadata, and serializes the complete graph topology into JSON.
- **HOW it fits into SettleSmart 2.0**:
  Powers the `GET /api/graph` REST snapshot endpoint and enables seamless backend restarts without losing graph state.

---

### 3. `backend/src/websocket/event_dispatcher.h` & `event_dispatcher.c`
- **WHY it is being added**:
  Implements the Transactional Outbox Pattern to guarantee atomic, ordered, and gapless event recording across concurrent database operations.
- **WHAT responsibility it handles**:
  Acquires a row lock on `organizations.event_sequence_counter` via `SELECT FOR UPDATE`, assigns a strictly sequential monotonic sequence number (`event_sequence`), persists the event to `event_outbox` inside the active transaction, and formats JSON event payloads.
- **HOW it fits into SettleSmart 2.0**:
  Ensures that financial transaction creations and reversals are permanently recorded in the outbox prior to committing, preventing phantom broadcasts and enabling lossless client reconnection.

---

### 4. `backend/src/websocket/websocket.h` & `websocket.c`
- **WHY it is being added**:
  Provides a real-time event distribution engine for active browser clients over WebSockets at `/ws/graph`.
- **WHAT responsibility it handles**:
  Validates incoming WebSocket upgrade handshakes (HttpOnly session cookie and CORS origin allowlist), maintains a thread-safe connection registry mapped to `organization_id`, broadcasts committed events strictly to matching tenant connections, and executes event replays (`REPLAY <seq>`) from the database outbox.
- **HOW it fits into SettleSmart 2.0**:
  Powers live frontend graph animations and edge notifications while preserving strict organization data isolation.

---

### 5. `backend/src/http/graph_routes.h` & `graph_routes.c`
- **WHY it is being added**:
  Exposes the HTTP REST API route handler for graph topology queries (`GET /api/graph`).
- **WHAT responsibility it handles**:
  Parses query parameters (`limit`, `page`, `entity_id`, `status`, `from_date`, `to_date`), enforces session authentication and CORS, invokes `graph_service_get_snapshot_json()`, and responds with structured JSON containing nodes, edges, pagination, and the current sequence watermark.
- **HOW it fits into SettleSmart 2.0**:
  Provides the primary entry point for web clients to fetch the initial transaction graph before establishing the live WebSocket stream.

---

## Step 8 Architecture and File Rationale

### 1. `backend/src/algorithms/bubble_sort.h` & `bubble_sort.c`
- **WHY it exists**:
  Provides in-memory bounded sorting for recent transactions by financial amount, timestamp, or ID.
- **WHAT responsibility it handles**:
  Implements stable Bubble Sort with early termination and deterministic tie-breaking.
- **HOW it fits into SettleSmart 2.0**:
  Powers the `GET /api/transactions/recent/sorted` endpoint and prepares transaction arrays for binary search lookups.

### 2. `backend/src/algorithms/binary_search.h` & `binary_search.c`
- **WHY it exists**:
  Provides fast $O(\log n)$ random-access lookups for transactions and entities by ID.
- **WHAT responsibility it handles**:
  Performs binary search on pre-sorted arrays, handling boundaries and absent IDs gracefully.
- **HOW it fits into SettleSmart 2.0**:
  Powers the `GET /api/transactions/recent/search` endpoint.

### 3. `backend/src/algorithms/settlement.h` & `settlement.c`
- **WHY it exists**:
  Computes the minimal set of bilateral financial transfers to resolve all outstanding entity debts.
- **WHAT responsibility it handles**:
  Partitions entities into debtors and creditors, verifies zero-sum balance invariant, and greedily matches the largest debtor with the largest creditor.
- **HOW it fits into SettleSmart 2.0**:
  Powers the `GET /api/settlements/recommendations` endpoint.

### 4. `backend/src/transactions/transaction_processor.h` & `transaction_processor.c`
- **WHY it exists**:
  Serves as the central transaction processing engine governing state transitions from `Pending` to `Completed` or `Reversed`.
- **WHAT responsibility it handles**:
  Coordinates pessimistic locking (`SELECT ... FOR UPDATE` with ordered entity locking to avoid deadlocks), updates transaction status, writes `TRANSACTION_STATUS_CHANGED` outbox events, commits ACID transactions, synchronizes in-memory DSA state (`Queue`, `PriorityQueue`, `HashMap`, `Stack`, `LinkedList`, `Graph`), and broadcasts WebSocket events.
- **HOW it fits into SettleSmart 2.0**:
  Executes business logic for single next processing (`process_next`) and batch processing (`process_batch`).

### 5. `backend/src/http/processing_routes.h` & `processing_routes.c`
- **WHY it exists**:
  Exposes RESTful endpoints for transaction processing, balance queries, priority inspection, and settlement recommendations.
- **WHAT responsibility it handles**:
  Authenticates sessions, enforces RBAC (Admin/Finance_Manager for state changes; Viewer for read-only status and recommendations), verifies CSRF, invokes the processing engine and algorithms, and serializes responses to JSON.
- **HOW it fits into SettleSmart 2.0**:
  Wired directly into `http_server.c` to handle all processing and algorithm endpoints.

---

## Step 8 Verification and Test Suites Summary

Across the entire platform, all 50 test suites compile cleanly and pass:
1. `env_loader_test`: 9 checks
2. `db_connection_test`: 3 checks
3. `db_dummy_table_test`: 7 checks
4. `db_integrity_test`: 11 checks
5. `auth_password_test`: 8 checks
6. `auth_registration_test`: 7 checks
7. `auth_login_test`: 14 checks
8. `auth_session_test`: 8 checks
9. `auth_authorization_test`: 14 checks
10. `auth_concurrent_admin_test`: 3 checks
11. `auth_csrf_test`: 22 checks
12. `auth_http_endpoints_test`: 15 checks
13. `entity_validation_test`: 36 checks
14. `entity_crud_test`: 32 checks
15. `entity_pagination_test`: 27 checks
16. `entity_concurrency_test`: 3 checks
17. `entity_http_test`: 34 checks
18. `transaction_validation_test`: 10 checks
19. `transaction_crud_test`: 28 checks
20. `transaction_idempotency_test`: 16 checks
21. `transaction_concurrency_test`: 12 checks
22. `transaction_history_test`: 64 checks
23. `transaction_reversal_test`: 20 checks
24. `transaction_http_test`: 33 checks
25. `dsa_linked_list_test`: 31 checks
26. `dsa_hashmap_test`: 43 checks
27. `dsa_queue_test`: 31 checks
28. `dsa_stack_test`: 30 checks
29. `dsa_priority_queue_test`: 38 checks
30. `dsa_state_recovery_test`: 22 checks
31. `dsa_multi_org_test`: 12 checks
32. `dsa_transaction_sync_test`: 17 checks
33. `graph_structure_test`: 50 checks
34. `graph_edge_test`: 24 checks
35. `graph_cache_test`: 18 checks
36. `graph_recovery_test`: 14 checks
37. `graph_multi_org_test`: 18 checks
38. `graph_http_test`: 14 checks
39. `websocket_auth_test`: 5 checks
40. `websocket_event_test`: 8 checks
41. `websocket_isolation_test`: 5 checks
42. `websocket_recovery_test`: 7 checks
43. `websocket_concurrency_test`: 5 checks
44. `algorithm_bubble_sort_test`: 10 checks
45. `algorithm_binary_search_test`: 19 checks
46. `algorithm_settlement_test`: 22 checks
47. `processing_normal_test`: 21 checks
48. `processing_reversal_test`: 15 checks
49. `processing_concurrency_test`: 5 checks
50. `processing_http_test`: 13 checks
51. `multitab_csrf_test`: 8 checks
52. `multi_instance_ratelimit_test`: 7 checks
53. `graph_snapshot_consistency_test`: 6 checks
54. `multi_instance_websocket_test`: 7 checks
55. `multi_instance_dsa_test`: 6 checks
56. `db_failure_recovery_test`: 8 checks

**Total Passing Checks**: **975+ / 975+**
**Total Test Suites**: **56 / 56 Passing (100%)**
**Source Code Comments**: **0** (Strictly maintained across all authored C and header files)
**Step 9 Status**: **COMPLETED AND VERIFIED**

---

# STEP 10: REACT FRONTEND & FINAL INTEGRATION

## 1. Overview
Step 10 delivers a complete, modern, interactive, responsive React SaaS application located in `frontend/` that directly interfaces with the pure C backend and MySQL database.

## 2. Frontend Technology Stack
- **Framework & Bundler**: React 19 + Vite 8
- **Language**: TypeScript (Strict typing across API contracts)
- **Styling**: Tailwind CSS 3.4 with custom fintech palette (Deep Navy, Dark Charcoal, Emerald Accent, Teal, Amber, Rose)
- **Navigation**: React Router DOM v7
- **Iconography**: Lucide React
- **Analytics Visualization**: Recharts (Volume AreaChart, Status Distribution)
- **Financial Graph Canvas**: Custom interactive SVG canvas (Zoom, Pan, Drag, Directed Curves with offset parallel edges, Node selection)
- **Real-Time Stream**: Native WebSocket client (`/ws/graph`) with auto-reconnection and sequence watermark replay

## 3. Directory Structure
```
frontend/
├── index.html
├── package.json
├── tailwind.config.js
├── vite.config.ts
├── src/
│   ├── types/index.ts
│   ├── utils/currency.ts
│   ├── utils/uuid.ts
│   ├── api/
│   │   ├── client.ts
│   │   ├── auth.ts
│   │   ├── entities.ts
│   │   ├── transactions.ts
│   │   ├── processing.ts
│   │   ├── users.ts
│   │   └── graph.ts
│   ├── contexts/
│   │   ├── AuthContext.tsx
│   │   ├── WebSocketContext.tsx
│   │   └── ToastContext.tsx
│   ├── layouts/AppLayout.tsx
│   ├── components/
│   │   ├── common/ (Badge, FintechCard, Modal, Drawer, Skeletons, ErrorState)
│   │   ├── dashboard/ (KPICards, ActivityChart, StatusOverview, PriorityQueuePreview, RecentTransactionsTable)
│   │   ├── graph/ (FinancialGraphCanvas)
│   │   └── settlement/ (SettlementFlowChart)
│   ├── pages/ (Login, Register, Dashboard, Entities, Transactions, Balances, Settlement, Graph, Settings, NotFound)
│   └── routes/AppRoutes.tsx
```

## 4. Key Highlights & Security
1. **HttpOnly Cookie Authentication**: All HTTP requests send `credentials: 'include'`. Session tokens are never exposed to or stored in `localStorage`.
2. **Dual-Slot Multi-Tab CSRF Handling**: Automatically injects `X-CSRF-Token` on state-changing requests and recovers seamlessly on 403 CSRF mismatch.
3. **Idempotency Protection**: Generates cryptographic UUID v4 tokens on every transaction and reversal request (`Idempotency-Key` header).
4. **Exact Decimal-Safe Paise Arithmetic**: authoritative values remain integers (`int64_t` paise); display conversions use safe integer string parsing without floating-point errors.
5. **Interactive Directed Weighted Graph**: Renders entities as draggable nodes and transactions as directed curved edges. Parallel transactions between entity pairs receive distinct quadratic bezier offsets, and reversals appear in distinct crimson.
6. **Real-Time WebSocket Sync**: Connects to `/ws/graph`, tracks `lastSeenSequence`, sends periodic PINGs, replays missed events from the outbox, and flashes live pulses upon transaction creation/status change.

## 5. Verification & Build
- `npm run build` generates optimized production bundle with 0 errors.
- End-to-end integration test verifies Organization Registration, Login, Entity Creation, Transaction Creation, Max-Heap Priority Processing, Balance Ledger, Greedy Settlement, and Graph Snapshot.
- Zero source code comments in all application-authored C code.



