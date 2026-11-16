import React, { useState } from 'react';
import { NavLink } from 'react-router-dom';
import { useAuth } from '../../contexts/AuthContext';
import {
  LayoutDashboard,
  ArrowLeftRight,
  Building2,
  Wallet,
  GitFork,
  Network,
  Settings,
  LogOut,
  ChevronLeft,
  ChevronRight,
  Menu,
  X,
  ShieldCheck,
} from 'lucide-react';

export const Sidebar: React.FC = () => {
  const { user, organizationId, role, logout } = useAuth();
  const [collapsed, setCollapsed] = useState(false);
  const [mobileOpen, setMobileOpen] = useState(false);

  const navItems = [
    { label: 'Overview', to: '/', icon: LayoutDashboard },
    { label: 'Transactions', to: '/transactions', icon: ArrowLeftRight },
    { label: 'Entities', to: '/entities', icon: Building2 },
    { label: 'Balances', to: '/balances', icon: Wallet },
    { label: 'Settlement', to: '/settlement', icon: GitFork },
    { label: 'Financial Graph', to: '/graph', icon: Network },
    { label: 'Settings', to: '/settings', icon: Settings },
  ];

  return (
    <>
      {/* Mobile Menu Button */}
      <button
        onClick={() => setMobileOpen(true)}
        className="lg:hidden fixed bottom-4 right-4 z-40 p-3 bg-emerald-600 hover:bg-emerald-500 text-white rounded-full shadow-fintech-glow"
        aria-label="Open menu"
      >
        <Menu className="w-6 h-6" />
      </button>

      {/* Mobile Backdrop */}
      {mobileOpen && (
        <div
          className="lg:hidden fixed inset-0 z-40 bg-black/80 backdrop-blur-sm"
          onClick={() => setMobileOpen(false)}
        />
      )}

      {/* Sidebar Container */}
      <aside
        className={`fixed inset-y-0 left-0 z-40 bg-fintech-navy/95 backdrop-blur-2xl border-r border-fintech-borderSubtle flex flex-col transition-all duration-300 ease-in-out ${
          collapsed ? 'w-20' : 'w-64'
        } ${
          mobileOpen ? 'translate-x-0' : '-translate-x-full lg:translate-x-0'
        }`}
      >
        {/* Logo / Brand Header */}
        <div className="h-16 flex items-center justify-between px-5 border-b border-fintech-borderSubtle">
          <div className="flex items-center gap-3 overflow-hidden">
            <div className="w-9 h-9 rounded-xl bg-gradient-to-tr from-emerald-500 via-teal-500 to-emerald-400 p-0.5 flex-shrink-0 shadow-fintech-glow flex items-center justify-center">
              <div className="w-full h-full bg-fintech-dark rounded-[10px] flex items-center justify-center">
                <Network className="w-5 h-5 text-emerald-400" />
              </div>
            </div>
            {!collapsed && (
              <div className="truncate">
                <div className="font-bold text-base tracking-tight text-white flex items-center gap-1.5">
                  SettleSmart <span className="text-[10px] bg-emerald-950 text-emerald-400 border border-emerald-500/30 px-1.5 py-0.2 rounded-md font-mono">2.0</span>
                </div>
                <div className="text-[10px] text-slate-400 tracking-wider uppercase font-medium">Fintech Settlement</div>
              </div>
            )}
          </div>

          {/* Desktop Collapse Toggle */}
          <button
            onClick={() => setCollapsed(!collapsed)}
            className="hidden lg:flex p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-fintech-surface transition-colors"
            aria-label={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
          >
            {collapsed ? <ChevronRight className="w-4 h-4" /> : <ChevronLeft className="w-4 h-4" />}
          </button>

          {/* Mobile Close Button */}
          <button
            onClick={() => setMobileOpen(false)}
            className="lg:hidden p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-fintech-surface"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Navigation Links */}
        <nav className="flex-1 px-3 py-4 space-y-1.5 overflow-y-auto">
          {navItems.map((item) => {
            const Icon = item.icon;
            return (
              <NavLink
                key={item.to}
                to={item.to}
                end={item.to === '/'}
                onClick={() => setMobileOpen(false)}
                className={({ isActive }) =>
                  `flex items-center gap-3 px-3.5 py-2.5 rounded-xl text-xs font-medium transition-all duration-150 ${
                    isActive
                      ? 'bg-emerald-950/60 text-emerald-300 border border-emerald-500/30 shadow-fintech-glow'
                      : 'text-slate-400 hover:text-slate-200 hover:bg-fintech-surface/70 border border-transparent'
                  }`
                }
                title={collapsed ? item.label : undefined}
              >
                <Icon className={`w-4 h-4 flex-shrink-0 ${collapsed ? 'mx-auto' : ''}`} />
                {!collapsed && <span className="truncate">{item.label}</span>}
              </NavLink>
            );
          })}
        </nav>

        {/* Bottom User Profile Section */}
        <div className="p-3 border-t border-fintech-borderSubtle bg-fintech-surface/40">
          {!collapsed ? (
            <div className="flex items-center justify-between gap-3 p-2 rounded-xl bg-fintech-surface border border-fintech-borderSubtle">
              <div className="overflow-hidden">
                <p className="text-xs font-semibold text-white truncate">{user?.email || 'User'}</p>
                <div className="flex items-center gap-1.5 mt-0.5">
                  <span className="text-[10px] text-emerald-400 font-medium">Org #{organizationId}</span>
                  <span className="text-[10px] text-slate-500">•</span>
                  <span className="text-[10px] text-slate-400 truncate">{role?.replace('_', ' ')}</span>
                </div>
              </div>
              <button
                onClick={logout}
                title="Sign out"
                className="p-1.5 rounded-lg text-slate-400 hover:text-rose-400 hover:bg-red-950/30 transition-colors"
              >
                <LogOut className="w-4 h-4" />
              </button>
            </div>
          ) : (
            <button
              onClick={logout}
              title="Sign out"
              className="w-full flex justify-center p-2 rounded-xl text-slate-400 hover:text-rose-400 hover:bg-red-950/30 transition-colors"
            >
              <LogOut className="w-5 h-5" />
            </button>
          )}
        </div>
      </aside>
    </>
  );
};
