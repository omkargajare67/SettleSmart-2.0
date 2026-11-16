import React, { useState, useEffect, useCallback } from 'react';
import { useAuth } from '../contexts/AuthContext';
import { useToast } from '../contexts/ToastContext';
import { changePassword } from '../api/auth';
import { getOrganizationUsers, createOrganizationUser, updateOrganizationUser } from '../api/users';
import { OrganizationUser, UserRole } from '../types';
import { Badge } from '../components/common/Badge';
import { Modal } from '../components/common/Modal';
import { TableSkeleton } from '../components/common/LoadingSkeleton';
import {
  User,
  Shield,
  Key,
  Users,
  Plus,
  Power,
  Lock,
  Mail,
  Building,
  CheckCircle2,
} from 'lucide-react';

export const SettingsPage: React.FC = () => {
  const { user, organizationId, role, canManageUsers, logout } = useAuth();
  const { showToast } = useToast();

  // Change Password State
  const [currentPassword, setCurrentPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [passwordLoading, setPasswordLoading] = useState(false);

  // User Management State (Admin only)
  const [users, setUsers] = useState<OrganizationUser[]>([]);
  const [usersLoading, setUsersLoading] = useState(false);
  const [createUserModalOpen, setCreateUserModalOpen] = useState(false);
  const [newEmail, setNewEmail] = useState('');
  const [newPasswordVal, setNewPasswordVal] = useState('');
  const [newRole, setNewRole] = useState<UserRole>('Viewer');
  const [creatingUser, setCreatingUser] = useState(false);

  const fetchUsers = useCallback(async () => {
    if (!canManageUsers) return;
    setUsersLoading(true);
    try {
      const res = await getOrganizationUsers();
      if (res && res.success) {
        setUsers(res.users);
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to load organization team members', 'error');
    } finally {
      setUsersLoading(false);
    }
  }, [canManageUsers, showToast]);

  useEffect(() => {
    fetchUsers();
  }, [fetchUsers]);

  const handleChangePassword = async (e: React.FormEvent) => {
    e.preventDefault();
    if (newPassword !== confirmPassword) {
      showToast('New passwords do not match', 'error');
      return;
    }
    if (newPassword.length < 6) {
      showToast('New password must be at least 6 characters', 'error');
      return;
    }

    setPasswordLoading(true);
    try {
      const res = await changePassword(currentPassword, newPassword);
      if (res && res.success) {
        showToast('Password updated successfully!', 'success');
        setCurrentPassword('');
        setNewPassword('');
        setConfirmPassword('');
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to update password', 'error');
    } finally {
      setPasswordLoading(false);
    }
  };

  const handleCreateUser = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!newEmail.includes('@') || newPasswordVal.length < 6) {
      showToast('Please enter a valid email and minimum 6-character password', 'error');
      return;
    }

    setCreatingUser(true);
    try {
      const res = await createOrganizationUser(newEmail.trim(), newPasswordVal, newRole);
      if (res && res.success) {
        showToast(`User created with ID #${res.user_id}!`, 'success');
        setCreateUserModalOpen(false);
        setNewEmail('');
        setNewPasswordVal('');
        setNewRole('Viewer');
        fetchUsers();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to create user', 'error');
    } finally {
      setCreatingUser(false);
    }
  };

  const handleToggleUserStatus = async (targetUser: OrganizationUser) => {
    const nextStatus = targetUser.status === 'Active' ? 'Inactive' : 'Active';
    try {
      const res = await updateOrganizationUser(targetUser.id, { status: nextStatus });
      if (res && res.success) {
        showToast(`User status updated to ${nextStatus}`, 'success');
        fetchUsers();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to change user status', 'error');
    }
  };

  return (
    <div className="space-y-8 max-w-5xl">
      {/* Profile & Organization Overview */}
      <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-6 shadow-fintech-card">
        <h3 className="text-base font-semibold text-white tracking-tight mb-4 flex items-center gap-2">
          <User className="w-5 h-5 text-emerald-400" />
          <span>User Profile & Organization Workspace</span>
        </h3>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs">
          <div className="p-4 rounded-xl bg-fintech-surface border border-fintech-borderSubtle space-y-2">
            <span className="text-slate-400 block text-[11px]">Email Address</span>
            <span className="text-white font-medium text-sm">{user?.email}</span>
            <div className="pt-2 border-t border-fintech-borderSubtle flex items-center gap-2">
              <span className="text-slate-400">User ID:</span>
              <span className="text-white font-mono font-bold">#{user?.id}</span>
            </div>
          </div>

          <div className="p-4 rounded-xl bg-fintech-surface border border-fintech-borderSubtle space-y-2">
            <span className="text-slate-400 block text-[11px]">Organization Tenant</span>
            <div className="flex items-center justify-between">
              <span className="text-white font-bold font-mono text-sm">Org #{organizationId}</span>
              <Badge variant={role || 'Viewer'}>{(role || 'Viewer').replace('_', ' ')}</Badge>
            </div>
            <div className="pt-2 border-t border-fintech-borderSubtle text-[11px] text-slate-400">
              Isolated database schema partition with strict multi-tenancy enforcement.
            </div>
          </div>
        </div>
      </div>

      {/* Change Password Form */}
      <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-6 shadow-fintech-card">
        <h3 className="text-base font-semibold text-white tracking-tight mb-4 flex items-center gap-2">
          <Key className="w-5 h-5 text-teal-400" />
          <span>Security & Password Update</span>
        </h3>

        <form onSubmit={handleChangePassword} className="space-y-4 max-w-md text-xs">
          <div>
            <label className="block text-slate-300 font-semibold mb-1.5">Current Password</label>
            <input
              type="password"
              value={currentPassword}
              onChange={(e) => setCurrentPassword(e.target.value)}
              required
              placeholder="••••••••"
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500"
            />
          </div>

          <div>
            <label className="block text-slate-300 font-semibold mb-1.5">New Password</label>
            <input
              type="password"
              value={newPassword}
              onChange={(e) => setNewPassword(e.target.value)}
              required
              placeholder="Minimum 6 characters"
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500"
            />
          </div>

          <div>
            <label className="block text-slate-300 font-semibold mb-1.5">Confirm New Password</label>
            <input
              type="password"
              value={confirmPassword}
              onChange={(e) => setConfirmPassword(e.target.value)}
              required
              placeholder="Re-enter new password"
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500"
            />
          </div>

          <button
            type="submit"
            disabled={passwordLoading}
            className="py-2.5 px-4 rounded-xl font-semibold text-xs text-white bg-emerald-600 hover:bg-emerald-500 shadow-fintech-glow disabled:opacity-50"
          >
            {passwordLoading ? 'Updating Password...' : 'Update Password'}
          </button>
        </form>
      </div>

      {/* Organization Team Management (Admin Only) */}
      {canManageUsers && (
        <div className="bg-fintech-card/90 border border-fintech-borderSubtle rounded-2xl p-6 shadow-fintech-card space-y-4">
          <div className="flex items-center justify-between pb-3 border-b border-fintech-borderSubtle">
            <div>
              <h3 className="text-base font-semibold text-white tracking-tight flex items-center gap-2">
                <Users className="w-5 h-5 text-purple-400" />
                <span>Organization Team Members</span>
              </h3>
              <p className="text-xs text-slate-400 mt-0.5">
                Manage user access, roles, and authorization within Org #{organizationId}
              </p>
            </div>

            <button
              onClick={() => setCreateUserModalOpen(true)}
              className="inline-flex items-center gap-1.5 px-3.5 py-2 rounded-xl text-xs font-semibold text-white bg-purple-600 hover:bg-purple-500 shadow-md transition-all"
            >
              <Plus className="w-4 h-4" />
              <span>Add Member</span>
            </button>
          </div>

          {usersLoading ? (
            <TableSkeleton rows={4} cols={4} />
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-left text-xs">
                <thead>
                  <tr className="text-slate-400 border-b border-fintech-borderSubtle">
                    <th className="py-3 px-4 font-medium">User ID</th>
                    <th className="py-3 px-4 font-medium">Email</th>
                    <th className="py-3 px-4 font-medium">Assigned Role</th>
                    <th className="py-3 px-4 font-medium">Status</th>
                    <th className="py-3 px-4 font-medium text-right">Actions</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-fintech-borderSubtle">
                  {users.map((u) => (
                    <tr key={u.id} className="hover:bg-fintech-surface/40">
                      <td className="py-3 px-4 font-mono font-bold text-white">#{u.id}</td>
                      <td className="py-3 px-4 text-slate-200 font-medium">{u.email}</td>
                      <td className="py-3 px-4">
                        <Badge variant={u.role}>{u.role.replace('_', ' ')}</Badge>
                      </td>
                      <td className="py-3 px-4">
                        <Badge variant={u.status}>{u.status}</Badge>
                      </td>
                      <td className="py-3 px-4 text-right">
                        {u.id !== user?.id && (
                          <button
                            onClick={() => handleToggleUserStatus(u)}
                            className={`p-1.5 rounded-lg hover:bg-fintech-elevated transition-colors ${
                              u.status === 'Active' ? 'text-rose-400 hover:text-rose-300' : 'text-emerald-400 hover:text-emerald-300'
                            }`}
                            title={u.status === 'Active' ? 'Deactivate user' : 'Activate user'}
                          >
                            <Power className="w-4 h-4" />
                          </button>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      )}

      {/* Add Member Modal */}
      <Modal
        isOpen={createUserModalOpen}
        onClose={() => setCreateUserModalOpen(false)}
        title="Add Organization User"
        subtitle={`Creates a new authorized user in Organization #${organizationId}`}
      >
        <form onSubmit={handleCreateUser} className="space-y-4 text-xs">
          <div>
            <label className="block text-slate-300 font-semibold mb-1.5">User Email</label>
            <input
              type="email"
              value={newEmail}
              onChange={(e) => setNewEmail(e.target.value)}
              required
              placeholder="member@example.com"
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500"
            />
          </div>

          <div>
            <label className="block text-slate-300 font-semibold mb-1.5">Temporary Password</label>
            <input
              type="password"
              value={newPasswordVal}
              onChange={(e) => setNewPasswordVal(e.target.value)}
              required
              placeholder="Minimum 6 characters"
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500"
            />
          </div>

          <div>
            <label className="block text-slate-300 font-semibold mb-1.5">Role Permission</label>
            <select
              value={newRole}
              onChange={(e) => setNewRole(e.target.value as UserRole)}
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-surface border border-fintech-border text-white focus:outline-none focus:border-emerald-500"
            >
              <option value="Viewer">Viewer (Read-only access)</option>
              <option value="Finance_Manager">Finance Manager (Entity & Transaction operations)</option>
              <option value="Admin">Administrator (Full tenant control)</option>
            </select>
          </div>

          <div className="flex items-center justify-end gap-3 pt-3">
            <button
              type="button"
              onClick={() => setCreateUserModalOpen(false)}
              className="px-4 py-2 rounded-xl text-slate-300 hover:bg-fintech-elevated"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={creatingUser}
              className="px-4 py-2 rounded-xl font-semibold text-white bg-purple-600 hover:bg-purple-500 shadow-md disabled:opacity-50"
            >
              {creatingUser ? 'Creating Member...' : 'Create Member'}
            </button>
          </div>
        </form>
      </Modal>
    </div>
  );
};
