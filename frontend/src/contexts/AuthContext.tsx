import React, { createContext, useContext, useState, useEffect, useCallback } from 'react';
import { User, UserRole } from '../types';
import { getCurrentUser, login as apiLogin, logout as apiLogout, LoginParams } from '../api/auth';
import { refreshCsrfToken } from '../api/client';

interface AuthContextType {
  user: User | null;
  organizationId: number | null;
  role: UserRole | null;
  isAuthenticated: boolean;
  isLoading: boolean;
  lastOrgId: number | null;
  canManageEntities: boolean;
  canManageTransactions: boolean;
  canManageUsers: boolean;
  canProcessTransactions: boolean;
  login: (params: LoginParams) => Promise<void>;
  logout: () => Promise<void>;
  refreshAuth: () => Promise<void>;
}

const AuthContext = createContext<AuthContextType | undefined>(undefined);

export const AuthProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [user, setUser] = useState<User | null>(null);
  const [isLoading, setIsLoading] = useState<boolean>(true);
  const [lastOrgId, setLastOrgId] = useState<number | null>(() => {
    try {
      const saved = localStorage.getItem('settlesmart_last_org_id');
      return saved ? parseInt(saved, 10) : null;
    } catch {
      return null;
    }
  });

  const checkAuth = useCallback(async () => {
    setIsLoading(true);
    try {
      const res = await getCurrentUser();
      if (res && res.success && res.user) {
        setUser(res.user);
        if (res.token) {
          try { sessionStorage.setItem('settlesmart_ws_token', res.token); } catch {}
        }
        await refreshCsrfToken();
      } else {
        setUser(null);
      }
    } catch {
      setUser(null);
    } finally {
      setIsLoading(false);
    }
  }, []);

  useEffect(() => {
    checkAuth();
  }, [checkAuth]);

  const login = async (params: LoginParams) => {
    setIsLoading(true);
    try {
      const res = await apiLogin(params);
      if (res && res.success && res.user) {
        setUser(res.user);
        if (res.token) {
          try { sessionStorage.setItem('settlesmart_ws_token', res.token); } catch {}
        }
        setLastOrgId(params.organization_id);
        try {
          localStorage.setItem('settlesmart_last_org_id', String(params.organization_id));
        } catch {
          // ignore storage error
        }
      } else {
        throw new Error(res.error || 'Authentication failed');
      }
    } finally {
      setIsLoading(false);
    }
  };

  const logout = async () => {
    setIsLoading(true);
    try {
      try { sessionStorage.removeItem('settlesmart_ws_token'); } catch {}
      await apiLogout();
    } catch (err) {
      console.warn('Logout warning', err);
    } finally {
      setUser(null);
      setIsLoading(false);
    }
  };

  const role = user?.role || null;
  const canManageEntities = role === 'Admin' || role === 'Finance_Manager';
  const canManageTransactions = role === 'Admin' || role === 'Finance_Manager';
  const canProcessTransactions = role === 'Admin' || role === 'Finance_Manager';
  const canManageUsers = role === 'Admin';

  return (
    <AuthContext.Provider
      value={{
        user,
        organizationId: user?.organization_id || null,
        role,
        isAuthenticated: !!user,
        isLoading,
        lastOrgId,
        canManageEntities,
        canManageTransactions,
        canManageUsers,
        canProcessTransactions,
        login,
        logout,
        refreshAuth: checkAuth,
      }}
    >
      {children}
    </AuthContext.Provider>
  );
};

export function useAuth(): AuthContextType {
  const context = useContext(AuthContext);
  if (!context) {
    throw new Error('useAuth must be used within an AuthProvider');
  }
  return context;
}
