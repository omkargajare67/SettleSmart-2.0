# SettleSmart 2.0 — Entity Management Specification

## 1. Overview and Purpose

Entities in SettleSmart 2.0 represent the legal and organizational participants between whom financial transactions occur. Examples include internal departments, external vendors, business partners, clients, and operating subsidiaries.

The Entity Management module is built strictly in C (C11) backed by MySQL. It provides full lifecycle management (creation, retrieval, update, status toggling, search, filtering, and pagination) under multi-tenant organization isolation and strict role-based access control.

---

## 2. Architectural Design Principles

1. **Authoritative Database Auto-Increment IDs**:
   - Entity IDs are generated exclusively by MySQL's `AUTO_INCREMENT` mechanism.
   - Client requests cannot specify, dictate, or influence entity IDs during creation.
   - Manually computed or application-guessed IDs are strictly prohibited.

2. **Strict Organization-Scoped Isolation**:
   - `organization_id` is derived exclusively from server-verified session state.
   - Request bodies and query parameters are never trusted for tenant identity.
   - Cross-organization queries return standard `404 Not Found` errors rather than `403 Forbidden`, preventing enumeration or leakage of entity IDs belonging to other tenants.

3. **Organization-Scoped Unique Name Prevention**:
   - Entity names are unique within a given organization (`uq_entities_org_name UNIQUE (organization_id, name)`).
   - Attempting to create or rename an entity to match an existing entity in the same organization returns `409 Conflict`.
   - Distinct organizations can safely reuse the same entity names without conflict.

4. **Role-Based Access Control (RBAC)**:
   - **Admin**: Full entity management rights (create, read, update, deactivate/reactivate).
   - **Finance Manager**: Full entity management rights (create, read, update, deactivate/reactivate).
   - **Viewer**: Read-only access (search, filter, list, get by ID). Modification attempts return `403 Forbidden`.

5. **Soft Deactivation and Immutability of History**:
   - Physical deletion (`DELETE /api/entities/:id`) is explicitly prohibited and rejected with `405 Method Not Allowed`.
   - Entities are soft-deactivated via `PATCH /api/entities/:id/status` (`Active` <-> `Inactive`) to preserve referential integrity for historical financial transactions.

---

## 3. Database Schema

```sql
CREATE TABLE IF NOT EXISTS entities (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    name VARCHAR(150) NOT NULL,
    entity_type ENUM('Department', 'Vendor', 'Subsidiary', 'Client', 'Partner') NOT NULL DEFAULT 'Department',
    status ENUM('Active', 'Inactive') NOT NULL DEFAULT 'Active',
    created_by_user_id BIGINT NULL,
    updated_by_user_id BIGINT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_entities_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,
    CONSTRAINT uq_entities_org_id UNIQUE (organization_id, id),
    CONSTRAINT uq_entities_org_name UNIQUE (organization_id, name),
    CONSTRAINT fk_entities_created_by FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_entities_updated_by FOREIGN KEY (organization_id, updated_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT
) ENGINE=InnoDB;
```

---

## 4. API Endpoints

### 4.1 Create Entity
- **Endpoint**: `POST /api/entities`
- **Permissions**: `Admin`, `Finance_Manager`
- **Security**: HttpOnly Session Cookie, CSRF Token (`X-CSRF-Token`), Origin verification
- **Request Body**:
```json
{
  "name": "Global Logistics",
  "entity_type": "Vendor"
}
```
- **Response (201 Created)**:
```json
{
  "success": true,
  "entity": {
    "id": 1,
    "organization_id": 1,
    "name": "Global Logistics",
    "entity_type": "Vendor",
    "status": "Active",
    "created_by_user_id": 1,
    "updated_by_user_id": 1,
    "created_at": "2026-11-15 13:00:00",
    "updated_at": "2026-11-15 13:00:00"
  }
}
```
- **Error Codes**:
  - `400 Bad Request`: Invalid name (empty, >150 chars) or invalid entity type.
  - `401 Unauthorized`: Missing or invalid session.
  - `403 Forbidden`: Viewer role or missing/invalid CSRF token.
  - `409 Conflict`: Entity with this name already exists in the organization.

