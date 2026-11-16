import React from 'react';
import { Link } from 'react-router-dom';
import { Network, Home } from 'lucide-react';

export const NotFoundPage: React.FC = () => {
  return (
    <div className="min-h-screen bg-fintech-dark flex items-center justify-center p-4 text-center">
      <div className="max-w-md w-full bg-fintech-card/90 border border-fintech-borderSubtle rounded-3xl p-8 shadow-2xl space-y-4">
        <div className="w-12 h-12 rounded-2xl bg-emerald-950/60 border border-emerald-500/30 text-emerald-400 mx-auto flex items-center justify-center">
          <Network className="w-6 h-6" />
        </div>
        <h2 className="text-3xl font-bold font-mono text-white">404</h2>
        <h3 className="text-base font-semibold text-slate-200">Page Not Found</h3>
        <p className="text-xs text-slate-400">
          The requested page route does not exist in the SettleSmart 2.0 system.
        </p>
        <Link
          to="/"
          className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl text-xs font-semibold text-white bg-emerald-600 hover:bg-emerald-500 shadow-fintech-glow transition-all"
        >
          <Home className="w-4 h-4" />
          <span>Return to Dashboard</span>
        </Link>
      </div>
    </div>
  );
};
