import React from 'react';
import { Building2, ArrowLeftRight, Clock, CheckCircle2, TrendingUp, TrendingDown } from 'lucide-react';
import { formatPaiseToINR } from '../../utils/currency';

interface KPICardsProps {
  totalEntities: number;
  totalTransactions: number;
  pendingTransactions: number;
  completedTransactions: number;
  totalReceivablesPaise: number;
  totalPayablesPaise: number;
  isLoading?: boolean;
}

export const KPICards: React.FC<KPICardsProps> = ({
  totalEntities,
  totalTransactions,
  pendingTransactions,
  completedTransactions,
  totalReceivablesPaise,
  totalPayablesPaise,
  isLoading = false,
}) => {
  const cards = [
    {
      title: 'Total Entities',
      value: totalEntities.toLocaleString(),
      subtitle: 'Registered participants',
      icon: <Building2 className="w-5 h-5 text-emerald-400" />,
      color: 'border-emerald-500/20 text-emerald-400',
    },
    {
      title: 'Total Transactions',
      value: totalTransactions.toLocaleString(),
      subtitle: 'All-time volume',
      icon: <ArrowLeftRight className="w-5 h-5 text-blue-400" />,
      color: 'border-blue-500/20 text-blue-400',
    },
    {
      title: 'Pending Transactions',
      value: pendingTransactions.toLocaleString(),
      subtitle: 'Awaiting heap processing',
      icon: <Clock className="w-5 h-5 text-amber-400" />,
      color: 'border-amber-500/20 text-amber-400',
    },
    {
      title: 'Completed Transactions',
      value: completedTransactions.toLocaleString(),
      subtitle: 'Fully settled in graph',
      icon: <CheckCircle2 className="w-5 h-5 text-teal-400" />,
      color: 'border-teal-500/20 text-teal-400',
    },
    {
      title: 'Outstanding Receivables',
      value: formatPaiseToINR(totalReceivablesPaise),
      subtitle: 'Net positive balances',
      icon: <TrendingUp className="w-5 h-5 text-emerald-400" />,
      color: 'border-emerald-500/20 text-emerald-400',
    },
    {
      title: 'Outstanding Payables',
      value: formatPaiseToINR(totalPayablesPaise),
      subtitle: 'Net negative balances',
      icon: <TrendingDown className="w-5 h-5 text-rose-400" />,
      color: 'border-rose-500/20 text-rose-400',
    },
  ];

  return (
    <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-6 gap-4">
      {cards.map((c, idx) => (
        <div
          key={idx}
          className={`bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl p-4 transition-all duration-200 hover:border-fintech-border shadow-fintech-card ${
            isLoading ? 'animate-pulse' : ''
          }`}
        >
          <div className="flex items-center justify-between mb-3">
            <span className="text-xs font-medium text-slate-400 truncate">{c.title}</span>
            <div className="p-1.5 bg-fintech-surface rounded-xl border border-fintech-borderSubtle">
              {c.icon}
            </div>
          </div>
          <div className="text-xl font-bold text-white tracking-tight truncate font-mono">
            {isLoading ? '...' : c.value}
          </div>
          <div className="text-[11px] text-slate-400 mt-1 truncate">{c.subtitle}</div>
        </div>
      ))}
    </div>
  );
};
