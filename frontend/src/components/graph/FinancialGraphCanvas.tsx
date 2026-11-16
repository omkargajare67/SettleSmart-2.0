import React, { useState, useRef, useEffect, useMemo } from 'react';
import { GraphNode, GraphEdge } from '../../types';
import { formatAmountString } from '../../utils/currency';
import { ZoomIn, ZoomOut, RotateCcw, Search, Filter, Network } from 'lucide-react';

interface FinancialGraphCanvasProps {
  nodes: GraphNode[];
  edges: GraphEdge[];
  selectedNodeId?: number | null;
  selectedEdgeId?: number | null;
  onSelectNode?: (node: GraphNode | null) => void;
  onSelectEdge?: (edge: GraphEdge | null) => void;
  highlightTxId?: number | null;
}

export const FinancialGraphCanvas: React.FC<FinancialGraphCanvasProps> = ({
  nodes,
  edges,
  selectedNodeId,
  selectedEdgeId,
  onSelectNode,
  onSelectEdge,
  highlightTxId,
}) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const [zoom, setZoom] = useState<number>(1);
  const [pan, setPan] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
  const [isDraggingCanvas, setIsDraggingCanvas] = useState<boolean>(false);
  const [dragStart, setDragStart] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
  const [draggingNodeId, setDraggingNodeId] = useState<number | null>(null);
  const [searchQuery, setSearchQuery] = useState<string>('');
  const [statusFilter, setStatusFilter] = useState<string>('ALL');

  const [positions, setPositions] = useState<{ [id: number]: { x: number; y: number } }>({});

  useEffect(() => {
    if (nodes.length === 0) return;

    setPositions((prev) => {
      const updated: { [id: number]: { x: number; y: number } } = { ...prev };
      const width = containerRef.current?.clientWidth || 800;
      const height = containerRef.current?.clientHeight || 580;
      const centerX = Math.round(width / 2);
      const centerY = Math.round(height / 2);

      if (nodes.length === 1) {
        if (!updated[nodes[0].id]) {
          updated[nodes[0].id] = { x: centerX, y: centerY };
        }
      } else if (nodes.length === 2) {
        if (!updated[nodes[0].id]) {
          updated[nodes[0].id] = { x: centerX - 180, y: centerY };
        }
        if (!updated[nodes[1].id]) {
          updated[nodes[1].id] = { x: centerX + 180, y: centerY };
        }
      } else {
        const radius = Math.min(220, Math.max(130, 80 + nodes.length * 20));
        nodes.forEach((node, idx) => {
          if (!updated[node.id]) {
            const angle = (idx / nodes.length) * 2 * Math.PI - Math.PI / 2;
            updated[node.id] = {
              x: Math.round(centerX + radius * Math.cos(angle)),
              y: Math.round(centerY + radius * Math.sin(angle)),
            };
          }
        });
      }
      return updated;
    });
  }, [nodes]);

  const handleMouseDown = (e: React.MouseEvent) => {
    if (draggingNodeId !== null) return;
    setIsDraggingCanvas(true);
    setDragStart({ x: e.clientX - pan.x, y: e.clientY - pan.y });
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (draggingNodeId !== null) {
      const rect = containerRef.current?.getBoundingClientRect();
      if (rect) {
        const mouseX = (e.clientX - rect.left - pan.x) / zoom;
        const mouseY = (e.clientY - rect.top - pan.y) / zoom;
        setPositions((prev) => ({
          ...prev,
          [draggingNodeId]: { x: Math.round(mouseX), y: Math.round(mouseY) },
        }));
      }
    } else if (isDraggingCanvas) {
      setPan({ x: e.clientX - dragStart.x, y: e.clientY - dragStart.y });
    }
  };

  const handleMouseUp = () => {
    setIsDraggingCanvas(false);
    setDraggingNodeId(null);
  };

  const handleWheel = (e: React.WheelEvent) => {
    const factor = e.deltaY < 0 ? 1.08 : 0.92;
    setZoom((prev) => Math.min(2.5, Math.max(0.4, prev * factor)));
  };

  const handleZoom = (factor: number) => {
    setZoom((prev) => Math.min(2.5, Math.max(0.4, prev * factor)));
  };

  const resetView = () => {
    setZoom(1);
    setPan({ x: 0, y: 0 });
  };

  const handleSearchSelect = (nodeId: number) => {
    const pos = positions[nodeId];
    if (pos && containerRef.current) {
      const rect = containerRef.current.getBoundingClientRect();
      setPan({
        x: rect.width / 2 - pos.x * zoom,
        y: rect.height / 2 - pos.y * zoom,
      });
      const node = nodes.find((n) => n.id === nodeId) || null;
      onSelectNode?.(node);
    }
  };

  const getEdgeTxId = (e: GraphEdge): number => e.tx_id ?? e.id ?? 0;
  const getEdgeFromId = (e: GraphEdge): number => e.from_id ?? e.sender_id ?? 0;
  const getEdgeToId = (e: GraphEdge): number => e.to_id ?? e.receiver_id ?? 0;

  const filteredEdges = useMemo(() => {
    if (statusFilter === 'ALL') return edges;
    return edges.filter((e) => e.status === statusFilter);
  }, [edges, statusFilter]);

  const edgeCurves = useMemo(() => {
    const pairGroups: { [key: string]: GraphEdge[] } = {};

    filteredEdges.forEach((edge) => {
      const fromId = getEdgeFromId(edge);
      const toId = getEdgeToId(edge);
      const key = `${Math.min(fromId, toId)}-${Math.max(fromId, toId)}`;
      if (!pairGroups[key]) pairGroups[key] = [];
      pairGroups[key].push(edge);
    });

    const result: { [txId: number]: { path: string; labelX: number; labelY: number } } = {};

    filteredEdges.forEach((edge) => {
      const txId = getEdgeTxId(edge);
      const fromId = getEdgeFromId(edge);
      const toId = getEdgeToId(edge);
      const source = positions[fromId] || { x: 250, y: 280 };
      const target = positions[toId] || { x: 550, y: 280 };

      const key = `${Math.min(fromId, toId)}-${Math.max(fromId, toId)}`;
      const group = pairGroups[key] || [edge];
      const indexInGroup = group.indexOf(edge);
      const totalInGroup = group.length;

      const dx = target.x - source.x;
      const dy = target.y - source.y;
      const dist = Math.sqrt(dx * dx + dy * dy) || 1;

      const nx = -dy / dist;
      const ny = dx / dist;

      const isReverse = fromId > toId;
      const baseOffset = (indexInGroup - (totalInGroup - 1) / 2) * 35;
      const curvature = totalInGroup === 1 ? (isReverse ? -30 : 30) : baseOffset;

      const midX = (source.x + target.x) / 2 + nx * curvature;
      const midY = (source.y + target.y) / 2 + ny * curvature;

      const path = `M ${source.x} ${source.y} Q ${midX} ${midY} ${target.x} ${target.y}`;
      result[txId] = { path, labelX: midX, labelY: midY };
    });

    return result;
  }, [filteredEdges, positions]);

  return (
    <div
      ref={containerRef}
      onMouseDown={handleMouseDown}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onWheel={handleWheel}
      className="relative w-full h-[580px] bg-fintech-dark/95 rounded-2xl border border-fintech-borderSubtle overflow-hidden select-none cursor-grab active:cursor-grabbing"
    >
      <div className="absolute inset-0 bg-[radial-gradient(#1E293B_1px,transparent_1px)] [background-size:24px_24px] opacity-40 pointer-events-none" />

      {nodes.length === 0 && (
        <div className="absolute inset-0 flex flex-col items-center justify-center pointer-events-none z-10 p-6 text-center">
          <div className="w-12 h-12 rounded-2xl bg-fintech-surface border border-fintech-border flex items-center justify-center mb-3 text-slate-500">
            <Network className="w-6 h-6" />
          </div>
          <p className="text-sm font-semibold text-white">No Graph Data</p>
          <p className="text-xs text-slate-400 mt-1 max-w-sm">
            No active entities or transactions recorded yet. Create entities and execute transactions to generate directed transaction paths.
          </p>
        </div>
      )}

      {/* Top Floating Controls */}
      <div className="absolute top-4 left-4 z-20 flex flex-wrap items-center gap-2">
        <div className="relative">
          <Search className="w-3.5 h-3.5 text-slate-400 absolute left-3 top-2.5" />
          <input
            type="text"
            placeholder="Search entity..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="pl-8 pr-3 py-1.5 rounded-xl text-xs bg-fintech-surface/90 border border-fintech-border text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500 w-44"
          />
          {searchQuery && (
            <div className="absolute left-0 top-10 w-52 bg-fintech-surface border border-fintech-border rounded-xl shadow-2xl py-1 z-30 max-h-48 overflow-y-auto">
              {nodes
                .filter((n) => n.name.toLowerCase().includes(searchQuery.toLowerCase()) || String(n.id).includes(searchQuery))
                .slice(0, 5)
                .map((n) => (
                  <button
                    key={n.id}
                    onClick={() => {
                      handleSearchSelect(n.id);
                      setSearchQuery('');
                    }}
                    className="w-full text-left px-3 py-1.5 text-xs text-slate-300 hover:bg-fintech-elevated hover:text-white"
                  >
                    <span className="font-semibold text-white">{n.name}</span>{' '}
                    <span className="text-[10px] text-slate-400 font-mono">(#{n.id})</span>
                  </button>
                ))}
            </div>
          )}
        </div>

        <div className="flex items-center gap-1 bg-fintech-surface/90 p-1 rounded-xl border border-fintech-border">
          <Filter className="w-3.5 h-3.5 text-slate-400 ml-1.5" />
          {['ALL', 'Completed', 'Pending', 'Reversed'].map((st) => (
            <button
              key={st}
              onClick={() => setStatusFilter(st)}
              className={`px-2.5 py-1 rounded-lg text-[11px] font-medium transition-colors ${
                statusFilter === st
                  ? 'bg-fintech-elevated text-emerald-400 font-semibold'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              {st}
            </button>
          ))}
        </div>
      </div>

      {/* Zoom / Reset Buttons */}
      <div className="absolute top-4 right-4 z-20 flex flex-col gap-1.5 bg-fintech-surface/90 border border-fintech-border p-1.5 rounded-2xl shadow-xl">
        <button
          onClick={() => handleZoom(1.2)}
          className="p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-fintech-elevated transition-colors"
          title="Zoom in"
        >
          <ZoomIn className="w-4 h-4" />
        </button>
        <button
          onClick={() => handleZoom(0.8)}
          className="p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-fintech-elevated transition-colors"
          title="Zoom out"
        >
          <ZoomOut className="w-4 h-4" />
        </button>
        <button
          onClick={resetView}
          className="p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-fintech-elevated transition-colors"
          title="Reset View"
        >
          <RotateCcw className="w-4 h-4" />
        </button>
      </div>

      {/* Main SVG Canvas */}
      <svg
        id="graph-svg"
        className="w-full h-full"
      >
        <defs>
          <marker id="arrow-completed" viewBox="0 0 10 10" refX="28" refY="5" markerWidth="6" markerHeight="6" orient="auto">
            <path d="M 0 1 L 10 5 L 0 9 z" fill="#10B981" />
          </marker>
          <marker id="arrow-pending" viewBox="0 0 10 10" refX="28" refY="5" markerWidth="6" markerHeight="6" orient="auto">
            <path d="M 0 1 L 10 5 L 0 9 z" fill="#F59E0B" />
          </marker>
          <marker id="arrow-reversed" viewBox="0 0 10 10" refX="28" refY="5" markerWidth="6" markerHeight="6" orient="auto">
            <path d="M 0 1 L 10 5 L 0 9 z" fill="#EF4444" />
          </marker>
        </defs>

        <rect width="100%" height="100%" fill="transparent" />

        <g transform={`translate(${pan.x}, ${pan.y}) scale(${zoom})`}>
          {/* Directed Edges */}
          {filteredEdges.map((edge) => {
            const txId = getEdgeTxId(edge);
            const fromId = getEdgeFromId(edge);
            const toId = getEdgeToId(edge);
            const curve = edgeCurves[txId];
            if (!curve) return null;

            const isSelected = selectedEdgeId === txId;
            const isHighlighted = highlightTxId === txId;
            const isNodeConnected = selectedNodeId && (fromId === selectedNodeId || toId === selectedNodeId);

            let strokeColor = '#334155';
            let markerId = 'arrow-completed';

            if (edge.status === 'Completed') {
              strokeColor = '#10B981';
              markerId = 'arrow-completed';
            } else if (edge.status === 'Pending') {
              strokeColor = '#F59E0B';
              markerId = 'arrow-pending';
            } else if (edge.status === 'Reversed') {
              strokeColor = '#EF4444';
              markerId = 'arrow-reversed';
            }

            const strokeWidth = isSelected || isHighlighted ? 3.5 : isNodeConnected ? 2.5 : 1.5;
            const amtPaise = edge.amount_paise ?? Math.round(Number(edge.amount_formatted || 0) * 100);
            const amtRupees = Math.round(amtPaise / 100);

            return (
              <g
                key={txId}
                onClick={(e) => {
                  e.stopPropagation();
                  onSelectEdge?.(edge);
                  onSelectNode?.(null);
                }}
                className="cursor-pointer group"
              >
                <path d={curve.path} fill="none" stroke="transparent" strokeWidth="20" />

                <path
                  d={curve.path}
                  fill="none"
                  stroke={strokeColor}
                  strokeWidth={strokeWidth}
                  strokeDasharray={edge.status === 'Pending' ? '4 3' : undefined}
                  markerEnd={`url(#${markerId})`}
                  className={`transition-all ${isHighlighted ? 'animate-pulse' : ''}`}
                  opacity={selectedNodeId && !isNodeConnected ? 0.25 : 0.85}
                />

                <g transform={`translate(${curve.labelX}, ${curve.labelY})`}>
                  <rect
                    x="-34"
                    y="-11"
                    width="68"
                    height="22"
                    rx="6"
                    fill="#0F172A"
                    stroke={isSelected ? '#10B981' : strokeColor}
                    strokeWidth="1.2"
                    opacity="0.95"
                  />
                  <text
                    x="0"
                    y="4"
                    textAnchor="middle"
                    fill="#F1F5F9"
                    fontSize="10"
                    fontFamily="monospace"
                    fontWeight="bold"
                  >
                    ₹{amtRupees.toLocaleString('en-IN')}
                  </text>
                </g>
              </g>
            );
          })}

          {/* Nodes */}
          {nodes.map((node) => {
            const pos = positions[node.id] || { x: 300, y: 300 };
            const isSelected = selectedNodeId === node.id;
            const bal = node.balance_paise ?? 0;
            const isCreditor = bal > 0;
            const isDebtor = bal < 0;

            return (
              <g
                key={node.id}
                transform={`translate(${pos.x}, ${pos.y})`}
                onMouseDown={(e) => {
                  e.stopPropagation();
                  setDraggingNodeId(node.id);
                  onSelectNode?.(node);
                  onSelectEdge?.(null);
                }}
                className="cursor-move group"
              >
                {isSelected && (
                  <circle
                    r="36"
                    fill="none"
                    stroke="#10B981"
                    strokeWidth="2"
                    strokeDasharray="4 2"
                    className="animate-spin"
                    style={{ animationDuration: '10s' }}
                  />
                )}

                <circle
                  r="26"
                  fill="#161F30"
                  stroke={isSelected ? '#10B981' : isCreditor ? '#059669' : isDebtor ? '#E11D48' : '#334155'}
                  strokeWidth={isSelected ? 3 : 2}
                  className="transition-colors group-hover:fill-slate-800"
                />

                <text
                  x="0"
                  y="-3"
                  textAnchor="middle"
                  fill="#FFFFFF"
                  fontSize="11"
                  fontWeight="bold"
                  className="pointer-events-none"
                >
                  #{node.id}
                </text>

                <text
                  x="0"
                  y="10"
                  textAnchor="middle"
                  fill={isCreditor ? '#34D399' : isDebtor ? '#FB7185' : '#94A3B8'}
                  fontSize="8"
                  fontFamily="monospace"
                  fontWeight="bold"
                  className="pointer-events-none"
                >
                  {bal === 0 ? '₹0' : formatAmountString(String(Math.abs(bal) / 100))}
                </text>

                <text
                  x="0"
                  y="42"
                  textAnchor="middle"
                  fill="#CBD5E1"
                  fontSize="11"
                  fontWeight="500"
                  className="pointer-events-none"
                >
                  {node.name.length > 15 ? `${node.name.slice(0, 13)}…` : node.name}
                </text>
              </g>
            );
          })}
        </g>
      </svg>
    </div>
  );
};