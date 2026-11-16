import React, { useState } from 'react';
import { useNavigate, Link } from 'react-router-dom';
import { registerOrganization } from '../api/auth';
import { useToast } from '../contexts/ToastContext';
import { Network, Lock, Mail, Building2, CheckCircle2, ArrowRight } from 'lucide-react';

export const RegisterPage: React.FC = () => {
  const { showToast } = useToast();
  const navigate = useNavigate();

  const [orgName, setOrgName] = useState('');
  const [adminEmail, setAdminEmail] = useState('');
  const [adminPassword, setAdminPassword] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [registeredSuccess, setRegisteredSuccess] = useState<{ orgId: number; userId: number } | null>(null);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);

    if (!orgName.trim()) {
      setError('Please provide an organization name.');
      return;
    }
    if (!adminEmail || !adminEmail.includes('@')) {
      setError('Please provide a valid administrator email.');
      return;
    }
    if (!adminPassword || adminPassword.length < 6) {
      setError('Password must be at least 6 characters.');
      return;
    }

    setLoading(true);
    try {
      const res = await registerOrganization({
        organization_name: orgName.trim(),
        admin_email: adminEmail.trim(),
        admin_password: adminPassword,
      });

      if (res && res.success) {
        setRegisteredSuccess({
          orgId: res.organization_id,
          userId: res.admin_user_id,
        });
        showToast(`Organization registered! Your Organization ID is ${res.organization_id}`, 'success');
      } else {
        throw new Error(res.error || 'Registration failed');
      }
    } catch (err: any) {
      const msg = err.message || 'Registration failed. Please try again.';
      setError(msg);
      showToast(msg, 'error');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen bg-fintech-dark flex items-center justify-center p-4 relative overflow-hidden">
      <div className="absolute top-1/4 -left-32 w-96 h-96 bg-emerald-600/10 rounded-full blur-3xl pointer-events-none" />
      <div className="absolute bottom-1/4 -right-32 w-96 h-96 bg-teal-600/10 rounded-full blur-3xl pointer-events-none" />

      <div className="w-full max-w-md bg-fintech-card/90 backdrop-blur-xl border border-fintech-borderSubtle rounded-3xl p-8 shadow-2xl relative z-10">
        <div className="text-center mb-8">
          <div className="inline-flex w-12 h-12 rounded-2xl bg-gradient-to-tr from-emerald-500 via-teal-500 to-emerald-400 p-0.5 shadow-fintech-glow mb-3">
            <div className="w-full h-full bg-fintech-dark rounded-[14px] flex items-center justify-center">
              <Network className="w-6 h-6 text-emerald-400" />
            </div>
          </div>
          <h2 className="text-2xl font-bold text-white tracking-tight">Register Organization</h2>
          <p className="text-xs text-slate-400 mt-1">Deploy an isolated tenant workspace</p>
        </div>

        {error && (
          <div className="mb-6 p-4 rounded-xl bg-red-950/40 border border-red-500/30 text-rose-300 text-xs leading-relaxed">
            {error}
          </div>
        )}

        {registeredSuccess ? (
          <div className="space-y-6 text-center animate-in zoom-in-95">
            <div className="p-4 rounded-2xl bg-emerald-950/40 border border-emerald-500/30 text-emerald-200">
              <CheckCircle2 className="w-8 h-8 text-emerald-400 mx-auto mb-2" />
              <h3 className="font-bold text-base text-white">Registration Successful!</h3>
              <p className="text-xs text-emerald-300/80 mt-1">
                Your workspace is active and ready.
              </p>
              <div className="mt-4 p-3 bg-fintech-surface rounded-xl border border-fintech-border text-left space-y-1 text-xs">
                <div>
                  <span className="text-slate-400">Organization ID:</span>{' '}
                  <span className="font-mono font-bold text-emerald-400 text-sm">
                    {registeredSuccess.orgId}
                  </span>
                </div>
                <div>
                  <span className="text-slate-400">Admin User ID:</span>{' '}
                  <span className="font-mono text-white">#{registeredSuccess.userId}</span>
                </div>
              </div>
            </div>

            <p className="text-xs text-slate-400">
              Please save your <span className="text-white font-semibold">Organization ID ({registeredSuccess.orgId})</span>. You will need it to sign in.
            </p>

            <button
              onClick={() => navigate('/login')}
              className="w-full py-3 px-4 rounded-xl font-semibold text-xs text-white bg-emerald-600 hover:bg-emerald-500 shadow-fintech-glow flex items-center justify-center gap-2"
            >
              <span>Proceed to Login</span>
              <ArrowRight className="w-4 h-4" />
            </button>
          </div>
        ) : (
          <form onSubmit={handleSubmit} className="space-y-4">
            <div>
              <label className="block text-xs font-semibold text-slate-300 mb-1.5">
                Organization / Company Name
              </label>
              <div className="relative">
                <Building2 className="w-4 h-4 text-slate-500 absolute left-3.5 top-3" />
                <input
                  type="text"
                  placeholder="e.g. Acme Financial Group"
                  value={orgName}
                  onChange={(e) => setOrgName(e.target.value)}
                  required
                  className="w-full pl-10 pr-4 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white text-xs placeholder-slate-500 focus:outline-none focus:border-emerald-500 transition-colors"
                />
              </div>
            </div>

            <div>
              <label className="block text-xs font-semibold text-slate-300 mb-1.5">
                Primary Administrator Email
              </label>
              <div className="relative">
                <Mail className="w-4 h-4 text-slate-500 absolute left-3.5 top-3" />
                <input
                  type="email"
                  placeholder="admin@acme.com"
                  value={adminEmail}
                  onChange={(e) => setAdminEmail(e.target.value)}
                  required
                  className="w-full pl-10 pr-4 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white text-xs placeholder-slate-500 focus:outline-none focus:border-emerald-500 transition-colors"
                />
              </div>
            </div>

            <div>
              <label className="block text-xs font-semibold text-slate-300 mb-1.5">
                Admin Password
              </label>
              <div className="relative">
                <Lock className="w-4 h-4 text-slate-500 absolute left-3.5 top-3" />
                <input
                  type="password"
                  placeholder="••••••••"
                  value={adminPassword}
                  onChange={(e) => setAdminPassword(e.target.value)}
                  required
                  className="w-full pl-10 pr-4 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white text-xs placeholder-slate-500 focus:outline-none focus:border-emerald-500 transition-colors"
                />
              </div>
              <p className="text-[10px] text-slate-500 mt-1">Minimum 6 characters with secure Argon2 hashing</p>
            </div>

            <button
              type="submit"
              disabled={loading}
              className="w-full mt-2 py-3 px-4 rounded-xl font-semibold text-xs text-white bg-gradient-to-r from-emerald-600 to-teal-600 hover:from-emerald-500 hover:to-teal-500 shadow-fintech-glow disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center gap-2 transition-all"
            >
              {loading ? (
                <span>Creating Organization...</span>
              ) : (
                <>
                  <span>Create Organization</span>
                  <ArrowRight className="w-4 h-4" />
                </>
              )}
            </button>
          </form>
        )}

        <div className="mt-6 pt-6 border-t border-fintech-borderSubtle text-center text-xs text-slate-400">
          Already have an organization?{' '}
          <Link to="/login" className="font-semibold text-emerald-400 hover:text-emerald-300">
            Sign In
          </Link>
        </div>
      </div>
    </div>
  );
};
