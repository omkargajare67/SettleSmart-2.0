import React, { useState, useEffect, useCallback } from 'react';
import { useAuth } from '../contexts/AuthContext';
import { useToast } from '../contexts/ToastContext';
import {
  getEntities,
  createEntity,
  updateEntity,
  updateEntityStatus,
} from '../api/entities';
import { getEntityBalances } from '../api/processing';
import { Entity, EntityType, EntityStatus, EntityFilter } from '../types';
import { Badge } from '../components/common/Badge';
import { Modal } from '../components/common/Modal';
import { Drawer } from '../components/common/Drawer';
import { TableSkeleton } from '../components/common/LoadingSkeleton';
import { EmptyState } from '../components/common/EmptyState';
import { ErrorState } from '../components/common/ErrorState';
import { formatPaiseToINR } from '../utils/currency';
import {
  Building2,
  Plus,
  Search,
  Filter,
  Eye,
  Edit2,
  Power,
  ChevronLeft,
  ChevronRight,
  TrendingUp,
  TrendingDown,
} from 'lucide-react';

export const EntitiesPage: React.FC = () => {
  const { canManageEntities } = useAuth();
  const { showToast } = useToast();

  const [entities, setEntities] = useState<Entity[]>([]);
  const [balances, setBalances] = useState<{ [id: number]: number }>({});
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Filters & Pagination
  const [search, setSearch] = useState('');
  const [typeFilter, setTypeFilter] = useState<string>('');
  const [statusFilter, setStatusFilter] = useState<string>('');
  const [page, setPage] = useState(1);
  const [totalPages, setTotalPages] = useState(1);
  const [totalRecords, setTotalRecords] = useState(0);

  // Modals & Drawers
  const [createModalOpen, setCreateModalOpen] = useState(false);
  const [editModalEntity, setEditModalEntity] = useState<Entity | null>(null);
  const [detailDrawerEntity, setDetailDrawerEntity] = useState<Entity | null>(null);

  // Form states
  const [formName, setFormName] = useState('');
  const [formType, setFormType] = useState<EntityType>('Vendor');
  const [submitting, setSubmitting] = useState(false);

  const fetchEntities = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const filter: EntityFilter = {
        page,
        limit: 15,
        search: search.trim() || undefined,
        type: (typeFilter as EntityType) || undefined,
        status: (statusFilter as EntityStatus) || undefined,
      };

      const [res, balRes] = await Promise.all([
        getEntities(filter),
        getEntityBalances().catch(() => ({ balances: [] })),
      ]);

      if (res && res.success) {
        setEntities(res.data);
        setTotalPages(res.pagination.total_pages || 1);
        setTotalRecords(res.pagination.total_records || 0);
      }

      if (balRes && balRes.balances) {
        const map: { [id: number]: number } = {};
        balRes.balances.forEach((b: any) => {
          map[b.entity_id] = b.balance_paise;
        });
        setBalances(map);
      }
    } catch (err: any) {
      setError(err.message || 'Failed to load entities');
    } finally {
      setLoading(false);
    }
  }, [page, search, typeFilter, statusFilter]);

  useEffect(() => {
    fetchEntities();
  }, [fetchEntities]);

  // Create Entity
  const handleCreateSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!formName.trim()) {
      showToast('Entity name is required', 'error');
      return;
    }

    setSubmitting(true);
    try {
      const res = await createEntity(formName.trim(), formType);
      if (res && res.success) {
        showToast(`Entity "${res.entity.name}" created with ID #${res.entity.id}`, 'success');
        setCreateModalOpen(false);
        setFormName('');
        setFormType('Vendor');
        fetchEntities();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to create entity', 'error');
    } finally {
      setSubmitting(false);
    }
  };

  // Update Entity
  const handleEditSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!editModalEntity) return;

    setSubmitting(true);
    try {
      const res = await updateEntity(editModalEntity.id, formName.trim(), formType);
      if (res && res.success) {
        showToast(`Entity #${res.entity.id} updated successfully`, 'success');
        setEditModalEntity(null);
        fetchEntities();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to update entity', 'error');
    } finally {
      setSubmitting(false);
    }
  };

  // Toggle Status
  const handleToggleStatus = async (entity: Entity) => {
    const nextStatus: EntityStatus = entity.status === 'Active' ? 'Inactive' : 'Active';
    try {
      const res = await updateEntityStatus(entity.id, nextStatus);
      if (res && res.success) {
        showToast(`Entity #${entity.id} is now ${nextStatus}`, 'success');
        fetchEntities();
      }
    } catch (err: any) {
      showToast(err.message || 'Failed to change entity status', 'error');
    }
  };

  const openEditModal = (entity: Entity) => {
    setEditModalEntity(entity);
    setFormName(entity.name);
    setFormType(entity.entity_type);
  };

  return (
    <div className="space-y-6">
      {/* Header with Title and Create Action */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-bold text-white tracking-tight">Entity Directory</h2>
          <p className="text-xs text-slate-400 mt-0.5">
            Manage companies, vendors, departments, and financial participants ({totalRecords} total)
          </p>
        </div>

        {canManageEntities && (
          <button
            onClick={() => {
              setFormName('');
              setFormType('Vendor');
              setCreateModalOpen(true);
            }}
            className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl font-semibold text-xs text-white bg-gradient-to-r from-emerald-600 to-teal-600 hover:from-emerald-500 hover:to-teal-500 shadow-fintech-glow transition-all"
          >
            <Plus className="w-4 h-4" />
            <span>Create Entity</span>
          </button>
        )}
      </div>

      {/* Filter and Search Bar */}
      <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle p-4 rounded-2xl flex flex-col md:flex-row items-center justify-between gap-4">
        <div className="relative w-full md:w-72">
          <Search className="w-4 h-4 text-slate-500 absolute left-3.5 top-3" />
          <input
            type="text"
            placeholder="Search by name or entity ID..."
            value={search}
            onChange={(e) => {
              setSearch(e.target.value);
              setPage(1);
            }}
            className="w-full pl-10 pr-4 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500 transition-colors"
          />
        </div>

        <div className="flex items-center gap-3 w-full md:w-auto">
          {/* Type Filter */}
          <select
            value={typeFilter}
            onChange={(e) => {
              setTypeFilter(e.target.value);
              setPage(1);
            }}
            className="px-3 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-slate-300 focus:outline-none focus:border-emerald-500"
          >
            <option value="">All Types</option>
            <option value="Department">Department</option>
            <option value="Vendor">Vendor</option>
            <option value="Subsidiary">Subsidiary</option>
            <option value="Client">Client</option>
            <option value="Partner">Partner</option>
          </select>

          {/* Status Filter */}
          <select
            value={statusFilter}
            onChange={(e) => {
              setStatusFilter(e.target.value);
              setPage(1);
            }}
            className="px-3 py-2 rounded-xl text-xs bg-fintech-surface border border-fintech-border text-slate-300 focus:outline-none focus:border-emerald-500"
          >
            <option value="">All Statuses</option>
            <option value="Active">Active</option>
            <option value="Inactive">Inactive</option>
          </select>
        </div>
      </div>

      {/* Entity Table or States */}
      {error ? (
        <ErrorState message={error} onRetry={fetchEntities} />
      ) : loading ? (
        <TableSkeleton rows={8} cols={6} />
      ) : entities.length === 0 ? (
        <EmptyState
          title="No Entities Found"
          description="No financial entities match your search criteria. Register a new entity to get started."
          action={
            canManageEntities ? (
              <button
                onClick={() => setCreateModalOpen(true)}
                className="px-4 py-2 rounded-xl text-xs font-semibold bg-emerald-600 text-white shadow-fintech-glow"
              >
                Create First Entity
              </button>
            ) : undefined
          }
        />
      ) : (
        <div className="bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl overflow-hidden shadow-fintech-card">
          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs">
              <thead>
                <tr className="text-slate-400 border-b border-fintech-borderSubtle bg-fintech-surface/40">
                  <th className="py-3.5 px-6 font-medium">ID</th>
                  <th className="py-3.5 px-6 font-medium">Entity Name</th>
                  <th className="py-3.5 px-6 font-medium">Type</th>
                  <th className="py-3.5 px-6 font-medium">Net Balance</th>
                  <th className="py-3.5 px-6 font-medium">Status</th>
                  <th className="py-3.5 px-6 font-medium">Registered Date</th>
                  <th className="py-3.5 px-6 font-medium text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-fintech-borderSubtle">
                {entities.map((entity) => {
                  const balPaise = balances[entity.id] || 0;
                  const isCreditor = balPaise > 0;
                  const isDebtor = balPaise < 0;

                  return (
                    <tr key={entity.id} className="hover:bg-fintech-surface/40 transition-colors">
                      <td className="py-3.5 px-6 font-mono font-bold text-white">#{entity.id}</td>
                      <td className="py-3.5 px-6">
                        <span className="font-semibold text-white">{entity.name}</span>
                      </td>
                      <td className="py-3.5 px-6">
                        <span className="px-2.5 py-1 rounded-lg bg-fintech-surface text-slate-300 border border-fintech-border font-medium">
                          {entity.entity_type}
                        </span>
                      </td>
                      <td className="py-3.5 px-6 font-mono font-bold">
                        <span className={isCreditor ? 'text-emerald-400' : isDebtor ? 'text-rose-400' : 'text-slate-400'}>
                          {formatPaiseToINR(balPaise)}
                        </span>
                      </td>
                      <td className="py-3.5 px-6">
                        <Badge variant={entity.status}>{entity.status}</Badge>
                      </td>
                      <td className="py-3.5 px-6 text-slate-400">
                        {new Date(entity.created_at).toLocaleDateString([], {
                          year: 'numeric',
                          month: 'short',
                          day: 'numeric',
                        })}
                      </td>
                      <td className="py-3.5 px-6 text-right">
                        <div className="flex items-center justify-end gap-1.5">
                          <button
                            onClick={() => setDetailDrawerEntity(entity)}
                            className="p-1.5 rounded-lg hover:bg-fintech-elevated text-slate-400 hover:text-white transition-colors"
                            title="View details"
                          >
                            <Eye className="w-4 h-4" />
                          </button>

                          {canManageEntities && (
                            <>
                              <button
                                onClick={() => openEditModal(entity)}
                                className="p-1.5 rounded-lg hover:bg-fintech-elevated text-slate-400 hover:text-white transition-colors"
                                title="Edit entity"
                              >
                                <Edit2 className="w-4 h-4" />
                              </button>
                              <button
                                onClick={() => handleToggleStatus(entity)}
                                className={`p-1.5 rounded-lg hover:bg-fintech-elevated transition-colors ${
                                  entity.status === 'Active' ? 'text-rose-400 hover:text-rose-300' : 'text-emerald-400 hover:text-emerald-300'
                                }`}
                                title={entity.status === 'Active' ? 'Deactivate' : 'Activate'}
                              >
                                <Power className="w-4 h-4" />
                              </button>
                            </>
                          )}
                        </div>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>

          {/* Pagination Controls */}
          {totalPages > 1 && (
            <div className="px-6 py-3.5 border-t border-fintech-borderSubtle bg-fintech-surface/20 flex items-center justify-between text-xs text-slate-400">
              <div>
                Showing page <span className="text-white font-medium">{page}</span> of{' '}
                <span className="text-white font-medium">{totalPages}</span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  disabled={page <= 1}
                  onClick={() => setPage(page - 1)}
                  className="p-1.5 rounded-lg bg-fintech-surface hover:bg-fintech-elevated disabled:opacity-30 disabled:cursor-not-allowed border border-fintech-borderSubtle text-slate-300"
                >
                  <ChevronLeft className="w-4 h-4" />
                </button>
                <button
                  disabled={page >= totalPages}
                  onClick={() => setPage(page + 1)}
                  className="p-1.5 rounded-lg bg-fintech-surface hover:bg-fintech-elevated disabled:opacity-30 disabled:cursor-not-allowed border border-fintech-borderSubtle text-slate-300"
                >
                  <ChevronRight className="w-4 h-4" />
                </button>
              </div>
            </div>
          )}
        </div>
      )}

      {/* Create Entity Modal */}
      <Modal
        isOpen={createModalOpen}
        onClose={() => setCreateModalOpen(false)}
        title="Create Financial Entity"
        subtitle="Registers a new participant. ID will be generated by MySQL automatically."
      >
        <form onSubmit={handleCreateSubmit} className="space-y-4">
          <div>
            <label className="block text-xs font-semibold text-slate-300 mb-1.5">
              Entity Name
            </label>
            <input
              type="text"
              placeholder="e.g. Acme Logistics, Treasury Dept, etc."
              value={formName}
              onChange={(e) => setFormName(e.target.value)}
              required
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-card border border-fintech-border text-white text-xs placeholder-slate-500 focus:outline-none focus:border-emerald-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-300 mb-1.5">
              Entity Classification
            </label>
            <select
              value={formType}
              onChange={(e) => setFormType(e.target.value as EntityType)}
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-card border border-fintech-border text-white text-xs focus:outline-none focus:border-emerald-500"
            >
              <option value="Department">Department</option>
              <option value="Vendor">Vendor</option>
              <option value="Subsidiary">Subsidiary</option>
              <option value="Client">Client</option>
              <option value="Partner">Partner</option>
            </select>
          </div>

          <div className="p-3 rounded-xl bg-fintech-dark/60 border border-fintech-borderSubtle text-[11px] text-slate-400">
            Note: SettleSmart will automatically assign an authoritative integer ID and set status to <span className="text-emerald-400 font-semibold">Active</span>.
          </div>

          <div className="flex items-center justify-end gap-3 pt-3">
            <button
              type="button"
              onClick={() => setCreateModalOpen(false)}
              className="px-4 py-2 rounded-xl text-xs font-medium text-slate-300 hover:bg-fintech-elevated"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={submitting}
              className="px-4 py-2 rounded-xl text-xs font-semibold text-white bg-emerald-600 hover:bg-emerald-500 shadow-fintech-glow disabled:opacity-50"
            >
              {submitting ? 'Creating...' : 'Create Entity'}
            </button>
          </div>
        </form>
      </Modal>

      {/* Edit Entity Modal */}
      <Modal
        isOpen={!!editModalEntity}
        onClose={() => setEditModalEntity(null)}
        title={`Edit Entity #${editModalEntity?.id}`}
        subtitle="Update participant name or classification"
      >
        <form onSubmit={handleEditSubmit} className="space-y-4">
          <div>
            <label className="block text-xs font-semibold text-slate-300 mb-1.5">
              Entity Name
            </label>
            <input
              type="text"
              value={formName}
              onChange={(e) => setFormName(e.target.value)}
              required
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-card border border-fintech-border text-white text-xs focus:outline-none focus:border-emerald-500"
            />
          </div>

          <div>
            <label className="block text-xs font-semibold text-slate-300 mb-1.5">
              Entity Classification
            </label>
            <select
              value={formType}
              onChange={(e) => setFormType(e.target.value as EntityType)}
              className="w-full px-3.5 py-2.5 rounded-xl bg-fintech-card border border-fintech-border text-white text-xs focus:outline-none focus:border-emerald-500"
            >
              <option value="Department">Department</option>
              <option value="Vendor">Vendor</option>
              <option value="Subsidiary">Subsidiary</option>
              <option value="Client">Client</option>
              <option value="Partner">Partner</option>
            </select>
          </div>

          <div className="flex items-center justify-end gap-3 pt-3">
            <button
              type="button"
              onClick={() => setEditModalEntity(null)}
              className="px-4 py-2 rounded-xl text-xs font-medium text-slate-300 hover:bg-fintech-elevated"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={submitting}
              className="px-4 py-2 rounded-xl text-xs font-semibold text-white bg-emerald-600 hover:bg-emerald-500 shadow-fintech-glow disabled:opacity-50"
            >
              {submitting ? 'Saving...' : 'Save Changes'}
            </button>
          </div>
        </form>
      </Modal>

      {/* Entity Detail Drawer */}
      <Drawer
        isOpen={!!detailDrawerEntity}
        onClose={() => setDetailDrawerEntity(null)}
        title={detailDrawerEntity?.name || 'Entity Details'}
        subtitle={`Entity #${detailDrawerEntity?.id}`}
      >
        {detailDrawerEntity && (
          <div className="space-y-5 text-xs">
            <div className="flex items-center justify-between p-3 rounded-xl bg-fintech-card border border-fintech-border">
              <span className="text-slate-400">Status:</span>
              <Badge variant={detailDrawerEntity.status}>{detailDrawerEntity.status}</Badge>
            </div>

            <div className="p-4 rounded-xl bg-fintech-card border border-fintech-border text-center">
              <span className="text-slate-400 text-[11px] block mb-1">Current Net Balance</span>
              <span className="text-2xl font-bold font-mono text-white">
                {formatPaiseToINR(balances[detailDrawerEntity.id] || 0)}
              </span>
              <div className="mt-1">
                {(balances[detailDrawerEntity.id] || 0) > 0 ? (
                  <span className="text-emerald-400 font-semibold flex items-center justify-center gap-1">
                    <TrendingUp className="w-3.5 h-3.5" /> Net Creditor (Owed Money)
                  </span>
                ) : (balances[detailDrawerEntity.id] || 0) < 0 ? (
                  <span className="text-rose-400 font-semibold flex items-center justify-center gap-1">
                    <TrendingDown className="w-3.5 h-3.5" /> Net Debtor (Owes Money)
                  </span>
                ) : (
                  <span className="text-slate-400">Settled (Zero Balance)</span>
                )}
              </div>
            </div>

            <div className="space-y-3">
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Classification:</span>
                <span className="text-white font-medium">{detailDrawerEntity.entity_type}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Organization ID:</span>
                <span className="text-white font-mono">Org #{detailDrawerEntity.organization_id}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Created At:</span>
                <span className="text-slate-300 font-mono">{new Date(detailDrawerEntity.created_at).toLocaleString()}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Updated At:</span>
                <span className="text-slate-300 font-mono">{new Date(detailDrawerEntity.updated_at).toLocaleString()}</span>
              </div>
            </div>
          </div>
        )}
      </Drawer>
    </div>
  );
};
