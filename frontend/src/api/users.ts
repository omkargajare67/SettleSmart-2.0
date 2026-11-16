import { apiClient } from './client';
import { OrganizationUser, UserRole } from '../types';

export interface UsersListResponse {
  success: boolean;
  users: OrganizationUser[];
  error?: string;
}

export interface CreateUserResponse {
  success: boolean;
  user_id: number;
  error?: string;
}

export async function getOrganizationUsers(): Promise<UsersListResponse> {
  return apiClient<UsersListResponse>('/api/organization/users');
}

export async function createOrganizationUser(email: string, password: string, role: UserRole): Promise<CreateUserResponse> {
  return apiClient<CreateUserResponse>('/api/organization/users', {
    method: 'POST',
    body: JSON.stringify({ email, password, role }),
  });
}

export async function updateOrganizationUser(
  userId: number,
  updates: { role?: UserRole; status?: 'Active' | 'Inactive' }
): Promise<{ success: boolean; error?: string }> {
  return apiClient(`/api/organization/users/${userId}`, {
    method: 'PATCH',
    body: JSON.stringify(updates),
  });
}
