import React, { useState, useEffect, useCallback } from 'react';
import { getEntityBalances } from '../api/processing';
import { getEntities } from '../api/entities';
import { EntityBalance, Entity } from '../types';
import { formatPaiseToINR } from '../utils/currency';
import { Badge } from '../components/common/Badge';
import { TableSkeleton } from '../components/common/LoadingSkeleton';
import { EmptyState } from '../components/common/EmptyState';
import { ErrorState } from '../components/common/ErrorState';
import { Wallet, TrendingUp, TrendingDown, Scale, RefreshCw, Search } from 'lucide-react';
import { ResponsiveContainer, PieChart, Pie, Cell, Tooltip } from 'recharts';

export const BalancesPage: React.FC = () => {
  const [balances, setBalances] = useState<EntityBalance[]>([]);
  const [entities, setEntities] = useState<{ [id: number]: Entity }>({});
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [search, setSearch] = useState('');
  const [positionFilter, setPositionFilter] = useState<'ALL' | 'CREDITOR' | 'DEBTOR' | 'SETTLED'>('ALL');

  const fetchBalances = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const [balRes, entRes] = await Promise.all([
        getEntityBalances(),
        getEntities({ limit: 100 }),
      ]);

      if (balRes && balRes.balances) {
        setBalances(balRes.balances);
      }
      if (entRes && entRes.data) {
        const map: { [id: number]: Entity } = {};
        entRes.data.forEach((e) => {
          map[e.id] = e;
        });
        setEntities(map);
      }
    } catch (err: any) {
      setError(err.message || 'Failed to calculate entity balances');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchBalances();
  }, [fetchBalances]);

  // Aggregate metrics
  const creditors = balances.filter((b) => b.balance_paise > 0);
  const debtors = balances.filter((b) => b.balance_paise < 0);
  const settled = balances.filter((b) => b.balance_paise === 0);

  const totalReceivablesPaise = creditors.reduce((sum, b) => sum + b.balance_paise, 0);
  const totalPayablesPaise = Math.abs(debtors.reduce((sum, b) => sum + b.balance_paise, 0));

  const pieData = [
    { name: 'Receivables (Creditors)', value: totalReceivablesPaise / 100, color: '#10B981' },
    { name: 'Payables (Debtors)', value: totalPayablesPaise / 100, color: '#F43F5E' },
  ];

  const filteredBalances = balances.filter((b) => {
    const ent = entities[b.entity_id];
    const nameMatch = ent ? ent.name.toLowerCase().includes(search.toLowerCase()) : false;
    const idMatch = String(b.entity_id).includes(search);

    if (search && !nameMatch && !idMatch) return false;

    if (positionFilter === 'CREDITOR') return b.balance_paise > 0;
    if (positionFilter === 'DEBTOR') return b.balance_paise < 0;
    if (positionFilter === 'SETTLED') return b.balance_paise === 0;
    return true;
  });

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-bold text-white tracking-tight">Entity Balances</h2>
          <p className="text-xs text-slate-400 mt-0.5">
            Real-time balance ledger derived from completed transactions
          </p>
        </div>

        <button
          onClick={fetchBalances}
          disabled={loading}
          className="inline-flex items-center gap-2 px-3.5 py-2 rounded-xl text-xs font-semibold bg-fintech-card border border-fintech-border text-slate-300 hover:text-white hover:bg-fintech-elevated transition-colors"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} />
          <span>Refresh Balances</span>
        </button>
      </div>

      {/* Summary Cards */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        <div className="bg-fintech-card/90 border border-emerald-500/20 rounded-2xl p-5 shadow-fintech-card">
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-slate-400">Total Receivables</span>
            <div className="p-2 bg-emerald-950/40 rounded-xl border border-emerald-500/30 text-emerald-400">
              <TrendingUp className="w-5 h-5" />
            </div>
          </div>
          <div className="text-2xl font-bold text-emerald-400 font-mono tracking-tight">
            {formatPaiseToINR(totalReceivablesPaise)}
          </div>
          <div className="text-[11px] text-slate-400 mt-1">
            Across {creditors.length} net creditor {creditors.length === 1 ? 'entity' : 'entities'}
          </div>
        </div>

        <div className="bg-fintech-card/90 border border-rose-500/20 rounded-2xl p-5 shadow-fintech-card">
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-slate-400">Total Payables</span>
            <div className="p-2 bg-rose-950/40 rounded-xl border border-rose-500/30 text-rose-400">
              <TrendingDown className="w-5 h-5" />
            </div>
          </div>
          <div className="text-2xl font-bold text-rose-400 font-mono tracking-tight">
            {formatPaiseToINR(totalPayablesPaise)}
          </div>
          <div className="text-[11px] text-slate-400 mt-1">
            Across {debtors.length} net debtor {debtors.length === 1 ? 'entity' : 'entities'}
          </div>
        </div>

        <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card flex items-center justify-between">
          <div>
            <span className="text-xs font-medium text-slate-400">System Balance Parity</span>
            <div className="text-lg font-bold text-white mt-1">
              {totalReceivablesPaise === totalPayablesPaise ? 'Consistently Balanced' : 'Imbalance Detected'}
            </div>
            <div className="text-[11px] text-slate-400 mt-1">
              Zero-sum invariant: Σ Receivables = Σ Payables
            </div>
          </div>
          <div className="p-3 bg-fintech-surface rounded-2xl border border-fintech-border text-emerald-400">
            <Scale className="w-6 h-6" />
          </div>
        </div>
      </div>

      {/* Filter and Search */}
      <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle p-4 rounded-2xl flex flex-col sm:flex-row items-center justify-between gap-4">
        <div className="relative w-full sm:w-72">
          <Search className="w-4 h-4 text-slate-500 absolute left-3.5 top-3" />
          <input
            type="text"
            placeholder="Search entity name or ID..."
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            className="w-full pl-10 pr-4 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500 transition-colors"
          />
        </div>

        <div className="flex items-center gap-1 bg-fintech-surface p-1 rounded-xl border border-fintech-border">
          {(['ALL', 'CREDITOR', 'DEBTOR', 'SETTLED'] as const).map((pos) => (
            <button
              key={pos}
              onClick={() => setPositionFilter(pos)}
              className={`px-3 py-1 rounded-lg text-xs font-medium capitalize transition-all ${
                positionFilter === pos
                  ? 'bg-fintech-elevated text-emerald-400 shadow-sm'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              {pos.toLowerCase()}
            </button>
          ))}
        </div>
      </div>

      {/* Ledger Table */}
      {error ? (
        <ErrorState message={error} onRetry={fetchBalances} />
      ) : loading ? (
        <TableSkeleton rows={6} cols={5} />
      ) : filteredBalances.length === 0 ? (
        <EmptyState
          title="No Balances to Display"
          description="There are currently no entity balance records matching your filter."
        />
      ) : (
        <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl overflow-hidden shadow-fintech-card">
          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs">
              <thead>
                <tr className="text-slate-400 border-b border-fintech-borderSubtle bg-fintech-surface/40">
                  <th className="py-3.5 px-6 font-medium">Entity ID</th>
                  <th className="py-3.5 px-6 font-medium">Entity Name</th>
                  <th className="py-3.5 px-6 font-medium">Type</th>
                  <th className="py-3.5 px-6 font-medium">Financial Position</th>
                  <th className="py-3.5 px-6 font-medium text-right">Net Balance (Paise Exact)</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-fintech-borderSubtle">
                {filteredBalances.map((b) => {
                  const ent = entities[b.entity_id];
                  const isCreditor = b.balance_paise > 0;
                  const isDebtor = b.balance_paise < 0;

                  return (
                    <tr key={b.entity_id} className="hover:bg-fintech-surface/40 transition-colors">
                      <td className="py-3.5 px-6 font-mono font-bold text-white">#{b.entity_id}</td>
                      <td className="py-3.5 px-6">
                        <span className="font-semibold text-white">{ent?.name || `Entity #${b.entity_id}`}</span>
                      </td>
                      <td className="py-3.5 px-6">
                        <span className="px-2 py-0.5 rounded-lg bg-fintech-surface text-slate-300 border border-fintech-border text-[11px]">
                          {ent?.entity_type || 'Unknown'}
                        </span>
                      </td>
                      <td className="py-3.5 px-6">
                        {isCreditor ? (
                          <Badge variant="Creditor">Net Creditor</Badge>
                        ) : isDebtor ? (
                          <Badge variant="Debtor">Net Debtor</Badge>
                        ) : (
                          <Badge variant="Settled">Settled</Badge>
                        )}
                      </td>
                      <td className="py-3.5 px-6 font-mono font-bold text-right">
                        <span className={`text-sm ${isCreditor ? 'text-emerald-400' : isDebtor ? 'text-rose-400' : 'text-slate-400'}`}>
                          {formatPaiseToINR(b.balance_paise)}
                        </span>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        </div>
      )}
    </div>
  );
};
