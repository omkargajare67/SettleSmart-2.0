import React from 'react';
import { Outlet, useLocation } from 'react-router-dom';
import { Sidebar } from '../components/common/Sidebar';
import { Navbar } from '../components/common/Navbar';

export const AppLayout: React.FC = () => {
  const location = useLocation();

  const getPageMeta = (pathname: string) => {
    switch (pathname) {
      case '/':
        return { title: 'Financial Overview', breadcrumbs: [{ label: 'Home' }, { label: 'Overview' }] };
      case '/transactions':
        return { title: 'Transaction Management', breadcrumbs: [{ label: 'Home' }, { label: 'Transactions' }] };
      case '/entities':
        return { title: 'Entity Directory', breadcrumbs: [{ label: 'Home' }, { label: 'Entities' }] };
      case '/balances':
        return { title: 'Entity Balances', breadcrumbs: [{ label: 'Home' }, { label: 'Balances' }] };
      case '/settlement':
        return { title: 'Settlement Recommendations', breadcrumbs: [{ label: 'Home' }, { label: 'Settlement' }] };
      case '/graph':
        return { title: 'Interactive Financial Graph', breadcrumbs: [{ label: 'Home' }, { label: 'Graph' }] };
      case '/settings':
        return { title: 'Settings & Team Management', breadcrumbs: [{ label: 'Home' }, { label: 'Settings' }] };
      default:
        return { title: 'SettleSmart 2.0', breadcrumbs: [{ label: 'Home' }] };
    }
  };

  const { title, breadcrumbs } = getPageMeta(location.pathname);

  return (
    <div className="min-h-screen bg-fintech-dark text-fintech-text flex">
      {/* Sidebar */}
      <Sidebar />

      {/* Main Content Area */}
      <div className="flex-1 lg:pl-64 flex flex-col min-w-0 transition-all duration-300">
        <Navbar pageTitle={title} breadcrumbs={breadcrumbs} />
        <main className="flex-1 p-6 md:p-8 max-w-7xl w-full mx-auto space-y-6">
          <Outlet />
        </main>
      </div>
    </div>
  );
};
