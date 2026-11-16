import React, { useState, useEffect, useCallback } from 'react';
import { getSettlementRecommendations, getEntityBalances } from '../api/processing';
import { SettlementPlanResponse, EntityBalance } from '../types';
import { SettlementFlowChart } from '../components/settlement/SettlementFlowChart';
import { CardSkeleton } from '../components/common/LoadingSkeleton';
import { ErrorState } from '../components/common/ErrorState';
import { formatAmountString, formatPaiseToINR } from '../utils/currency';
import { GitFork, RefreshCw, Users, DollarSign, CheckCircle2, ArrowRight } from 'lucide-react';

export const SettlementPage: React.FC = () => {
  const [plan, setPlan] = useState<SettlementPlanResponse | null>(null);
  const [balances, setBalances] = useState<EntityBalance[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const fetchSettlement = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const [planRes, balRes] = await Promise.all([
        getSettlementRecommendations(),
        getEntityBalances(),
      ]);

      if (planRes && planRes.success) {
        setPlan(planRes);
      }
      if (balRes && balRes.balances) {
        setBalances(balRes.balances);
      }
    } catch (err: any) {
      setError(err.message || 'Failed to calculate settlement recommendations');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchSettlement();
  }, [fetchSettlement]);

  const debtorCount = balances.filter((b) => b.balance_paise < 0).length;
  const creditorCount = balances.filter((b) => b.balance_paise > 0).length;
  const totalSettledPaise = plan?.total_settled_paise || 0;
  const recommendationCount = plan?.recommendation_count || 0;

  return (
    <div className="space-y-6">
      {/* Page Header */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-bold text-white tracking-tight">Settlement Recommendations</h2>
          <p className="text-xs text-slate-400 mt-0.5">
            Optimized minimal-transfer plan computed via the Greedy Settlement Algorithm
          </p>
        </div>

        <button
          onClick={fetchSettlement}
          disabled={loading}
          className="inline-flex items-center gap-2 px-3.5 py-2 rounded-xl text-xs font-semibold bg-fintech-card border border-fintech-border text-slate-300 hover:text-white hover:bg-fintech-elevated transition-colors"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} />
          <span>Recalculate Plan</span>
        </button>
      </div>

      {/* Summary KPI Cards */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-slate-400">Total Debtors</span>
            <div className="p-2 bg-rose-950/40 rounded-xl border border-rose-500/30 text-rose-400">
              <Users className="w-4 h-4" />
            </div>
          </div>
          <div className="text-2xl font-bold text-white font-mono">
            {debtorCount}
          </div>
          <div className="text-[11px] text-slate-400 mt-1">Entities with negative net balance</div>
        </div>

        <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-slate-400">Total Creditors</span>
            <div className="p-2 bg-emerald-950/40 rounded-xl border border-emerald-500/30 text-emerald-400">
              <Users className="w-4 h-4" />
            </div>
          </div>
          <div className="text-2xl font-bold text-white font-mono">
            {creditorCount}
          </div>
          <div className="text-[11px] text-slate-400 mt-1">Entities with positive net balance</div>
        </div>

        <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-slate-400">Total Settled Volume</span>
            <div className="p-2 bg-blue-950/40 rounded-xl border border-blue-500/30 text-blue-400">
              <DollarSign className="w-4 h-4" />
            </div>
          </div>
          <div className="text-2xl font-bold text-white font-mono">
            {formatPaiseToINR(totalSettledPaise)}
          </div>
          <div className="text-[11px] text-slate-400 mt-1">Total debt resolved across plan</div>
        </div>

        <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-slate-400">Suggested Transfers</span>
            <div className="p-2 bg-teal-950/40 rounded-xl border border-teal-500/30 text-teal-400">
              <GitFork className="w-4 h-4" />
            </div>
          </div>
          <div className="text-2xl font-bold text-emerald-400 font-mono">
            {recommendationCount}
          </div>
          <div className="text-[11px] text-slate-400 mt-1">Minimal transfers needed</div>
        </div>
      </div>

      {/* Main Flow Section */}
      {error ? (
        <ErrorState message={error} onRetry={fetchSettlement} />
      ) : loading ? (
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
          <CardSkeleton count={4} />
        </div>
      ) : (
        <SettlementFlowChart plan={plan} isLoading={loading} />
      )}
    </div>
  );
};
