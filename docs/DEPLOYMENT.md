# SettleSmart 2.0 — Deployment and Operating Modes

## 1. Architectural Philosophy: One Codebase, Two Operating Modes

SettleSmart 2.0 is designed from the ground up to support two completely different operating environments using a single unified codebase:

1. **Mode A: Local Installation / Source Code Sharing**:
   - For software engineering students, educators, academic examiners, and local developers.
   - Built and run on the developer's local workstation.
   - Uses a local MySQL database with independent auto-increment sequences.
   - Zero hardcoded credentials; fully driven by a local `.env` file parsed by the C backend.
2. **Mode B: Centralized Online Cloud Deployment**:
   - For enterprise organizations, finance teams, and end-users.
   - Hosted on a remote cloud server/container connecting to a managed cloud MySQL database.
   - Accessible via a standard public web URL (e.g., `https://settlesmart.app`).
   - End-users do not need to install C compilers, MySQL, Node.js, or development tools.

---

> [!WARNING]
> **CRITICAL CONFIGURATION SECURITY RULE**:
> Never share your personal `.env` file or commit it to version control! The `.env` file contains sensitive local database credentials and is strictly excluded by `.gitignore`.
> When sharing the project through GitHub, email, or a ZIP archive, include `.env.example` instead. Each person must create their own local `.env` file and configure their own database credentials.

---

## 2. Supported Database Engines & Version Policy

- **Officially Supported LTS Versions**:
  - **MySQL 8.4 LTS** (Long Term Support) — Recommended production target.
  - **MySQL 9.7 LTS** (Long Term Support) — Verified local development target.
- **Support Boundary & Policy**:
  - The platform targets long-term supported releases (8.4 LTS and 9.7 LTS).
  - SettleSmart 2.0 explicitly does **not** make a blanket claim that every intermediate non-LTS innovation release (such as 9.0, 9.1, 9.2) is supported without testing.
  - Current installed server version on the development workstation: **MySQL Community Server 9.7.1 for Win64 on x86_64**.
  - Do not update or replace the installed database server without checking compiler and client library compatibility first.

---

## 3. Environment Compatibility & 64-bit Architecture Findings (Step 2)

During Step 2 inspection, the following critical environment characteristics were verified:

1. **MySQL Server Architecture**:
   - Oracle MySQL Server 8.4 LTS and 9.x on Windows are exclusively **64-bit x86_64** binaries (`IMAGE_FILE_MACHINE_AMD64`).
   - The associated client library `libmysql.dll` and import library `libmysql.lib` are 64-bit AMD64 PE binaries.
2. **Compiler Compatibility**:
   - Legacy 32-bit compilers (such as MinGW.org GCC 6.3.0 i386) **cannot link** against 64-bit MySQL libraries on Windows due to architecture mismatches and name-mangling incompatibilities (`undefined reference to mysql_init@4`).
   - SettleSmart 2.0 requires a standard **64-bit MinGW-w64 toolchain** (`x86_64-w64-mingw32-gcc`, version 11+) on Windows, or standard 64-bit GCC (`x86_64-linux-gnu-gcc`) on Linux.
3. **Runtime Dynamic Library Resolution on Windows**:
   - On Windows, `libmysql.dll` dynamically loads OpenSSL 3 libraries (`libcrypto-3-x64.dll` and `libssl-3-x64.dll`).
   - These runtime libraries are located in MySQL Server's `bin/` directory (`C:\Program Files\MySQL\MySQL Server 9.7\bin`).
   - Therefore, on Windows workstations, both the MySQL `bin` directory and `lib` directory must be present on the system `PATH` (or copied alongside the compiled executable).
4. **Path Naming Best Practice**:
   - Avoid installing compilers or toolchains in directory paths containing single quotes or apostrophes (e.g., `C:\Users\OMKAR's PC\...`), as GNU `collect2` and `ld` can misinterpret unescaped apostrophes during linking. Use standard paths such as `C:\mingw64` or short 8.3 paths.

---

## 4. Environment Configuration Loading (`env_loader`)

The C backend integrates a dedicated cross-platform configuration loader:

### 4.1 Functionality
- Reads key-value pairs from `.env`.
- Trims whitespace and strips surrounding single or double quotes.
- Ignores blank lines and comment lines starting with `#`.
- Supports directory traversal fallback: checks `.env` in the current working directory, and falls back to `../.env` if executing from a test or sub-folder.
- **Precedence Rule**: If a system environment variable is already set (e.g. injected in a Docker container or Kubernetes pod), `env_loader` preserves it, allowing cloud orchestrators to override local file defaults.
- Safe execution: Does not output database passwords or secrets to console streams.

---

## 5. Mode A: Local Installation Workflow

### 5.1 Windows Workstation Setup
1. **Prerequisites**:
   - 64-bit MinGW-w64 GCC toolchain (e.g. WinLibs UCRT / MSYS2 mingw64) installed at `C:\mingw64\bin`.
   - MySQL Community Server 8.4 LTS or 9.7 LTS x64.
2. **Environment Variables**:
   Add MySQL and GCC to PATH:
   ```cmd
   set PATH=C:\mingw64\bin;C:\Program Files\MySQL\MySQL Server 9.7\bin;C:\Program Files\MySQL\MySQL Server 9.7\lib;%PATH%
   ```
