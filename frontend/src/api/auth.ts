import { apiClient, setCsrfToken } from './client';
import { User } from '../types';

export interface LoginParams {
  organization_id: number;
  email: string;
  password: string;
}

export interface LoginResponse {
  success: boolean;
  csrf_token: string;
  user: User;
  error?: string;
}

export interface RegisterOrgParams {
  organization_name: string;
  admin_email: string;
  admin_password: string;
}

export interface RegisterOrgResponse {
  success: boolean;
  organization_id: number;
  admin_user_id: number;
  error?: string;
}

export async function login(params: LoginParams): Promise<LoginResponse> {
  const data = await apiClient<LoginResponse>('/api/auth/login', {
    method: 'POST',
    body: JSON.stringify(params),
  });
  if (data && data.csrf_token) {
    setCsrfToken(data.csrf_token);
  }
  return data;
}

export async function registerOrganization(params: RegisterOrgParams): Promise<RegisterOrgResponse> {
  return apiClient<RegisterOrgResponse>('/api/auth/register-organization', {
    method: 'POST',
    body: JSON.stringify(params),
  });
}

export async function logout(): Promise<{ success: boolean; message: string }> {
  try {
    return await apiClient<{ success: boolean; message: string }>('/api/auth/logout', {
      method: 'POST',
    });
  } finally {
    setCsrfToken(null);
  }
}

export async function getCurrentUser(): Promise<{ success: boolean; user: User }> {
  return apiClient<{ success: boolean; user: User }>('/api/auth/me');
}

export async function changePassword(current_password: string, new_password: string): Promise<{ success: boolean; message: string }> {
  return apiClient<{ success: boolean; message: string }>('/api/auth/change-password', {
    method: 'POST',
    body: JSON.stringify({
      old_password: current_password,
      new_password,
    }),
  });
}
