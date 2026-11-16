import React, { useState, useEffect, useCallback } from 'react';
import { useAuth } from '../contexts/AuthContext';
import { useWebSocket } from '../contexts/WebSocketContext';
import { useToast } from '../contexts/ToastContext';
import {
  getTransactions,
  createTransaction,
  createReversal,
} from '../api/transactions';
import { getEntities } from '../api/entities';
import { processNext, processBatch } from '../api/processing';
import { Transaction, Entity, TransactionStatus, TransactionFilter } from '../types';
import { Badge } from '../components/common/Badge';
import { Modal } from '../components/common/Modal';
import { Drawer } from '../components/common/Drawer';
import { TableSkeleton } from '../components/common/LoadingSkeleton';
import { EmptyState } from '../components/common/EmptyState';
import { ErrorState } from '../components/common/ErrorState';
import { formatAmountString } from '../utils/currency';
import { generateUUIDv4 } from '../utils/uuid';
import {
  ArrowLeftRight,
  Plus,
  Play,
  FastForward,
  RotateCcw,
  Search,
  Filter,
  Eye,
  ChevronLeft,
  ChevronRight,
  ArrowRight,
  Clock,
  CheckCircle2,
  AlertCircle,
} from 'lucide-react';

export const TransactionsPage: React.FC = () => {
  const { canManageTransactions, canProcessTransactions } = useAuth();
  const { subscribeToEvents } = useWebSocket();
  const { showToast } = useToast();

  const [transactions, setTransactions] = useState<Transaction[]>([]);
  const [entities, setEntities] = useState<Entity[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);

  // Filters & Pagination
  const [searchId, setSearchId] = useState('');
  const [statusFilter, setStatusFilter] = useState<string>('');
  const [entityFilter, setEntityFilter] = useState<string>('');
  const [sortBy, setSortBy] = useState<string>('created_at');
  const [order, setOrder] = useState<'asc' | 'desc'>('desc');
  const [page, setPage] = useState(1);
  const [totalPages, setTotalPages] = useState(1);
  const [totalRecords, setTotalRecords] = useState(0);

  // Modals & Drawers
  const [createModalOpen, setCreateModalOpen] = useState(false);
  const [reversalModalTx, setReversalModalTx] = useState<Transaction | null>(null);
  const [detailDrawerTx, setDetailDrawerTx] = useState<Transaction | null>(null);

  // Form states
  const [senderId, setSenderId] = useState<number | ''>('');
  const [receiverId, setReceiverId] = useState<number | ''>('');
  const [amount, setAmount] = useState('');
  const [currency, setCurrency] = useState('INR');
  const [submitting, setSubmitting] = useState(false);

  // Fetch Entities for dropdowns
  useEffect(() => {
    getEntities({ limit: 100, status: 'Active' })
      .then((res) => {
        if (res && res.success) setEntities(res.data);
      })
      .catch(() => {});
  }, []);

  const fetchTransactions = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const filter: TransactionFilter = {
        page,
        limit: 15,
        id: searchId.trim() ? parseInt(searchId.trim(), 10) : undefined,
        entity_id: entityFilter ? parseInt(entityFilter, 10) : undefined,
        status: (statusFilter as TransactionStatus) || undefined,
        sort_by: sortBy,
        order,
      };

      const res = await getTransactions(filter);
      if (res && res.success) {
        setTransactions(res.data);
        setTotalPages(res.pagination.total_pages || 1);
        setTotalRecords(res.pagination.total_records || 0);
      }
    } catch (err: any) {
      setError(err.message || 'Failed to load transactions');
    } finally {
      setLoading(false);
    }
  }, [page, searchId, statusFilter, entityFilter, sortBy, order]);

  useEffect(() => {
    fetchTransactions();
  }, [fetchTransactions]);

  // Subscribe to live WebSocket events
  useEffect(() => {
    const unsubscribe = subscribeToEvents(() => {
      fetchTransactions();
    });
    return unsubscribe;
  }, [subscribeToEvents, fetchTransactions]);

  // Create Transaction
  const handleCreateSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!senderId || !receiverId) {
      showToast('Please select both sender and receiver entities', 'error');
      return;
    }
    if (senderId === receiverId) {
      showToast('Sender and receiver entities must be distinct', 'error');
      return;
    }
    const parsedAmount = parseFloat(amount);
    if (isNaN(parsedAmount) || parsedAmount <= 0) {
      showToast('Amount must be a positive decimal number', 'error');
      return;
    }

    setSubmitting(true);
    const idempotencyKey = generateUUIDv4();

    try {
      const res = await createTransaction({
        sender_entity_id: Number(senderId),
        receiver_entity_id: Number(receiverId),
        amount: parsedAmount.toFixed(2),
        currency,
        idempotency_key: idempotencyKey,
      });

      if (res && res.success) {
        showToast(
          res.idempotent_replay
            ? `Idempotent replay: Transaction #${res.transaction.id} already exists.`
            : `Transaction #${res.transaction.id} recorded in Pending status.`,
          'success'
        );
        setCreateModalOpen(false);
        setSenderId('');
        setReceiverId('');
        setAmount('');
        fetchTransactions();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to record transaction', 'error');
    } finally {
      setSubmitting(false);
    }
  };

  // Reversal Request
  const handleReversalSubmit = async () => {
    if (!reversalModalTx) return;

    setSubmitting(true);
    const idempotencyKey = generateUUIDv4();

    try {
      const res = await createReversal(reversalModalTx.id, idempotencyKey);
      if (res && res.success) {
        showToast(`Reversal transaction #${res.transaction.id} queued for processing!`, 'success');
        setReversalModalTx(null);
        fetchTransactions();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to request reversal', 'error');
    } finally {
      setSubmitting(false);
    }
  };

  // Process Next
  const handleProcessNext = async () => {
    setIsProcessing(true);
    try {
      const res = await processNext();
      if (res && res.success) {
        showToast(`Processed next priority transaction #${res.transaction.id}!`, 'success');
        fetchTransactions();
      }
    } catch (err: any) {
      showToast(err.message || 'Processing failed', 'error');
    } finally {
      setIsProcessing(false);
    }
  };

  // Process Batch
  const handleProcessBatch = async () => {
    setIsProcessing(true);
    try {
      const res = await processBatch(10);
      if (res && res.success) {
        showToast(`Processed batch: ${res.processed_count} transactions completed!`, 'success');
        fetchTransactions();
      }
    } catch (err: any) {
      showToast(err.message || 'Batch processing failed', 'error');
    } finally {
      setIsProcessing(false);
    }
  };

  return (
    <div className="space-y-6">
      {/* Header and Controls */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-bold text-white tracking-tight">Transaction Management</h2>
          <p className="text-xs text-slate-400 mt-0.5">
            Authoritative financial ledger & heap processing ({totalRecords} records)
          </p>
        </div>

        <div className="flex flex-wrap items-center gap-2.5">
          {canProcessTransactions && (
            <>
              <button
                onClick={handleProcessNext}
                disabled={isProcessing}
                className="inline-flex items-center gap-1.5 px-3 py-2 rounded-xl text-xs font-semibold bg-emerald-600 hover:bg-emerald-500 disabled:opacity-50 text-white shadow-fintech-glow transition-all"
                title="Process highest priority transaction via Max-Heap"
              >
                <Play className="w-3.5 h-3.5 fill-current" />
                <span>Process Next</span>
              </button>
              <button
                onClick={handleProcessBatch}
                disabled={isProcessing}
                className="inline-flex items-center gap-1.5 px-3 py-2 rounded-xl text-xs font-medium bg-fintech-surface hover:bg-fintech-elevated disabled:opacity-50 text-slate-200 border border-fintech-border transition-all"
                title="Process batch of 10 transactions"
              >
                <FastForward className="w-3.5 h-3.5" />
                <span>Process Batch</span>
              </button>
            </>
          )}

          {canManageTransactions && (
            <button
              onClick={() => {
                setSenderId('');
                setReceiverId('');
                setAmount('');
                setCreateModalOpen(true);
              }}
              className="inline-flex items-center gap-2 px-4 py-2 rounded-xl font-semibold text-xs text-white bg-gradient-to-r from-emerald-600 to-teal-600 hover:from-emerald-500 hover:to-teal-500 shadow-fintech-glow transition-all"
            >
              <Plus className="w-4 h-4" />
              <span>Record Transaction</span>
            </button>
          )}
        </div>
      </div>

      {/* Filter and Search Bar */}
      <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle p-4 rounded-2xl flex flex-col md:flex-row items-center justify-between gap-4">
        <div className="relative w-full md:w-60">
          <Search className="w-4 h-4 text-slate-500 absolute left-3.5 top-3" />
          <input
            type="number"
            placeholder="Search by Tx ID..."
            value={searchId}
            onChange={(e) => {
              setSearchId(e.target.value);
              setPage(1);
            }}
            className="w-full pl-10 pr-4 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500 font-mono transition-colors"
          />
        </div>

        <div className="flex flex-wrap items-center gap-3 w-full md:w-auto">
          {/* Status Filter */}
          <select
            value={statusFilter}
            onChange={(e) => {
              setStatusFilter(e.target.value);
              setPage(1);
            }}
            className="px-3 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-slate-300 focus:outline-none focus:border-emerald-500"
          >
            <option value="">All Statuses</option>
            <option value="Pending">Pending</option>
            <option value="Completed">Completed</option>
            <option value="Reversed">Reversed</option>
            <option value="Disputed">Disputed</option>
          </select>

          {/* Entity Filter */}
          <select
            value={entityFilter}
            onChange={(e) => {
              setEntityFilter(e.target.value);
              setPage(1);
            }}
            className="px-3 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-slate-300 focus:outline-none focus:border-emerald-500"
          >
            <option value="">All Entities</option>
            {entities.map((ent) => (
              <option key={ent.id} value={ent.id}>
                #{ent.id} {ent.name}
              </option>
            ))}
          </select>

          {/* Sort By */}
          <select
            value={sortBy}
            onChange={(e) => {
              setSortBy(e.target.value);
              setPage(1);
            }}
            className="px-3 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-slate-300 focus:outline-none focus:border-emerald-500"
          >
            <option value="created_at">Sort by Date</option>
            <option value="amount">Sort by Amount</option>
            <option value="id">Sort by ID</option>
          </select>

          <button
            onClick={() => setOrder(order === 'asc' ? 'desc' : 'asc')}
            className="px-3 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-slate-300 hover:text-white"
            title="Toggle sort direction"
          >
            {order === 'asc' ? '↑ Asc' : '↓ Desc'}
          </button>
        </div>
      </div>

      {/* Table or States */}
      {error ? (
        <ErrorState message={error} onRetry={fetchTransactions} />
      ) : loading ? (
        <TableSkeleton rows={8} cols={7} />
      ) : transactions.length === 0 ? (
        <EmptyState
          title="No Transactions Found"
          description="No transactions match your current query. Record a new financial transfer to get started."
          action={
            canManageTransactions ? (
              <button
                onClick={() => setCreateModalOpen(true)}
                className="px-4 py-2 rounded-xl text-xs font-semibold bg-emerald-600 text-white shadow-fintech-glow"
              >
                Record First Transaction
              </button>
            ) : undefined
          }
        />
      ) : (
        <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl overflow-hidden shadow-fintech-card">
          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs">
              <thead>
                <tr className="text-slate-400 border-b border-fintech-borderSubtle bg-fintech-surface/40">
                  <th className="py-3.5 px-6 font-medium">Tx ID</th>
                  <th className="py-3.5 px-6 font-medium">Sender</th>
                  <th className="py-3.5 px-6 font-medium">Receiver</th>
                  <th className="py-3.5 px-6 font-medium">Amount</th>
                  <th className="py-3.5 px-6 font-medium">Type</th>
                  <th className="py-3.5 px-6 font-medium">Status</th>
                  <th className="py-3.5 px-6 font-medium">Timestamp</th>
                  <th className="py-3.5 px-6 font-medium text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-fintech-borderSubtle">
                {transactions.map((tx) => (
                  <tr key={tx.id} className="hover:bg-fintech-surface/40 transition-colors">
                    <td className="py-3.5 px-6 font-mono font-bold text-white">#{tx.id}</td>
                    <td className="py-3.5 px-6 text-slate-300 font-medium">Entity #{tx.sender_entity_id}</td>
                    <td className="py-3.5 px-6 text-slate-300 font-medium">Entity #{tx.receiver_entity_id}</td>
                    <td className="py-3.5 px-6 font-mono font-bold text-white">
                      {formatAmountString(tx.amount, tx.currency)}
                    </td>
                    <td className="py-3.5 px-6">
                      <span className="text-[11px] text-slate-400">{tx.type}</span>
                    </td>
                    <td className="py-3.5 px-6">
                      <Badge variant={tx.status}>{tx.status}</Badge>
                    </td>
                    <td className="py-3.5 px-6 text-slate-400 font-mono">
                      {new Date(tx.created_at).toLocaleDateString([], {
                        month: 'short',
                        day: 'numeric',
                        hour: '2-digit',
                        minute: '2-digit',
                      })}
                    </td>
                    <td className="py-3.5 px-6 text-right">
                      <div className="flex items-center justify-end gap-1.5">
                        <button
                          onClick={() => setDetailDrawerTx(tx)}
                          className="p-1.5 rounded-lg hover:bg-fintech-elevated text-slate-400 hover:text-white transition-colors"
                          title="View details"
                        >
                          <Eye className="w-4 h-4" />
                        </button>

                        {/* Reversal Action: only for completed standard transactions */}
                        {canManageTransactions && tx.status === 'Completed' && tx.type === 'Standard' && (
                          <button
                            onClick={() => setReversalModalTx(tx)}
                            className="p-1.5 rounded-lg hover:bg-red-950/40 text-slate-400 hover:text-rose-400 transition-colors"
                            title="Request reversal"
                          >
                            <RotateCcw className="w-4 h-4" />
                          </button>
                        )}
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>

          {/* Pagination */}
          {totalPages > 1 && (
            <div className="px-6 py-3.5 border-t border-fintech-borderSubtle bg-fintech-surface/20 flex items-center justify-between text-xs text-slate-400">
              <div>
                Showing page <span className="text-white font-medium">{page}</span> of{' '}
                <span className="text-white font-medium">{totalPages}</span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  disabled={page <= 1}
                  onClick={() => setPage(page - 1)}
                  className="p-1.5 rounded-lg bg-fintech-surface hover:bg-fintech-elevated disabled:opacity-30 disabled:cursor-not-allowed border border-fintech-borderSubtle text-slate-300"
                >
                  <ChevronLeft className="w-4 h-4" />
                </button>
                <button
                  disabled={page >= totalPages}
                  onClick={() => setPage(page + 1)}
                  className="p-1.5 rounded-lg bg-fintech-surface hover:bg-fintech-elevated disabled:opacity-30 disabled:cursor-not-allowed border border-fintech-borderSubtle text-slate-300"
                >
                  <ChevronRight className="w-4 h-4" />
                </button>
              </div>
            </div>
          )}
        </div>
      )}

      {/* Record Transaction Modal */}
      <Modal
        isOpen={createModalOpen}
        onClose={() => setCreateModalOpen(false)}
        title="Record Financial Transaction"
        subtitle="Generates a cryptographically unique UUID v4 idempotency key"
      >
        <form onSubmit={handleCreateSubmit} className="space-y-4">
          <div>
            <label className="block text-xs font-semibold text-slate-300 mb-1.5">
              Sender Entity (Debited)
            </label>
            <select
              value={senderId}
              onChange={(e) => setSenderId(Number(e.target.value))}
              required
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-card border border-fintech-border text-white text-xs focus:outline-none focus:border-emerald-500"
            >
              <option value="">Select active sender...</option>
              {entities.map((e) => (
                <option key={e.id} value={e.id}>
                  #{e.id} - {e.name} ({e.entity_type})
                </option>
              ))}
            </select>
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-300 mb-1.5">
              Receiver Entity (Credited)
            </label>
            <select
              value={receiverId}
              onChange={(e) => setReceiverId(Number(e.target.value))}
              required
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-card border border-fintech-border text-white text-xs focus:outline-none focus:border-emerald-500"
            >
              <option value="">Select active receiver...</option>
              {entities
                .filter((e) => e.id !== senderId)
                .map((e) => (
                  <option key={e.id} value={e.id}>
                    #{e.id} - {e.name} ({e.entity_type})
                  </option>
                ))}
            </select>
          </div>

          <div className="grid grid-cols-3 gap-3">
            <div className="col-span-2">
              <label className="block text-xs font-semibold text-slate-300 mb-1.5">
                Amount (INR)
              </label>
              <input
                type="number"
                step="0.01"
                min="0.01"
                placeholder="100.00"
                value={amount}
                onChange={(e) => setAmount(e.target.value)}
                required
                className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-card border border-fintech-border text-white text-xs font-mono focus:outline-none focus:border-emerald-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-300 mb-1.5">
                Currency
              </label>
              <input
                type="text"
                value={currency}
                disabled
                className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-surface border border-fintech-borderSubtle text-slate-400 text-xs font-mono"
              />
            </div>
          </div>

          <div className="p-3 rounded-xl bg-fintech-dark/60 border border-fintech-borderSubtle text-[11px] text-slate-400 space-y-1">
            <div>• New transactions enter <span className="text-amber-400 font-semibold">Pending</span> state.</div>
            <div>• Idempotency key will be automatically submitted via <code className="text-emerald-400 font-mono">Idempotency-Key</code> header.</div>
          </div>

          <div className="flex items-center justify-end gap-3 pt-3">
            <button
              type="button"
              onClick={() => setCreateModalOpen(false)}
              className="px-4 py-2 rounded-xl text-xs font-medium text-slate-300 hover:bg-fintech-elevated"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={submitting}
              className="px-4 py-2 rounded-xl text-xs font-semibold text-white bg-emerald-600 hover:bg-emerald-500 shadow-fintech-glow disabled:opacity-50"
            >
              {submitting ? 'Submitting...' : 'Record Transaction'}
            </button>
          </div>
        </form>
      </Modal>

      {/* Reversal Confirmation Modal */}
      <Modal
        isOpen={!!reversalModalTx}
        onClose={() => setReversalModalTx(null)}
        title="Confirm Transaction Reversal"
        subtitle={`Request reversal for Transaction #${reversalModalTx?.id}`}
      >
        {reversalModalTx && (
          <div className="space-y-4 text-xs">
            <div className="p-4 rounded-xl bg-fintech-card border border-fintech-border space-y-2">
              <div className="flex justify-between">
                <span className="text-slate-400">Transaction ID:</span>
                <span className="text-white font-mono font-bold">#{reversalModalTx.id}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Amount:</span>
                <span className="text-white font-mono font-bold">
                  {formatAmountString(reversalModalTx.amount, reversalModalTx.currency)}
                </span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Sender / Receiver:</span>
                <span className="text-slate-200">
                  Entity #{reversalModalTx.sender_entity_id} &rarr; Entity #{reversalModalTx.receiver_entity_id}
                </span>
              </div>
            </div>

            <div className="p-3 rounded-xl bg-amber-950/20 border border-amber-500/30 text-amber-200 text-[11px] leading-relaxed">
              <AlertCircle className="w-4 h-4 inline mr-1 text-amber-400" />
              Reversal creates a new Pending reversal transaction linked to #{reversalModalTx.id}. The original transaction remains Completed until the reversal is processed by the engine.
            </div>

            <div className="flex items-center justify-end gap-3 pt-3">
              <button
                type="button"
                onClick={() => setReversalModalTx(null)}
                className="px-4 py-2 rounded-xl text-xs font-medium text-slate-300 hover:bg-fintech-elevated"
              >
                Cancel
              </button>
              <button
                onClick={handleReversalSubmit}
                disabled={submitting}
                className="px-4 py-2 rounded-xl text-xs font-semibold text-white bg-rose-600 hover:bg-rose-500 shadow-lg disabled:opacity-50"
              >
                {submitting ? 'Creating Reversal...' : 'Confirm Reversal'}
              </button>
            </div>
          </div>
        )}
      </Modal>

      {/* Transaction Detail Drawer */}
      <Drawer
        isOpen={!!detailDrawerTx}
        onClose={() => setDetailDrawerTx(null)}
        title={`Transaction Details`}
        subtitle={`Transaction #${detailDrawerTx?.id}`}
      >
        {detailDrawerTx && (
          <div className="space-y-6 text-xs">
            <div className="flex items-center justify-between p-3.5 rounded-xl bg-fintech-card border border-fintech-border">
              <span className="text-slate-400 font-medium">Status</span>
              <Badge variant={detailDrawerTx.status}>{detailDrawerTx.status}</Badge>
            </div>

            <div className="p-5 rounded-2xl bg-fintech-card border border-fintech-border text-center">
              <span className="text-slate-400 text-xs block mb-1">Authoritative Amount</span>
              <span className="text-3xl font-bold font-mono text-white">
                {formatAmountString(detailDrawerTx.amount, detailDrawerTx.currency)}
              </span>
            </div>

            {/* Lifecycle Timeline */}
            <div className="p-4 rounded-xl bg-fintech-card border border-fintech-border">
              <h4 className="font-semibold text-white mb-3">Processing Lifecycle</h4>
              <div className="space-y-3 pl-2 border-l-2 border-slate-700">
                <div className="relative pl-4">
                  <div className="absolute -left-[11px] top-0.5 w-3 h-3 rounded-full bg-emerald-500" />
                  <div className="font-semibold text-white">Recorded in MySQL Outbox</div>
                  <div className="text-[10px] text-slate-400 font-mono">
                    {new Date(detailDrawerTx.created_at).toLocaleString()}
                  </div>
                </div>

                <div className="relative pl-4">
                  <div
                    className={`absolute -left-[11px] top-0.5 w-3 h-3 rounded-full ${
                      detailDrawerTx.status === 'Completed'
                        ? 'bg-emerald-500'
                        : detailDrawerTx.status === 'Reversed'
                        ? 'bg-rose-500'
                        : 'bg-amber-500'
                    }`}
                  />
                  <div className="font-semibold text-white">Status: {detailDrawerTx.status}</div>
                  <div className="text-[10px] text-slate-400">
                    {detailDrawerTx.status === 'Pending'
                      ? 'Queued in Max-Heap Priority Queue'
                      : 'Settled in in-memory DSA and graph'}
                  </div>
                </div>
              </div>
            </div>

            {/* Technical Metadata */}
            <div className="space-y-3">
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Sender Entity:</span>
                <span className="text-white font-medium">Entity #{detailDrawerTx.sender_entity_id}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Receiver Entity:</span>
                <span className="text-white font-medium">Entity #{detailDrawerTx.receiver_entity_id}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Transaction Type:</span>
                <span className="text-white">{detailDrawerTx.type}</span>
              </div>
              {detailDrawerTx.reversal_of_id && (
                <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                  <span className="text-slate-400">Reversal Of:</span>
                  <span className="text-rose-400 font-mono">Tx #{detailDrawerTx.reversal_of_id}</span>
                </div>
              )}
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Idempotency Key:</span>
                <span className="text-slate-300 font-mono truncate max-w-[200px]" title={detailDrawerTx.idempotency_key}>
                  {detailDrawerTx.idempotency_key}
                </span>
              </div>
            </div>
          </div>
        )}
      </Drawer>
    </div>
  );
};
