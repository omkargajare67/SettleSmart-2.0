import { apiClient } from './client';
import { Transaction, ProcessingStatus, EntityBalance, SettlementPlanResponse } from '../types';

export interface ProcessNextResponse {
  success: boolean;
  message: string;
  transaction: Transaction;
  error?: string;
}

export interface ProcessBatchResponse {
  success: boolean;
  message: string;
  processed_count: number;
  error?: string;
}

export interface ProcessingStatusResponse {
  success: boolean;
  status: ProcessingStatus;
  error?: string;
}

export interface PendingPriorityResponse {
  success: boolean;
  count: number;
  transactions: Transaction[];
  error?: string;
}

export interface EntityBalancesResponse {
  success: boolean;
  count: number;
  balances: EntityBalance[];
  error?: string;
}

export async function processNext(): Promise<ProcessNextResponse> {
  return apiClient<ProcessNextResponse>('/api/transactions/process-next', {
    method: 'POST',
  });
}

export async function processBatch(batchSize: number = 10): Promise<ProcessBatchResponse> {
  return apiClient<ProcessBatchResponse>('/api/transactions/process-batch', {
    method: 'POST',
    body: JSON.stringify({ batch_size: batchSize }),
  });
}

export async function getProcessingStatus(): Promise<ProcessingStatusResponse> {
  return apiClient<ProcessingStatusResponse>('/api/transactions/processing/status');
}

export async function getPendingPriority(limit: number = 50): Promise<PendingPriorityResponse> {
  return apiClient<PendingPriorityResponse>(`/api/transactions/pending/priority?limit=${limit}`);
}

export async function getEntityBalances(): Promise<EntityBalancesResponse> {
  return apiClient<EntityBalancesResponse>('/api/entities/balances');
}

export async function getSettlementRecommendations(): Promise<SettlementPlanResponse> {
  return apiClient<SettlementPlanResponse>('/api/settlements/recommendations');
}

export async function getRecentSorted(
  sortBy: 'timestamp' | 'amount' | 'id' = 'timestamp',
  order: 'asc' | 'desc' = 'desc',
  limit: number = 50
): Promise<{ success: boolean; sort_by: string; order: string; count: number; transactions: Transaction[] }> {
  return apiClient(`/api/transactions/recent/sorted?sort_by=${sortBy}&order=${order}&limit=${limit}`);
}

export async function searchRecentById(id: number): Promise<{ success: boolean; transaction: Transaction }> {
  return apiClient(`/api/transactions/recent/search?id=${id}`);
}
