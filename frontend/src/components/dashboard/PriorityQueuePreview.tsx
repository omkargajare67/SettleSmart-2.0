import React from 'react';
import { Transaction } from '../../types';
import { formatAmountString } from '../../utils/currency';
import { Badge } from '../common/Badge';
import { Flame, Play, FastForward, Clock, ArrowRight } from 'lucide-react';
import { useAuth } from '../../contexts/AuthContext';
import { Link } from 'react-router-dom';

interface PriorityQueuePreviewProps {
  pendingTransactions: Transaction[];
  onProcessNext?: () => void;
  onProcessBatch?: () => void;
  isProcessing?: boolean;
}

export const PriorityQueuePreview: React.FC<PriorityQueuePreviewProps> = ({
  pendingTransactions,
  onProcessNext,
  onProcessBatch,
  isProcessing = false,
}) => {
  const { canProcessTransactions } = useAuth();

  return (
    <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 mb-4 pb-3 border-b border-fintech-borderSubtle">
        <div className="flex items-center gap-2.5">
          <div className="p-1.5 rounded-lg bg-amber-950/60 border border-amber-500/30 text-amber-400">
            <Flame className="w-4 h-4" />
          </div>
          <div>
            <h3 className="font-semibold text-base text-white tracking-tight">Priority Queue (Max-Heap)</h3>
            <p className="text-xs text-slate-400 mt-0.5">Ranked by Amount (Desc), Timestamp (Asc), ID (Asc)</p>
          </div>
        </div>

        {canProcessTransactions && (
          <div className="flex items-center gap-2">
            <button
              onClick={onProcessNext}
              disabled={isProcessing || pendingTransactions.length === 0}
              className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-xs font-semibold bg-emerald-600 hover:bg-emerald-500 disabled:opacity-50 disabled:cursor-not-allowed text-white shadow-fintech-glow transition-all"
            >
              <Play className="w-3.5 h-3.5 fill-current" />
              Process Next
            </button>
            <button
              onClick={onProcessBatch}
              disabled={isProcessing || pendingTransactions.length === 0}
              className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-xs font-medium bg-fintech-surface hover:bg-fintech-elevated disabled:opacity-50 disabled:cursor-not-allowed text-slate-300 border border-fintech-border transition-all"
            >
              <FastForward className="w-3.5 h-3.5" />
              Process Batch (10)
            </button>
          </div>
        )}
      </div>

      {pendingTransactions.length === 0 ? (
        <div className="p-8 text-center text-xs text-slate-400">
          <Clock className="w-6 h-6 mx-auto mb-2 text-slate-500" />
          No pending transactions in queue. All transactions processed.
        </div>
      ) : (
        <div className="divide-y divide-fintech-borderSubtle">
          {pendingTransactions.slice(0, 5).map((tx, idx) => (
            <div key={tx.id} className="py-3 flex items-center justify-between gap-4">
              <div className="flex items-center gap-3">
                <span className="w-5 text-center text-xs font-mono font-bold text-amber-400/80">
                  #{idx + 1}
                </span>
                <div>
                  <div className="flex items-center gap-2">
                    <span className="text-xs font-semibold text-white">Tx #{tx.id}</span>
                    <Badge variant={tx.status}>{tx.status}</Badge>
                  </div>
                  <div className="text-[11px] text-slate-400 mt-0.5 flex items-center gap-1.5">
                    <span>Entity #{tx.sender_entity_id}</span>
                    <ArrowRight className="w-3 h-3 text-slate-600" />
                    <span>Entity #{tx.receiver_entity_id}</span>
                  </div>
                </div>
              </div>

              <div className="text-right">
                <div className="text-sm font-bold text-white font-mono">
                  {formatAmountString(tx.amount, tx.currency)}
                </div>
                <div className="text-[10px] text-slate-400">
                  {new Date(tx.created_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                </div>
              </div>
            </div>
          ))}
        </div>
      )}

      {pendingTransactions.length > 5 && (
        <div className="mt-4 pt-3 border-t border-fintech-borderSubtle text-center">
          <Link to="/transactions?status=Pending" className="text-xs font-medium text-emerald-400 hover:text-emerald-300">
            View all {pendingTransactions.length} pending transactions &rarr;
          </Link>
        </div>
      )}
    </div>
  );
};
