import React from 'react';

export const CardSkeleton: React.FC<{ count?: number }> = ({ count = 1 }) => {
  return (
    <>
      {Array.from({ length: count }).map((_, i) => (
        <div
          key={i}
          className="bg-fintech-card/50 border border-fintech-borderSubtle rounded-2xl p-5 animate-pulse"
        >
          <div className="flex items-center justify-between mb-4">
            <div className="h-4 bg-slate-800 rounded w-1/3" />
            <div className="h-6 w-6 bg-slate-800 rounded-lg" />
          </div>
          <div className="h-8 bg-slate-800 rounded w-1/2 mb-2" />
          <div className="h-3 bg-slate-800/60 rounded w-2/3" />
        </div>
      ))}
    </>
  );
};

export const TableSkeleton: React.FC<{ rows?: number; cols?: number }> = ({ rows = 5, cols = 5 }) => {
  return (
    <div className="w-full bg-fintech-card/50 border border-fintech-borderSubtle rounded-2xl overflow-hidden animate-pulse">
      <div className="px-6 py-4 bg-fintech-surface/40 border-b border-fintech-borderSubtle flex gap-4">
        {Array.from({ length: cols }).map((_, i) => (
          <div key={i} className="h-4 bg-slate-800 rounded flex-1" />
        ))}
      </div>
      <div className="divide-y divide-fintech-borderSubtle">
        {Array.from({ length: rows }).map((_, r) => (
          <div key={r} className="px-6 py-4 flex gap-4 items-center">
            {Array.from({ length: cols }).map((_, c) => (
              <div key={c} className="h-4 bg-slate-800/60 rounded flex-1" />
            ))}
          </div>
        ))}
      </div>
    </div>
  );
};
