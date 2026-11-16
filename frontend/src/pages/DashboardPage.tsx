import React, { useState, useEffect, useCallback } from 'react';
import { useAuth } from '../contexts/AuthContext';
import { useWebSocket } from '../contexts/WebSocketContext';
import { useToast } from '../contexts/ToastContext';
import { KPICards } from '../components/dashboard/KPICards';
import { ActivityChart } from '../components/dashboard/ActivityChart';
import { StatusOverview } from '../components/dashboard/StatusOverview';
import { PriorityQueuePreview } from '../components/dashboard/PriorityQueuePreview';
import { RecentTransactionsTable } from '../components/dashboard/RecentTransactionsTable';
import { Drawer } from '../components/common/Drawer';
import { Badge } from '../components/common/Badge';
import { CardSkeleton } from '../components/common/LoadingSkeleton';
import { ErrorState } from '../components/common/ErrorState';
import {
  getProcessingStatus,
  getPendingPriority,
  getEntityBalances,
  processNext,
  processBatch,
} from '../api/processing';
import { getEntities } from '../api/entities';
import { getRecentTransactions } from '../api/transactions';
import {
  Transaction,
  ProcessingStatus,
  EntityBalance,
} from '../types';
import { formatAmountString, formatPaiseToINR } from '../utils/currency';
import { RefreshCw, Calendar, ArrowRight, User } from 'lucide-react';

