import React, { createContext, useContext, useEffect, useState, useRef, useCallback } from 'react';
import { useAuth } from './AuthContext';
import { WebSocketState, WebSocketEventMessage } from '../types';
import { useToast } from './ToastContext';

type EventListener = (event: WebSocketEventMessage) => void;

interface WebSocketContextType {
  connectionState: WebSocketState;
  lastSeenSequence: number;
  setWatermark: (seq: number) => void;
  subscribeToEvents: (listener: EventListener) => () => void;
  reconnect: () => void;
}

const WebSocketContext = createContext<WebSocketContextType | undefined>(undefined);

export const WebSocketProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const { isAuthenticated } = useAuth();
  const { showToast } = useToast();
  const [connectionState, setConnectionState] = useState<WebSocketState>('OFFLINE');
  const [lastSeenSequence, setLastSeenSequence] = useState<number>(0);
  const watermarkRef = useRef<number>(0);
  const wsRef = useRef<WebSocket | null>(null);
  const listenersRef = useRef<Set<EventListener>>(new Set());
  const reconnectTimeoutRef = useRef<any>(null);
  const heartbeatIntervalRef = useRef<any>(null);
  const consecutiveFailsRef = useRef<number>(0);

  const setWatermark = useCallback((seq: number) => {
    if (seq > watermarkRef.current) {
      watermarkRef.current = seq;
      setLastSeenSequence(seq);
    }
  }, []);

  const subscribeToEvents = useCallback((listener: EventListener) => {
    listenersRef.current.add(listener);
    return () => {
      listenersRef.current.delete(listener);
    };
  }, []);

  const connect = useCallback(() => {
    if (!isAuthenticated) {
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
      setConnectionState('OFFLINE');
      return;
    }

    if (wsRef.current && (wsRef.current.readyState === WebSocket.OPEN || wsRef.current.readyState === WebSocket.CONNECTING)) {
      return;
    }

    const isVercel = typeof window !== 'undefined' && window.location.host.includes('vercel.app');
    const defaultWs = isVercel
      ? 'wss://hewlett-epson-collectible-lunch.trycloudflare.com'
      : (typeof window !== 'undefined'
          ? `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}`
          : 'ws://localhost:8000');
    const wsBase = (
      import.meta.env.VITE_WS_URL && !import.meta.env.VITE_WS_URL.includes('localhost')
        ? import.meta.env.VITE_WS_URL
        : defaultWs
    ).replace(/\/+$/, '');
    const wsUrl = `${wsBase}/ws/graph`;

    try {
      setConnectionState(consecutiveFailsRef.current > 0 ? 'RECONNECTING' : 'OFFLINE');
      const ws = new WebSocket(wsUrl);
      wsRef.current = ws;

      ws.onopen = () => {
        consecutiveFailsRef.current = 0;
        setConnectionState('LIVE');
        showToast('Real-time connection established', 'info');

        // Subscribe with watermark
        ws.send(JSON.stringify({
          type: 'SUBSCRIBE_GRAPH',
          last_seen_sequence: watermarkRef.current,
        }));

        if (heartbeatIntervalRef.current) clearInterval(heartbeatIntervalRef.current);
        heartbeatIntervalRef.current = setInterval(() => {
          if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'PING' }));
          }
        }, 25000);
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data && data.type === 'PONG') {
            return;
          }

          if (data && data.event_type && data.sequence !== undefined) {
            const evMsg = data as WebSocketEventMessage;
            if (evMsg.sequence > watermarkRef.current) {
              watermarkRef.current = evMsg.sequence;
              setLastSeenSequence(evMsg.sequence);
            }

            listenersRef.current.forEach((listener) => {
              try {
                listener(evMsg);
              } catch (e) {
                console.error('Error in event listener', e);
              }
            });

            if (evMsg.event_type === 'TRANSACTION_CREATED') {
              showToast(`New Transaction #${evMsg.payload?.transaction_id || evMsg.payload?.id || ''} recorded`, 'info');
            } else if (evMsg.event_type === 'TRANSACTION_STATUS_CHANGED') {
              showToast(`Transaction #${evMsg.payload?.transaction_id || evMsg.payload?.id || ''} status: ${evMsg.payload?.status}`, 'info');
            }
          }
        } catch (e) {
          console.warn('Failed to parse WebSocket message', e);
        }
      };

      ws.onclose = () => {
        if (heartbeatIntervalRef.current) clearInterval(heartbeatIntervalRef.current);
        wsRef.current = null;
        consecutiveFailsRef.current += 1;
        setConnectionState('RECONNECTING');

        const delay = Math.min(1000 * Math.pow(1.5, consecutiveFailsRef.current), 15000);
        if (reconnectTimeoutRef.current) clearTimeout(reconnectTimeoutRef.current);
        reconnectTimeoutRef.current = setTimeout(() => {
          connect();
        }, delay);
      };

      ws.onerror = () => {
        ws.close();
      };
    } catch (err) {
      console.warn('WebSocket connection error', err);
      setConnectionState('OFFLINE');
    }
  }, [isAuthenticated, showToast]);

  useEffect(() => {
    connect();
    return () => {
      if (reconnectTimeoutRef.current) clearTimeout(reconnectTimeoutRef.current);
      if (heartbeatIntervalRef.current) clearInterval(heartbeatIntervalRef.current);
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, [connect]);

  const reconnect = useCallback(() => {
    consecutiveFailsRef.current = 0;
    if (wsRef.current) {
      wsRef.current.close();
    }
    connect();
  }, [connect]);

  return (
    <WebSocketContext.Provider
      value={{
        connectionState,
        lastSeenSequence,
        setWatermark,
        subscribeToEvents,
        reconnect,
      }}
    >
      {children}
    </WebSocketContext.Provider>
  );
};

export function useWebSocket(): WebSocketContextType {
  const context = useContext(WebSocketContext);
  if (!context) {
    throw new Error('useWebSocket must be used within a WebSocketProvider');
  }
  return context;
}
