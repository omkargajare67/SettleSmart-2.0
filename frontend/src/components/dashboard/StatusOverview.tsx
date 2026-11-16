import React from 'react';
import { ProcessingStatus } from '../../types';

interface StatusOverviewProps {
  status: ProcessingStatus | null;
}

export const StatusOverview: React.FC<StatusOverviewProps> = ({ status }) => {
  const total = status?.total_count || 0;
  const completed = status?.completed_count || 0;
  const pending = status?.pending_count || 0;
  const reversed = status?.reversed_count || 0;
  const disputed = status?.disputed_count || 0;

  const getPercent = (count: number) => {
    if (total === 0) return 0;
    return Math.round((count / total) * 100);
  };

  const items = [
    { label: 'Completed', count: completed, percent: getPercent(completed), color: 'bg-emerald-500', barBg: 'bg-emerald-500/20', text: 'text-emerald-400' },
    { label: 'Pending', count: pending, percent: getPercent(pending), color: 'bg-amber-500', barBg: 'bg-amber-500/20', text: 'text-amber-400' },
    { label: 'Reversed', count: reversed, percent: getPercent(reversed), color: 'bg-rose-500', barBg: 'bg-rose-500/20', text: 'text-rose-400' },
    { label: 'Disputed', count: disputed, percent: getPercent(disputed), color: 'bg-red-500', barBg: 'bg-red-500/20', text: 'text-red-400' },
  ];

  return (
    <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
      <div className="mb-4">
        <h3 className="font-semibold text-base text-white tracking-tight">Status Distribution</h3>
        <p className="text-xs text-slate-400 mt-0.5">Transaction processing lifecycle</p>
      </div>

      {/* Progress segmented bar */}
      <div className="h-3 w-full bg-slate-800 rounded-full overflow-hidden flex mb-6">
        {total > 0 ? (
          <>
            <div style={{ width: `${getPercent(completed)}%` }} className="bg-emerald-500 transition-all duration-500" />
            <div style={{ width: `${getPercent(pending)}%` }} className="bg-amber-500 transition-all duration-500" />
            <div style={{ width: `${getPercent(reversed)}%` }} className="bg-rose-500 transition-all duration-500" />
            <div style={{ width: `${getPercent(disputed)}%` }} className="bg-red-500 transition-all duration-500" />
          </>
        ) : (
          <div className="w-full bg-slate-800" />
        )}
      </div>

      <div className="space-y-3">
        {items.map((item, idx) => (
          <div key={idx} className="flex items-center justify-between text-xs">
            <div className="flex items-center gap-2.5">
              <span className={`w-2 h-2 rounded-full ${item.color}`} />
              <span className="text-slate-300 font-medium">{item.label}</span>
            </div>
            <div className="flex items-center gap-3">
              <span className="font-mono text-slate-400">{item.count.toLocaleString()}</span>
              <span className={`font-mono font-medium ${item.text} w-9 text-right`}>{item.percent}%</span>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};
