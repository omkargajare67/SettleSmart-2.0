import React from 'react';
import { TransactionStatus, EntityStatus, UserRole } from '../../types';

interface BadgeProps {
  variant?: TransactionStatus | EntityStatus | UserRole | 'Standard' | 'Reversal' | 'Creditor' | 'Debtor' | 'Settled' | string;
  className?: string;
  children?: React.ReactNode;
}

export const Badge: React.FC<BadgeProps> = ({ variant, className = '', children }) => {
  const text = children || variant;

  let style = 'bg-slate-800 text-slate-300 border-slate-700';

  switch (variant) {
    case 'Completed':
    case 'Active':
    case 'Creditor':
      style = 'bg-emerald-950/60 text-emerald-300 border-emerald-500/30';
      break;
    case 'Pending':
      style = 'bg-amber-950/60 text-amber-300 border-amber-500/30';
      break;
    case 'Reversed':
    case 'Inactive':
    case 'Debtor':
      style = 'bg-rose-950/60 text-rose-300 border-rose-500/30';
      break;
    case 'Disputed':
      style = 'bg-red-950/60 text-red-300 border-red-500/30';
      break;
    case 'Admin':
      style = 'bg-purple-950/60 text-purple-300 border-purple-500/30';
      break;
    case 'Finance_Manager':
      style = 'bg-blue-950/60 text-blue-300 border-blue-500/30';
      break;
    case 'Viewer':
      style = 'bg-slate-900 text-slate-400 border-slate-700';
      break;
    case 'Reversal':
      style = 'bg-orange-950/60 text-orange-300 border-orange-500/30';
      break;
    case 'Settled':
      style = 'bg-slate-800 text-slate-300 border-slate-700';
      break;
  }

  return (
    <span
      className={`inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium border ${style} ${className}`}
    >
      {text}
    </span>
  );
};
