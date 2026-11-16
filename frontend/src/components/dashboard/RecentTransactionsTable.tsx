import React from 'react';
import { Transaction } from '../../types';
import { formatAmountString } from '../../utils/currency';
import { Badge } from '../common/Badge';
import { ArrowRight, Eye } from 'lucide-react';
import { Link } from 'react-router-dom';

interface RecentTransactionsTableProps {
  transactions: Transaction[];
  onSelectTransaction?: (tx: Transaction) => void;
}

export const RecentTransactionsTable: React.FC<RecentTransactionsTableProps> = ({
  transactions,
  onSelectTransaction,
}) => {
  return (
    <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
      <div className="flex items-center justify-between mb-4 pb-3 border-b border-fintech-borderSubtle">
        <div>
          <h3 className="font-semibold text-base text-white tracking-tight">Recent Transactions</h3>
          <p className="text-xs text-slate-400 mt-0.5">Latest ledger entries across all entities</p>
        </div>
        <Link
          to="/transactions"
          className="text-xs font-semibold text-emerald-400 hover:text-emerald-300 transition-colors"
        >
          View All &rarr;
        </Link>
      </div>

      {transactions.length === 0 ? (
        <div className="py-8 text-center text-xs text-slate-400">
          No transactions recorded yet
        </div>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-left text-xs">
            <thead>
              <tr className="text-slate-400 border-b border-fintech-borderSubtle">
                <th className="pb-3 font-medium">Tx ID</th>
                <th className="pb-3 font-medium">Participants</th>
                <th className="pb-3 font-medium">Type</th>
                <th className="pb-3 font-medium">Amount</th>
                <th className="pb-3 font-medium">Status</th>
                <th className="pb-3 font-medium">Date</th>
                <th className="pb-3 font-medium text-right">Action</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-fintech-borderSubtle">
              {transactions.slice(0, 8).map((tx) => (
                <tr key={tx.id} className="hover:bg-fintech-surface/40 transition-colors">
                  <td className="py-3 font-mono font-semibold text-white">#{tx.id}</td>
                  <td className="py-3">
                    <div className="flex items-center gap-1.5 text-slate-300">
                      <span className="font-medium">Entity #{tx.sender_entity_id}</span>
                      <ArrowRight className="w-3 h-3 text-slate-600" />
                      <span className="font-medium">Entity #{tx.receiver_entity_id}</span>
                    </div>
                  </td>
                  <td className="py-3">
                    <span className="text-[11px] text-slate-400">{tx.type}</span>
                  </td>
                  <td className="py-3 font-mono font-semibold text-white">
                    {formatAmountString(tx.amount, tx.currency)}
                  </td>
                  <td className="py-3">
                    <Badge variant={tx.status}>{tx.status}</Badge>
                  </td>
                  <td className="py-3 text-slate-400">
                    {new Date(tx.created_at).toLocaleDateString([], {
                      month: 'short',
                      day: 'numeric',
                      hour: '2-digit',
                      minute: '2-digit',
                    })}
                  </td>
                  <td className="py-3 text-right">
                    {onSelectTransaction && (
                      <button
                        onClick={() => onSelectTransaction(tx)}
                        className="p-1 rounded-lg hover:bg-fintech-elevated text-slate-400 hover:text-white transition-colors"
                        title="View details"
                      >
                        <Eye className="w-3.5 h-3.5" />
                      </button>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
};
