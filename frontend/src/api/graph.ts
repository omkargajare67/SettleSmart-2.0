import { apiClient } from './client';
import { GraphSnapshotResponse } from '../types';

export interface GraphFilter {
  page?: number;
  limit?: number;
  entity_id?: number;
  status?: string;
  from_date?: string;
  to_date?: string;
}

export async function getGraphSnapshot(filter: GraphFilter = {}): Promise<GraphSnapshotResponse> {
  const query = new URLSearchParams();
  if (filter.page) query.set('page', String(filter.page));
  if (filter.limit) query.set('limit', String(filter.limit));
  if (filter.entity_id) query.set('entity_id', String(filter.entity_id));
  if (filter.status) query.set('status', filter.status);
  if (filter.from_date) query.set('from_date', filter.from_date);
  if (filter.to_date) query.set('to_date', filter.to_date);

  const qs = query.toString();
  return apiClient<GraphSnapshotResponse>(`/api/graph${qs ? `?${qs}` : ''}`);
}