3. **Database Initialization**:
   - Log into MySQL:
     ```cmd
     mysql -u root -p
     ```
   - Create local database:
     ```sql
     CREATE DATABASE settlesmart CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
     ```
   - Run the application schema:
     ```cmd
     mysql -u root -p settlesmart < database/schema.sql
     ```
4. **Configure Local Environment**:
   - Copy `.env.example` to `.env`:
     ```cmd
     copy .env.example .env
     ```
   - Update `.env` with your local database port, user, and password.
5. **Compile and Run Tests**:
   ```cmd
   cd backend
   mingw32-make test
   ```

### 5.2 Linux Compatibility Status & Requirements
- **Verification Status**:
  - The local workstation has `wsl.exe` available, but no WSL Linux distributions are installed, and Docker is not installed.
  - Therefore, the Linux build is **explicitly marked as UNVERIFIED**. We do not simulate or falsely claim a successful Linux build.
- **Linux Build Prerequisites**:
  When deploying or verifying on an actual Linux system (Ubuntu 22.04 / 24.04 LTS or Debian 12):
  ```bash
  sudo apt-get update
  sudo apt-get install -y build-essential libmysqlclient-dev default-mysql-server
  sudo mysql -e "CREATE DATABASE settlesmart CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
  sudo mysql settlesmart < database/schema.sql
  cp .env.example .env
  cd backend
  make test
  ```

---

## 6. Mode B: Centralized Online Cloud Deployment

```
+------------------------------------------------------------------+
|                          END USERS                               |
|   (Any web browser on mobile, laptop, or desktop via public URL) |
+------------------------------------------------------------------+
                                  |
                                HTTPS
                                  |
                                  v
+------------------------------------------------------------------+
|                   HOSTED FRONTEND TIER (Vercel / Nginx)          |
|   - Serves pre-built static React bundle                         |
|   - Proxies API requests to C backend                            |
+------------------------------------------------------------------+
                                  |
                       Reverse Proxy / TLS
                                  |
                                  v
+------------------------------------------------------------------+
|                     HOSTED C BACKEND SERVICE                     |
|   - Linux Virtual Machine / Container (Docker / ECS / Cloud Run) |
|   - Runs optimized C executable: settlesmart_server              |
|   - Configuration injected via container environment variables   |
|   - In-memory multi-tenant DSA engines (active working window)   |
|   - WebSocket Hub broadcasting across tenant channels            |
+------------------------------------------------------------------+
                                  |
                    TLS MySQL Connection (Private VPC)
                                  |
                                  v
+------------------------------------------------------------------+
|                     MANAGED CLOUD DATABASE                       |
|   - Cloud MySQL 8.4 LTS (AWS RDS / GCP Cloud SQL / DigitalOcean) |
|   - Single persistent source of truth                            |
|   - Global AUTO_INCREMENT sequence (gap-tolerant)                |
|   - Exact DECIMAL(15, 2) financial storage                       |
|   - Comprehensive composite foreign keys (isolation enforced)    |
|   - Unique idempotency key deduplication                         |
+------------------------------------------------------------------+
```

### 6.1 Cloud Environment Security Best Practices
- **No Local `.env` in Production**: Configuration is injected directly by container management (Docker environment variables, Kubernetes ConfigMaps/Secrets, or AWS Parameter Store).
- **Network Boundaries**: The managed cloud MySQL database resides inside a private VPC, accepting connections solely from the C backend service over TLS.
- **Zero Frontend Leakage**: Database credentials remain strictly on the backend and are never exposed to browser clients.

---

## 7. Production TLS Configuration Status

- **Client Configuration in C**:
  - `backend/src/database/db_client.c` maps `DB_SSL_MODE` to `mysql_options(client->connection, MYSQL_OPT_SSL_MODE, &ssl_mode_val)` using `SSL_MODE_PREFERRED`, `SSL_MODE_REQUIRED`, `SSL_MODE_VERIFY_CA`, or `SSL_MODE_VERIFY_IDENTITY`.
- **Local Test Verification**:
  - The local connection test connected over TLS and confirmed the active cipher:
    `INFO: Active TLS Cipher: TLS_AES_128_GCM_SHA256`.
- **Remote Cloud TLS Status**:
  - Encrypted client negotiation is verified on the local TLS channel.
  - Remote cloud certificate chain verification against an external cloud provider (e.g. AWS RDS / Cloud SQL root CA certificates) has **not been functionally verified** due to the absence of a live remote cloud database during local development.

---

## 8. Step 3 Security & HTTP Environment Configuration

For Step 3 Authentication and HTTP Server deployment, the following variables must be configured in `.env` (development) or injected via container environment variables (production):

| Variable | Development Value | Production Value | Description |
| :--- | :--- | :--- | :--- |
| `APP_ENV` | `development` | `production` | Setting to `production` enforces `COOKIE_SECURE=true` and disables public registration by default. |
| `ALLOWED_ORIGINS` | `http://localhost:3000,http://127.0.0.1:3000` | `https://app.settlesmart.com` | Comma-separated list of approved frontend origins. Wildcard `*` is rejected on credentialed requests. |
| `COOKIE_SECURE` | `false` | `true` | When true, appends `; Secure` to session cookies. Production mode programmatically enforces `true`. |
| `ENABLE_PUBLIC_REGISTRATION` | `true` | `false` | When false, `POST /api/auth/register-organization` rejects new tenant registration with 403 Forbidden. |