export const DashboardPage: React.FC = () => {
  const { user, organizationId } = useAuth();
  const { subscribeToEvents } = useWebSocket();
  const { showToast } = useToast();

  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [isProcessing, setIsProcessing] = useState(false);

  const [totalEntities, setTotalEntities] = useState(0);
  const [procStatus, setProcStatus] = useState<ProcessingStatus | null>(null);
  const [pendingPriority, setPendingPriority] = useState<Transaction[]>([]);
  const [recentTransactions, setRecentTransactions] = useState<Transaction[]>([]);
  const [balances, setBalances] = useState<EntityBalance[]>([]);
  const [selectedTx, setSelectedTx] = useState<Transaction | null>(null);

  const fetchDashboardData = useCallback(async () => {
    setError(null);
    try {
      const [entitiesRes, statusRes, priorityRes, recentRes, balancesRes] = await Promise.all([
        getEntities({ limit: 1 }),
        getProcessingStatus(),
        getPendingPriority(20),
        getRecentTransactions(20),
        getEntityBalances(),
      ]);

      if (entitiesRes && entitiesRes.pagination) {
        setTotalEntities(entitiesRes.pagination.total_records);
      }
      if (statusRes && statusRes.status) {
        setProcStatus(statusRes.status);
      }
      if (priorityRes && priorityRes.transactions) {
        setPendingPriority(priorityRes.transactions);
      }
      if (recentRes && recentRes.data) {
        setRecentTransactions(recentRes.data);
      }
      if (balancesRes && balancesRes.balances) {
        setBalances(balancesRes.balances);
      }
    } catch (err: any) {
      setError(err.message || 'Failed to load dashboard metrics');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchDashboardData();
  }, [fetchDashboardData]);

  // Subscribe to live WebSocket events to automatically refresh dashboard
  useEffect(() => {
    const unsubscribe = subscribeToEvents(() => {
      fetchDashboardData();
    });
    return unsubscribe;
  }, [subscribeToEvents, fetchDashboardData]);

  // Process Next Handler
  const handleProcessNext = async () => {
    setIsProcessing(true);
    try {
      const res = await processNext();
      if (res && res.success) {
        showToast(`Transaction #${res.transaction?.id} processed successfully!`, 'success');
        fetchDashboardData();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to process transaction', 'error');
    } finally {
      setIsProcessing(false);
    }
  };

  // Process Batch Handler
  const handleProcessBatch = async () => {
    setIsProcessing(true);
    try {
      const res = await processBatch(10);
      if (res && res.success) {
        showToast(`Batch completed: ${res.processed_count} transactions processed!`, 'success');
        fetchDashboardData();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to process batch', 'error');
    } finally {
      setIsProcessing(false);
    }
  };

  // Compute total receivables and payables from balances
  const totalReceivablesPaise = balances
    .filter((b) => b.balance_paise > 0)
    .reduce((sum, b) => sum + b.balance_paise, 0);

  const totalPayablesPaise = Math.abs(
    balances
      .filter((b) => b.balance_paise < 0)
      .reduce((sum, b) => sum + b.balance_paise, 0)
  );

  const greetingTime = () => {
    const hr = new Date().getHours();
    if (hr < 12) return 'Good morning';
    if (hr < 18) return 'Good afternoon';
    return 'Good evening';
  };

  const formattedDate = new Date().toLocaleDateString('en-US', {
    weekday: 'long',
    year: 'numeric',
    month: 'long',
    day: 'numeric',
  });

  return (
    <div className="space-y-6">
      {/* Welcome Banner */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 bg-gradient-to-r from-fintech-card via-fintech-surface to-fintech-card p-6 rounded-3xl border border-fintech-borderSubtle shadow-fintech-card">
        <div>
          <h2 className="text-2xl font-bold text-white tracking-tight">
            {greetingTime()}, {user?.email?.split('@')[0] || 'Administrator'}
          </h2>
          <p className="text-xs text-slate-400 mt-1">
            Here is your organization's financial overview for today.
          </p>
        </div>

        <div className="flex items-center gap-3">
          <div className="flex items-center gap-2 px-3 py-1.5 rounded-xl bg-fintech-dark/60 border border-fintech-border text-xs text-slate-300">
            <Calendar className="w-3.5 h-3.5 text-emerald-400" />
            <span>{formattedDate}</span>
          </div>

          <button
            onClick={fetchDashboardData}
            disabled={loading}
            className="p-2 rounded-xl bg-fintech-dark/60 border border-fintech-border text-slate-400 hover:text-white hover:bg-fintech-elevated transition-colors"
            title="Refresh metrics"
          >
            <RefreshCw className={`w-4 h-4 ${loading ? 'animate-spin' : ''}`} />
          </button>
        </div>
      </div>

      {error ? (
        <ErrorState message={error} onRetry={fetchDashboardData} />
      ) : (
        <>
          {/* KPI Metrics */}
          <KPICards
            totalEntities={totalEntities}
            totalTransactions={procStatus?.total_count || 0}
            pendingTransactions={procStatus?.pending_count || 0}
            completedTransactions={procStatus?.completed_count || 0}
            totalReceivablesPaise={totalReceivablesPaise}
            totalPayablesPaise={totalPayablesPaise}
            isLoading={loading}
          />

          {/* Activity Chart & Status Overview */}
          <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <div className="lg:col-span-2">
              <ActivityChart transactions={recentTransactions} />
            </div>
            <div>
              <StatusOverview status={procStatus} />
            </div>
          </div>

          {/* Priority Queue & Recent Transactions */}
          <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <div>
              <PriorityQueuePreview
                pendingTransactions={pendingPriority}
                onProcessNext={handleProcessNext}
                onProcessBatch={handleProcessBatch}
                isProcessing={isProcessing}
              />
            </div>
            <div className="lg:col-span-2">
              <RecentTransactionsTable
                transactions={recentTransactions}
                onSelectTransaction={(tx) => setSelectedTx(tx)}
              />
            </div>
          </div>
        </>
      )}

      {/* Transaction Detail Drawer */}
      <Drawer
        isOpen={!!selectedTx}
        onClose={() => setSelectedTx(null)}
        title={`Transaction #${selectedTx?.id}`}
        subtitle="Immutable financial record"
      >
        {selectedTx && (
          <div className="space-y-5 text-xs">
            <div className="flex items-center justify-between p-3 rounded-xl bg-fintech-card border border-fintech-border">
              <span className="text-slate-400">Current Status:</span>
              <Badge variant={selectedTx.status}>{selectedTx.status}</Badge>
            </div>

            <div className="p-4 rounded-xl bg-fintech-card border border-fintech-border text-center">
              <span className="text-slate-400 text-[11px] block mb-1">Transaction Amount</span>
              <span className="text-2xl font-bold font-mono text-white">
                {formatAmountString(selectedTx.amount, selectedTx.currency)}
              </span>
            </div>

            <div className="space-y-3">
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Sender Entity:</span>
                <span className="text-white font-medium">Entity #{selectedTx.sender_entity_id}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Receiver Entity:</span>
                <span className="text-white font-medium">Entity #{selectedTx.receiver_entity_id}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Transaction Type:</span>
                <span className="text-white">{selectedTx.type}</span>
              </div>
              {selectedTx.reversal_of_id && (
                <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                  <span className="text-slate-400">Reversal Of:</span>
                  <span className="text-rose-400 font-mono">Tx #{selectedTx.reversal_of_id}</span>
                </div>
              )}
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Idempotency Key:</span>
                <span className="text-slate-300 font-mono truncate max-w-[200px]" title={selectedTx.idempotency_key}>
                  {selectedTx.idempotency_key}
                </span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Created At:</span>
                <span className="text-slate-300 font-mono">{new Date(selectedTx.created_at).toLocaleString()}</span>
              </div>
            </div>
          </div>
        )}
      </Drawer>
    </div>
  );
};
