import React, { useState, useMemo } from 'react';
import {
  AreaChart,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from 'recharts';
import { Transaction } from '../../types';
import { parseINRStringToPaise, formatPaiseToINR } from '../../utils/currency';

interface ActivityChartProps {
  transactions: Transaction[];
}

export const ActivityChart: React.FC<ActivityChartProps> = ({ transactions }) => {
  const [timeframe, setTimeframe] = useState<'daily' | 'weekly' | 'monthly'>('daily');

  const chartData = useMemo(() => {
    if (!transactions || transactions.length === 0) {
      return [];
    }

    const groups: { [key: string]: { volumePaise: number; count: number } } = {};

    transactions.forEach((tx) => {
      const date = new Date(tx.created_at);
      let key = date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' });

      if (timeframe === 'weekly') {
        const weekNum = Math.ceil(date.getDate() / 7);
        key = `${date.toLocaleDateString('en-US', { month: 'short' })} W${weekNum}`;
      } else if (timeframe === 'monthly') {
        key = date.toLocaleDateString('en-US', { month: 'short', year: '2-digit' });
      }

      const paise = parseINRStringToPaise(tx.amount);
      if (!groups[key]) {
        groups[key] = { volumePaise: 0, count: 0 };
      }
      groups[key].volumePaise += paise;
      groups[key].count += 1;
    });

    return Object.keys(groups).map((k) => ({
      name: k,
      volume: groups[k].volumePaise / 100,
      volumeFormatted: formatPaiseToINR(groups[k].volumePaise),
      transactions: groups[k].count,
    }));
  }, [transactions, timeframe]);

  return (
    <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card">
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 mb-6">
        <div>
          <h3 className="font-semibold text-base text-white tracking-tight">Transaction Volume</h3>
          <p className="text-xs text-slate-400 mt-0.5">Financial throughput over time</p>
        </div>

        {/* Timeframe switch */}
        <div className="flex items-center gap-1 bg-fintech-surface p-1 rounded-xl border border-fintech-borderSubtle">
          {(['daily', 'weekly', 'monthly'] as const).map((tf) => (
            <button
              key={tf}
              onClick={() => setTimeframe(tf)}
              className={`px-3 py-1 rounded-lg text-xs font-medium capitalize transition-all ${
                timeframe === tf
                  ? 'bg-fintech-elevated text-emerald-400 shadow-sm'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              {tf}
            </button>
          ))}
        </div>
      </div>

      <div className="h-64 w-full">
        {chartData.length === 0 ? (
          <div className="h-full flex items-center justify-center text-xs text-slate-400">
            No transaction records available for this period
          </div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={chartData} margin={{ top: 10, right: 10, left: 0, bottom: 0 }}>
              <defs>
                <linearGradient id="emeraldGradient" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#10B981" stopOpacity={0.4} />
                  <stop offset="95%" stopColor="#10B981" stopOpacity={0.0} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" stroke="#1E293B" vertical={false} />
              <XAxis dataKey="name" stroke="#64748B" fontSize={11} tickLine={false} axisLine={false} />
              <YAxis
                stroke="#64748B"
                fontSize={11}
                tickLine={false}
                axisLine={false}
                tickFormatter={(val) => `₹${val.toLocaleString('en-IN')}`}
              />
              <Tooltip
                content={({ active, payload }) => {
                  if (active && payload && payload.length) {
                    const data = payload[0].payload;
                    return (
                      <div className="bg-fintech-surface border border-fintech-border p-3 rounded-xl shadow-2xl text-xs space-y-1">
                        <p className="font-semibold text-white">{data.name}</p>
                        <p className="text-emerald-400 font-mono">Volume: {data.volumeFormatted}</p>
                        <p className="text-slate-400">Count: {data.transactions} txns</p>
                      </div>
                    );
                  }
                  return null;
                }}
              />
              <Area
                type="monotone"
                dataKey="volume"
                stroke="#10B981"
                strokeWidth={2}
                fillOpacity={1}
                fill="url(#emeraldGradient)"
              />
            </AreaChart>
          </ResponsiveContainer>
        )}
      </div>
    </div>
  );
};
