import { apiClient } from './client';
import { Entity, EntityFilter, Pagination, EntityType, EntityStatus } from '../types';

export interface EntityListResponse {
  success: boolean;
  data: Entity[];
  pagination: Pagination;
  error?: string;
}

export interface EntityResponse {
  success: boolean;
  entity: Entity;
  error?: string;
}

export async function getEntities(filter: EntityFilter = {}): Promise<EntityListResponse> {
  const query = new URLSearchParams();
  if (filter.page) query.set('page', String(filter.page));
  if (filter.limit) query.set('limit', String(filter.limit));
  if (filter.search) query.set('search', filter.search);
  if (filter.type) query.set('type', filter.type);
  if (filter.status) query.set('status', filter.status);
  if (filter.sort_by) query.set('sort_by', filter.sort_by);
  if (filter.order) query.set('order', filter.order);

  const qs = query.toString();
  return apiClient<EntityListResponse>(`/api/entities${qs ? `?${qs}` : ''}`);
}

export async function getEntityById(id: number): Promise<EntityResponse> {
  return apiClient<EntityResponse>(`/api/entities/${id}`);
}

export async function createEntity(name: string, entity_type: EntityType): Promise<EntityResponse> {
  return apiClient<EntityResponse>('/api/entities', {
    method: 'POST',
    body: JSON.stringify({ name, entity_type }),
  });
}

export async function updateEntity(id: number, name?: string, entity_type?: EntityType): Promise<EntityResponse> {
  const payload: any = {};
  if (name) payload.name = name;
  if (entity_type) payload.entity_type = entity_type;

  return apiClient<EntityResponse>(`/api/entities/${id}`, {
    method: 'PATCH',
    body: JSON.stringify(payload),
  });
}

export async function updateEntityStatus(id: number, status: EntityStatus): Promise<EntityResponse> {
  return apiClient<EntityResponse>(`/api/entities/${id}/status`, {
    method: 'PATCH',
    body: JSON.stringify({ status }),
  });
}
