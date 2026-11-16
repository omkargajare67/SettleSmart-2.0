import React, { useState, useEffect, useCallback } from 'react';
import { useWebSocket } from '../contexts/WebSocketContext';
import { useToast } from '../contexts/ToastContext';
import { getGraphSnapshot } from '../api/graph';
import { getEntityBalances } from '../api/processing';
import { GraphNode, GraphEdge } from '../types';
import { FinancialGraphCanvas } from '../components/graph/FinancialGraphCanvas';
import { Drawer } from '../components/common/Drawer';
import { Badge } from '../components/common/Badge';
import { ErrorState } from '../components/common/ErrorState';
import { formatAmountString, formatPaiseToINR } from '../utils/currency';
import { Network, RefreshCw, ArrowRight, TrendingUp, TrendingDown, Clock, ShieldCheck } from 'lucide-react';

export const GraphPage: React.FC = () => {
  const { setWatermark, subscribeToEvents, connectionState, reconnect } = useWebSocket();
  const { showToast } = useToast();

  const [nodes, setNodes] = useState<GraphNode[]>([]);
  const [edges, setEdges] = useState<GraphEdge[]>([]);
  const [watermark, setLocalWatermark] = useState<number>(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const [selectedNode, setSelectedNode] = useState<GraphNode | null>(null);
  const [selectedEdge, setSelectedEdge] = useState<GraphEdge | null>(null);
  const [highlightTxId, setHighlightTxId] = useState<number | null>(null);

  const fetchGraph = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const [res, balancesRes] = await Promise.all([
        getGraphSnapshot({ limit: 200 }),
        getEntityBalances().catch(() => null),
      ]);

      if (res && (res.success || Array.isArray(res.nodes))) {
        const balanceMap = new Map<number, number>();
        if (balancesRes && balancesRes.balances && Array.isArray(balancesRes.balances)) {
          balancesRes.balances.forEach((b: any) => {
            balanceMap.set(b.entity_id, b.balance_paise);
          });
        }

        const rawNodes = res.nodes || [];
        const rawEdges = res.edges || [];

        const enrichedNodes: GraphNode[] = rawNodes.map((node: any) => {
          let bal = balanceMap.get(node.id);
          if (bal === undefined) {
            let net = 0;
            rawEdges.forEach((e: any) => {
              const fromId = e.from_id ?? e.sender_id;
              const toId = e.to_id ?? e.receiver_id;
              const amt = Number(e.amount_paise ?? 0);
              if (e.status === 'Completed') {
                if (toId === node.id) net += amt;
                if (fromId === node.id) net -= amt;
              }
            });
            bal = net;
          }
          return {
            ...node,
            balance_paise: bal,
          };
        });

        setNodes(enrichedNodes);
        setEdges(rawEdges);
        setLocalWatermark(res.event_sequence_watermark || 0);
        setWatermark(res.event_sequence_watermark || 0);
      }
    } catch (err: any) {
      setError(err.message || 'Failed to retrieve graph snapshot');
    } finally {
      setLoading(false);
    }
  }, [setWatermark]);

  useEffect(() => {
    fetchGraph();
  }, [fetchGraph]);

  // Real-time graph listener
  useEffect(() => {
    const unsubscribe = subscribeToEvents((ev) => {
      // Trigger subtle pulse on incoming transaction
      const txId = ev.payload?.transaction_id || ev.payload?.id;
      if (txId) {
        setHighlightTxId(txId);
        setTimeout(() => setHighlightTxId(null), 3000);
      }
      fetchGraph();
    });
    return unsubscribe;
  }, [subscribeToEvents, fetchGraph]);

  // Compute incoming and outgoing transactions for selected node
  const incomingEdges = selectedNode ? edges.filter((e) => (e.to_id ?? e.receiver_id) === selectedNode.id) : [];
  const outgoingEdges = selectedNode ? edges.filter((e) => (e.from_id ?? e.sender_id) === selectedNode.id) : [];

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-bold text-white tracking-tight">Interactive Financial Graph</h2>
          <p className="text-xs text-slate-400 mt-0.5">
            Directed weighted graph with parallel transfer curves & watermark alignment (Watermark: #{watermark})
          </p>
        </div>

        <div className="flex items-center gap-2.5">
          <button
            onClick={fetchGraph}
            disabled={loading}
            className="inline-flex items-center gap-2 px-3.5 py-2 rounded-xl text-xs font-semibold bg-fintech-card border border-fintech-border text-slate-300 hover:text-white hover:bg-fintech-elevated transition-colors"
          >
            <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} />
            <span>Sync Snapshot</span>
          </button>
        </div>
      </div>

      {error ? (
        <ErrorState message={error} onRetry={fetchGraph} />
      ) : (
        <div className="space-y-4">
          {/* Main Visual Canvas */}
          <FinancialGraphCanvas
            nodes={nodes}
            edges={edges}
            selectedNodeId={selectedNode?.id}
            selectedEdgeId={selectedEdge?.tx_id ?? selectedEdge?.id}
            onSelectNode={(node) => {
              setSelectedNode(node);
              setSelectedEdge(null);
            }}
            onSelectEdge={(edge) => {
              setSelectedEdge(edge);
              setSelectedNode(null);
            }}
            highlightTxId={highlightTxId}
          />

          {/* Quick Legend / Guide */}
          <div className="flex flex-wrap items-center justify-between gap-4 p-4 rounded-2xl bg-fintech-card/60 border border-fintech-borderSubtle text-xs text-slate-400">
            <div className="flex items-center gap-6">
              <div className="flex items-center gap-2">
                <span className="w-2.5 h-2.5 rounded-full bg-emerald-500" />
                <span>Completed Edge</span>
              </div>
              <div className="flex items-center gap-2">
                <span className="w-2.5 h-2.5 rounded-full bg-amber-500" />
                <span>Pending Edge</span>
              </div>
              <div className="flex items-center gap-2">
                <span className="w-2.5 h-2.5 rounded-full bg-rose-500" />
                <span>Reversed Edge</span>
              </div>
            </div>

            <div className="text-[11px] text-slate-400">
              Drag nodes to reposition • Scroll/Buttons to zoom • Click node or edge to inspect
            </div>
          </div>
        </div>
      )}

      {/* Node Detail Drawer */}
      <Drawer
        isOpen={!!selectedNode}
        onClose={() => setSelectedNode(null)}
        title={selectedNode?.name || 'Entity Node'}
        subtitle={`Entity #${selectedNode?.id} (${selectedNode?.type})`}
      >
        {selectedNode && (
          <div className="space-y-6 text-xs">
            <div className="flex items-center justify-between p-3 rounded-xl bg-fintech-card border border-fintech-border">
              <span className="text-slate-400 font-medium">Status</span>
              <Badge variant={selectedNode.status}>{selectedNode.status}</Badge>
            </div>

            <div className="p-4 rounded-xl bg-fintech-card border border-fintech-border text-center">
              <span className="text-slate-400 text-xs block mb-1">Current Net Balance</span>
              <span className="text-2xl font-bold font-mono text-white">
                {formatPaiseToINR(selectedNode.balance_paise)}
              </span>
              <div className="mt-1">
                {selectedNode.balance_paise > 0 ? (
                  <span className="text-emerald-400 font-semibold flex items-center justify-center gap-1">
                    <TrendingUp className="w-3.5 h-3.5" /> Net Creditor
                  </span>
                ) : selectedNode.balance_paise < 0 ? (
                  <span className="text-rose-400 font-semibold flex items-center justify-center gap-1">
                    <TrendingDown className="w-3.5 h-3.5" /> Net Debtor
                  </span>
                ) : (
                  <span className="text-slate-400">Settled (₹0.00)</span>
                )}
              </div>
            </div>

            {/* In/Out Transfer Stats */}
            <div className="grid grid-cols-2 gap-3">
              <div className="p-3 rounded-xl bg-fintech-surface border border-fintech-borderSubtle">
                <span className="text-[10px] text-slate-400 uppercase font-semibold block">Incoming Edges</span>
                <span className="text-lg font-bold text-white mt-1 block font-mono">{incomingEdges.length}</span>
              </div>
              <div className="p-3 rounded-xl bg-fintech-surface border border-fintech-borderSubtle">
                <span className="text-[10px] text-slate-400 uppercase font-semibold block">Outgoing Edges</span>
                <span className="text-lg font-bold text-white mt-1 block font-mono">{outgoingEdges.length}</span>
              </div>
            </div>

            {/* Connected Transactions List */}
            <div>
              <h4 className="font-semibold text-white mb-2">Connected Financial Edges</h4>
              <div className="max-h-56 overflow-y-auto space-y-2 divide-y divide-fintech-borderSubtle">
                {[...incomingEdges, ...outgoingEdges].map((edge) => {
                  const edgeId = edge.tx_id || edge.id;
                  const fromId = edge.from_id || edge.sender_id;
                  const toId = edge.to_id || edge.receiver_id;
                  const amtStr = edge.amount_str || edge.amount_formatted || '0.00';
                  return (
                    <div key={edgeId} className="pt-2 flex items-center justify-between">
                      <div>
                        <div className="font-semibold text-white">Tx #{edgeId}</div>
                        <div className="text-[10px] text-slate-400">
                          #{fromId} &rarr; #{toId}
                        </div>
                      </div>
                      <div className="text-right">
                        <div className="font-mono font-bold text-white">{formatAmountString(amtStr)}</div>
                        <Badge variant={edge.status}>{edge.status}</Badge>
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          </div>
        )}
      </Drawer>

      {/* Edge Detail Drawer */}
      <Drawer
        isOpen={!!selectedEdge}
        onClose={() => setSelectedEdge(null)}
        title={`Transaction Edge #${selectedEdge?.tx_id || selectedEdge?.id}`}
        subtitle="Directed transaction edge"
      >
        {selectedEdge && (
          <div className="space-y-6 text-xs">
            <div className="flex items-center justify-between p-3 rounded-xl bg-fintech-card border border-fintech-border">
              <span className="text-slate-400 font-medium">Status</span>
              <Badge variant={selectedEdge.status}>{selectedEdge.status}</Badge>
            </div>

            <div className="p-4 rounded-xl bg-fintech-card border border-fintech-border text-center">
              <span className="text-slate-400 text-xs block mb-1">Edge Weight (Amount)</span>
              <span className="text-2xl font-bold font-mono text-white">
                {formatAmountString(selectedEdge.amount_str || selectedEdge.amount_formatted || '0.00')}
              </span>
              <span className="text-[10px] text-slate-400 block mt-1 font-mono">
                {(selectedEdge.amount_paise ?? 0).toLocaleString()} paise
              </span>
            </div>

            <div className="space-y-3">
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">From (Source Node):</span>
                <span className="text-white font-medium">Entity #{selectedEdge.from_id || selectedEdge.sender_id}</span>
              </div>
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">To (Target Node):</span>
                <span className="text-white font-medium">Entity #{selectedEdge.to_id || selectedEdge.receiver_id}</span>
              </div>
              {selectedEdge.reversal_of_id && (
                <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                  <span className="text-slate-400">Reversal Of Tx:</span>
                  <span className="text-rose-400 font-mono">#{selectedEdge.reversal_of_id}</span>
                </div>
              )}
              <div className="flex justify-between py-2 border-b border-fintech-borderSubtle">
                <span className="text-slate-400">Recorded Timestamp:</span>
                <span className="text-slate-300 font-mono">{new Date(selectedEdge.created_at).toLocaleString()}</span>
              </div>
            </div>
          </div>
        )}
      </Drawer>
    </div>
  );
};
