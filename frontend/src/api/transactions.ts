import { apiClient } from './client';
import { Transaction, TransactionFilter, Pagination } from '../types';

export interface TransactionListResponse {
  success: boolean;
  data: Transaction[];
  pagination: Pagination;
  error?: string;
}

export interface TransactionRecentResponse {
  success: boolean;
  data: Transaction[];
  count: number;
  error?: string;
}

export interface TransactionResponse {
  success: boolean;
  idempotent_replay?: boolean;
  transaction: Transaction;
  error?: string;
}

export interface CreateTransactionParams {
  sender_entity_id: number;
  receiver_entity_id: number;
  amount: string;
  currency?: string;
  idempotency_key: string;
}

export async function getTransactions(filter: TransactionFilter = {}): Promise<TransactionListResponse> {
  const query = new URLSearchParams();
  if (filter.page) query.set('page', String(filter.page));
  if (filter.limit) query.set('limit', String(filter.limit));
  if (filter.id) query.set('id', String(filter.id));
  if (filter.entity_id) query.set('entity_id', String(filter.entity_id));
  if (filter.direction) query.set('direction', filter.direction);
  if (filter.status) query.set('status', filter.status);
  if (filter.from_date) query.set('from_date', filter.from_date);
  if (filter.to_date) query.set('to_date', filter.to_date);
  if (filter.min_amount) query.set('min_amount', filter.min_amount);
  if (filter.max_amount) query.set('max_amount', filter.max_amount);
  if (filter.currency) query.set('currency', filter.currency);
  if (filter.sort_by) query.set('sort_by', filter.sort_by);
  if (filter.order) query.set('order', filter.order);

  const qs = query.toString();
  return apiClient<TransactionListResponse>(`/api/transactions${qs ? `?${qs}` : ''}`);
}

export async function getRecentTransactions(limit: number = 20): Promise<TransactionRecentResponse> {
  return apiClient<TransactionRecentResponse>(`/api/transactions/recent?limit=${limit}`);
}

export async function getTransactionById(id: number): Promise<TransactionResponse> {
  return apiClient<TransactionResponse>(`/api/transactions/${id}`);
}

export async function createTransaction(params: CreateTransactionParams): Promise<TransactionResponse> {
  return apiClient<TransactionResponse>('/api/transactions', {
    method: 'POST',
    idempotencyKey: params.idempotency_key,
    body: JSON.stringify({
      sender_entity_id: params.sender_entity_id,
      receiver_entity_id: params.receiver_entity_id,
      amount: params.amount,
      currency: params.currency || 'INR',
    }),
  });
}

export async function createReversal(origTxId: number, idempotencyKey: string): Promise<TransactionResponse> {
  return apiClient<TransactionResponse>(`/api/transactions/${origTxId}/reversal`, {
    method: 'POST',
    idempotencyKey,
  });
}