### 4.2 List Entities
- **Endpoint**: `GET /api/entities`
- **Permissions**: `Admin`, `Finance_Manager`, `Viewer`
- **Security**: HttpOnly Session Cookie
- **Query Parameters**:
  - `page`: Integer (default 1, minimum 1)
  - `limit`: Integer (default 10, range 1-100)
  - `search`: Substring search on name or exact match on numeric entity ID
  - `type` / `entity_type`: Filter by entity type (`Department`, `Vendor`, `Subsidiary`, `Client`, `Partner`)
  - `status`: Filter by status (`Active`, `Inactive`)
  - `sort_by`: Allowlisted column (`id`, `name`, `created_at`; default `id`)
  - `order`: `ASC` or `DESC` (default `ASC`)
- **Response (200 OK)**:
```json
{
  "success": true,
  "data": [
    {
      "id": 1,
      "organization_id": 1,
      "name": "Global Logistics",
      "entity_type": "Vendor",
      "status": "Active",
      "created_by_user_id": 1,
      "updated_by_user_id": 1,
      "created_at": "2026-11-15 13:00:00",
      "updated_at": "2026-11-15 13:00:00"
    }
  ],
  "pagination": {
    "page": 1,
    "limit": 10,
    "total_records": 1,
    "total_pages": 1
  }
}
```

### 4.3 Get Single Entity
- **Endpoint**: `GET /api/entities/:id`
- **Permissions**: `Admin`, `Finance_Manager`, `Viewer`
- **Response (200 OK)**:
```json
{
  "success": true,
  "entity": {
    "id": 1,
    "organization_id": 1,
    "name": "Global Logistics",
    "entity_type": "Vendor",
    "status": "Active",
    "created_by_user_id": 1,
    "updated_by_user_id": 1,
    "created_at": "2026-11-15 13:00:00",
    "updated_at": "2026-11-15 13:00:00"
  }
}
```
- **Error Codes**:
  - `404 Not Found`: Entity does not exist or belongs to another organization.

### 4.4 Update Entity Details
- **Endpoint**: `PATCH /api/entities/:id`
- **Permissions**: `Admin`, `Finance_Manager`
- **Security**: HttpOnly Session Cookie, CSRF Token, Origin verification
- **Request Body**:
```json
{
  "name": "Global Logistics International",
  "entity_type": "Partner"
}
```
- **Response (200 OK)**: Returns updated entity object.
- **Error Codes**:
  - `400 Bad Request`: Validation failure.
  - `403 Forbidden`: Viewer role or CSRF failure.
  - `404 Not Found`: Entity does not exist or foreign organization.
  - `409 Conflict`: New name collides with another entity in the same organization.

### 4.5 Update Entity Status (Activate / Deactivate)
- **Endpoint**: `PATCH /api/entities/:id/status`
- **Permissions**: `Admin`, `Finance_Manager`
- **Security**: HttpOnly Session Cookie, CSRF Token, Origin verification
- **Request Body**:
```json
{
  "status": "Inactive"
}
```
- **Response (200 OK)**: Returns updated entity object with new status.
- **Error Codes**:
  - `400 Bad Request`: Status value not 'Active' or 'Inactive'.
  - `403 Forbidden`: Viewer role or CSRF failure.
  - `404 Not Found`: Entity does not exist or foreign organization.

### 4.6 Prohibited Deletion
- **Endpoint**: `DELETE /api/entities/:id`
- **Response (405 Method Not Allowed)**:
```json
{
  "success": false,
  "error": "Physical deletion of entities is not permitted"
}
```

---

## 5. Verification and Test Suite

Five dedicated test suites validate Entity Management across all layers:

1. `tests/entity_validation_test.c`: 36 unit checks verifying name trimming, 1-150 character bounds, enum mappings, and status validation.
2. `tests/entity_crud_test.c`: 32 integration checks verifying auto-increment IDs, intra-org unique name conflicts, inter-org name independence, retrieval, updates, and status toggles.
3. `tests/entity_pagination_test.c`: 27 integration checks verifying pagination math, name substring searches, ID searches, type filters, status filters, and ASC/DESC sorting.
4. `tests/entity_concurrency_test.c`: 3 multi-threaded concurrency checks launching 4 simultaneous worker threads with independent database connections attempting identical entity creation.
5. `tests/entity_http_test.c`: 34 network integration checks verifying loopback HTTP endpoints, RBAC permissions (Admin, Finance Manager, Viewer), CSRF protection, and cross-organization 404 isolation.
