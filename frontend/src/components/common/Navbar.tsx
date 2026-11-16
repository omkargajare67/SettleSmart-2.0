import React, { useState } from 'react';
import { useAuth } from '../../contexts/AuthContext';
import { useWebSocket } from '../../contexts/WebSocketContext';
import { Badge } from './Badge';
import { Wifi, WifiOff, RefreshCw, User, LogOut, ChevronDown, Building } from 'lucide-react';
import { Link } from 'react-router-dom';

interface NavbarProps {
  pageTitle: string;
  breadcrumbs?: { label: string; href?: string }[];
}

export const Navbar: React.FC<NavbarProps> = ({ pageTitle, breadcrumbs }) => {
  const { user, organizationId, role, logout } = useAuth();
  const { connectionState, reconnect } = useWebSocket();
  const [profileOpen, setProfileOpen] = useState(false);

  return (
    <header className="sticky top-0 z-30 h-16 bg-fintech-dark/80 backdrop-blur-xl border-b border-fintech-borderSubtle px-6 flex items-center justify-between">
      {/* Title & Breadcrumbs */}
      <div>
        {breadcrumbs && breadcrumbs.length > 0 ? (
          <nav className="flex items-center space-x-1.5 text-xs text-slate-400 mb-0.5">
            {breadcrumbs.map((bc, idx) => (
              <React.Fragment key={idx}>
                {idx > 0 && <span className="text-slate-600">/</span>}
                {bc.href ? (
                  <Link to={bc.href} className="hover:text-slate-200 transition-colors">
                    {bc.label}
                  </Link>
                ) : (
                  <span className="text-slate-300 font-medium">{bc.label}</span>
                )}
              </React.Fragment>
            ))}
          </nav>
        ) : null}
        <h1 className="text-lg font-semibold text-white tracking-tight">{pageTitle}</h1>
      </div>

      {/* Right side controls */}
      <div className="flex items-center gap-3">
        {/* WebSocket Connection Pill */}
        <div
          onClick={connectionState !== 'LIVE' ? reconnect : undefined}
          title={connectionState !== 'LIVE' ? 'Click to reconnect' : 'Real-time WebSocket connected'}
          className={`flex items-center gap-2 px-3 py-1.5 rounded-full text-xs font-medium border transition-colors cursor-pointer ${
            connectionState === 'LIVE'
              ? 'bg-emerald-950/40 border-emerald-500/30 text-emerald-400'
              : connectionState === 'RECONNECTING'
              ? 'bg-amber-950/40 border-amber-500/30 text-amber-400 animate-pulse'
              : 'bg-rose-950/40 border-rose-500/30 text-rose-400'
          }`}
        >
          {connectionState === 'LIVE' ? (
            <>
              <span className="relative flex h-2 w-2">
                <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75"></span>
                <span className="relative inline-flex rounded-full h-2 w-2 bg-emerald-500"></span>
              </span>
              <span className="hidden sm:inline">Live Sync</span>
            </>
          ) : connectionState === 'RECONNECTING' ? (
            <>
              <RefreshCw className="w-3 h-3 animate-spin" />
              <span className="hidden sm:inline">Reconnecting</span>
            </>
          ) : (
            <>
              <WifiOff className="w-3 h-3" />
              <span className="hidden sm:inline">Offline</span>
            </>
          )}
        </div>

        {/* Organization Badge */}
        {organizationId && (
          <div className="hidden md:flex items-center gap-1.5 px-3 py-1 rounded-xl bg-fintech-surface border border-fintech-borderSubtle text-xs text-slate-300">
            <Building className="w-3.5 h-3.5 text-slate-400" />
            <span>Org #{organizationId}</span>
          </div>
        )}

        {/* User Role Badge */}
        {role && (
          <div className="hidden sm:block">
            <Badge variant={role}>{role.replace('_', ' ')}</Badge>
          </div>
        )}

        {/* Profile Dropdown */}
        <div className="relative">
          <button
            onClick={() => setProfileOpen(!profileOpen)}
            className="flex items-center gap-2 p-1.5 rounded-xl hover:bg-fintech-surface border border-transparent hover:border-fintech-border transition-colors text-slate-300"
          >
            <div className="w-8 h-8 rounded-lg bg-gradient-to-tr from-emerald-600 to-teal-500 flex items-center justify-center text-white font-semibold text-xs shadow-md">
              {user?.email?.charAt(0).toUpperCase() || 'U'}
            </div>
            <ChevronDown className="w-3.5 h-3.5 text-slate-400" />
          </button>

          {profileOpen && (
            <>
              <div className="fixed inset-0 z-40" onClick={() => setProfileOpen(false)} />
              <div className="absolute right-0 mt-2 w-56 rounded-2xl bg-fintech-surface border border-fintech-border shadow-2xl py-2 z-50 animate-in fade-in-50 zoom-in-95">
                <div className="px-4 py-2 border-b border-fintech-borderSubtle">
                  <p className="text-xs font-semibold text-white truncate">{user?.email}</p>
                  <p className="text-[10px] text-slate-400 mt-0.5">Org #{organizationId} • {role}</p>
                </div>

                <Link
                  to="/settings"
                  onClick={() => setProfileOpen(false)}
                  className="flex items-center gap-2.5 px-4 py-2 text-xs text-slate-300 hover:bg-fintech-elevated hover:text-white transition-colors"
                >
                  <User className="w-3.5 h-3.5 text-slate-400" />
                  Settings & Team
                </Link>

                <button
                  onClick={() => {
                    setProfileOpen(false);
                    logout();
                  }}
                  className="w-full flex items-center gap-2.5 px-4 py-2 text-xs text-rose-400 hover:bg-red-950/30 transition-colors"
                >
                  <LogOut className="w-3.5 h-3.5 text-rose-400" />
                  Sign Out
                </button>
              </div>
            </>
          )}
        </div>
      </div>
    </header>
  );
};
