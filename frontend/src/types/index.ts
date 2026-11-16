export type UserRole = 'Admin' | 'Finance_Manager' | 'Viewer';

export interface User {
  id: number;
  organization_id: number;
  email: string;
  role: UserRole;
  status?: string;
}

export interface OrganizationUser {
  id: number;
  email: string;
  role: UserRole;
  status: 'Active' | 'Inactive';
}

export type EntityType = 'Department' | 'Vendor' | 'Subsidiary' | 'Client' | 'Partner' | 'Unknown';
export type EntityStatus = 'Active' | 'Inactive';

export interface Entity {
  id: number;
  organization_id: number;
  name: string;
  entity_type: EntityType;
  status: EntityStatus;
  created_by_user_id: number;
  updated_by_user_id: number;
  created_at: string;
  updated_at: string;
}

export interface EntityFilter {
  page?: number;
  limit?: number;
  search?: string;
  type?: EntityType | '';
  status?: EntityStatus | '';
  sort_by?: string;
  order?: 'asc' | 'desc';
}

export interface Pagination {
  page: number;
  limit: number;
  total_records: number;
  total_pages: number;
}

export type TransactionStatus = 'Pending' | 'Completed' | 'Reversed' | 'Disputed';
export type TransactionType = 'Standard' | 'Reversal';

export interface Transaction {
  id: number;
  organization_id: number;
  sender_entity_id: number;
  receiver_entity_id: number;
  amount: string;
  currency: string;
  status: TransactionStatus;
  type: TransactionType;
  idempotency_key: string;
  request_fingerprint?: string;
  reversal_of_id: number | null;
  created_by_user_id: number | null;
  created_at: string;
}

export interface TransactionFilter {
  page?: number;
  limit?: number;
  id?: number;
  entity_id?: number;
  direction?: 'incoming' | 'outgoing' | 'all';
  status?: TransactionStatus | '';
  from_date?: string;
  to_date?: string;
  min_amount?: string;
  max_amount?: string;
  currency?: string;
  sort_by?: string;
  order?: 'asc' | 'desc';
}

export interface ProcessingStatus {
  organization_id: number;
  pending_count: number;
  completed_count: number;
  reversed_count: number;
  disputed_count: number;
  total_count: number;
}

export interface EntityBalance {
  entity_id: number;
  balance_paise: number;
  balance_str: string;
}

export interface SettlementRecommendation {
  from_entity_id: number;
  to_entity_id: number;
  amount_paise: number;
  amount_str: string;
}

export interface SettlementPlanResponse {
  success: boolean;
  total_settled_paise: number;
  total_settled_str: string;
  recommendation_count: number;
  recommendations: SettlementRecommendation[];
}

export interface GraphNode {
  id: number;
  name: string;
  type: EntityType;
  status: EntityStatus;
  balance_paise: number;
}

export interface GraphEdge {
  id?: number;
  tx_id?: number;
  sender_id?: number;
  from_id?: number;
  receiver_id?: number;
  to_id?: number;
  amount_paise: number;
  amount_str?: string;
  amount_formatted?: string;
  currency?: string;
  status: TransactionStatus;
  created_at: string;
  reversal_of_id?: number | null;
}

export interface GraphSnapshotResponse {
  success: boolean;
  event_sequence_watermark: number;
  nodes: GraphNode[];
  edges: GraphEdge[];
  pagination: {
    page: number;
    limit: number;
    total_edges: number;
    total_pages: number;
  };
}

export type WebSocketState = 'LIVE' | 'RECONNECTING' | 'OFFLINE';

export interface WebSocketEventMessage {
  event_type: 'TRANSACTION_CREATED' | 'TRANSACTION_STATUS_CHANGED' | 'POLL_SYNC';
  sequence: number;
  payload?: {
    transaction_id?: number;
    id?: number;
    organization_id?: number;
    sender_entity_id?: number;
    receiver_entity_id?: number;
    amount?: string;
    amount_str?: string;
    currency?: string;
    status?: TransactionStatus;
    type?: TransactionType;
    reversal_of_id?: number | null;
    created_at?: string;
  };
}
