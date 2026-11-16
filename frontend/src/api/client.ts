const defaultApi = typeof window !== 'undefined'
  ? `${window.location.protocol}//${window.location.host}`
  : 'http://localhost:8000';

const API_BASE_URL = (
  import.meta.env.VITE_API_BASE_URL && !import.meta.env.VITE_API_BASE_URL.includes('localhost')
    ? import.meta.env.VITE_API_BASE_URL
    : defaultApi
).replace(/\/+$/, '');

let memoryCsrfToken: string | null = null;

export function setCsrfToken(token: string | null) {
  memoryCsrfToken = token;
}

export function getCsrfToken(): string | null {
  return memoryCsrfToken;
}

export interface RequestOptions extends RequestInit {
  idempotencyKey?: string;
}

export async function refreshCsrfToken(): Promise<string | null> {
  try {
    const res = await fetch(`${API_BASE_URL}/api/auth/csrf`, {
      method: 'POST',
      credentials: 'include',
      headers: {
        'Content-Type': 'application/json',
      },
    });
    if (res.ok) {
      const data = await res.json();
      if (data && data.csrf_token) {
        setCsrfToken(data.csrf_token);
        return data.csrf_token;
      }
    }
  } catch (err) {
    console.error('Failed to refresh CSRF token', err);
  }
  return null;
}

export async function apiClient<T = any>(endpoint: string, options: RequestOptions = {}): Promise<T> {
  const url = endpoint.startsWith('http') ? endpoint : `${API_BASE_URL}${endpoint.startsWith('/') ? '' : '/'}${endpoint}`;
  const method = (options.method || 'GET').toUpperCase();
  const headers = new Headers(options.headers || {});

  if (!headers.has('Content-Type') && !(options.body instanceof FormData)) {
    headers.set('Content-Type', 'application/json');
  }

  const isStateChanging = ['POST', 'PATCH', 'PUT', 'DELETE'].includes(method);
  if (isStateChanging && memoryCsrfToken) {
    headers.set('X-CSRF-Token', memoryCsrfToken);
  }

  if (options.idempotencyKey) {
    headers.set('Idempotency-Key', options.idempotencyKey);
  }

  let response: Response;
  try {
    response = await fetch(url, {
      ...options,
      method,
      headers,
      credentials: 'include',
    });
  } catch (netErr: any) {
    throw new Error('Network error: Unable to connect to SettleSmart backend. Please verify the server is running.');
  }

  if (response.status === 403 && isStateChanging) {
    const cloned = response.clone();
    try {
      const errData = await cloned.json();
      if (errData && errData.error && errData.error.toLowerCase().includes('csrf')) {
        const freshToken = await refreshCsrfToken();
        if (freshToken) {
          headers.set('X-CSRF-Token', freshToken);
          response = await fetch(url, {
            ...options,
            method,
            headers,
            credentials: 'include',
          });
        }
      }
    } catch {
      // ignore
    }
  }

  let data: any = null;
  const contentType = response.headers.get('content-type');
  if (contentType && contentType.includes('application/json')) {
    try {
      data = await response.json();
    } catch {
      data = null;
    }
  }

  if (!response.ok) {
    const errorMsg = (data && data.error) || `Request failed with status ${response.status}`;
    const err: any = new Error(errorMsg);
    err.status = response.status;
    err.data = data;
    throw err;
  }

  return data;
}

export { API_BASE_URL };
