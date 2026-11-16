# FRONTEND DEPLOYMENT GUIDE — SETTLESMART 2.0

## 1. Prerequisites

- **Node.js**: Version 18.0.0 or higher (v26.4.0 verified)
- **npm**: Version 9.0.0 or higher
- **Backend**: SettleSmart 2.0 C backend running with MySQL database

---

## 2. Environment Configuration

Create a `.env` file in `frontend/` based on `.env.example`:

```env
# URL where the C backend HTTP server is listening
VITE_API_BASE_URL=http://localhost:8000

# WebSocket stream URL for graph updates
VITE_WS_URL=ws://localhost:8000
```

> **Security Note**: Never commit `.env` or production credentials. Keep `.env` strictly in `.gitignore`.

---

## 3. Local Development Mode

To run both backend and frontend locally:

### Step 1: Start MySQL Database
Ensure MySQL Server is running on port 3307 (or 3306):
```powershell
& "C:\Program Files\MySQL\MySQL Server 9.7\bin\mysqld.exe" --console --port=3307 --datadir=d:\SETTLESMART\mysql_dev_data
```

### Step 2: Start C Backend Server
From the `backend/` directory:
```powershell
cd backend
.\server.exe ..\.env
```
The server will bind to `0.0.0.0:8000` and announce:
```
SettleSmart 2.0 backend server listening on port 8000
Allowed origins: http://localhost:3000,http://127.0.0.1:3000
```

### Step 3: Start React Frontend Development Server
From the `frontend/` directory:
```powershell
cd frontend
npm install
npm run dev
```
The frontend dev server starts at `http://localhost:3000/`.

---

## 4. Production Build

To build an optimized production artifact:

```powershell
cd frontend
npm run build
```

The compiled static assets will be output to `frontend/dist/`:
- `dist/index.html` (~1.3 kB)
- `dist/assets/index-*.css` (~31 kB gzip ~6.3 kB)
- `dist/assets/index-*.js` (~773 kB gzip ~218 kB)

### Production Serving with Nginx
Example Nginx reverse-proxy configuration:

```nginx
server {
    listen 80;
    server_name settlesmart.example.com;

    # Serve static React SPA
    location / {
        root /var/www/settlesmart/frontend/dist;
        try_files $uri $uri/ /index.html;
    }

    # Proxy REST API requests to C backend
    location /api/ {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_cookie_path / /;
    }

    # Proxy WebSocket connections to C backend
    location /ws/ {
        proxy_pass http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "Upgrade";
        proxy_set_header Host $host;
    }
}
```
