import { useEffect, useRef } from 'react';
import { useStore } from '../store';
import type { WsMessage } from '../types';

export function useWebSocket() {
  const wsRef = useRef<WebSocket | null>(null);
  const addEvent = useStore((s) => s.addEvent);
  const setGateway = useStore((s) => s.setGateway);
  const applySnapshot = useStore((s) => s.applySnapshot);
  const setWsConnected = useStore((s) => s.setWsConnected);

  useEffect(() => {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = `${protocol}//${window.location.host}/ws`;
    let reconnectTimer: ReturnType<typeof setTimeout> | undefined;
    let disposed = false;

    function connect() {
      if (disposed) return;

      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onopen = () => {
        if (!disposed) setWsConnected(true);
      };

      ws.onmessage = (e) => {
        const msg: WsMessage = JSON.parse(e.data);
        if (msg.type === 'snapshot') {
          applySnapshot(msg);
        } else if (msg.type === 'event') {
          addEvent(msg.payload);
        } else if (msg.type === 'gateway') {
          setGateway(msg.gwId, msg.payload);
        }
      };

      ws.onclose = () => {
        if (disposed) return;
        setWsConnected(false);
        reconnectTimer = setTimeout(connect, 2000);
      };

      ws.onerror = () => ws.close();
    }

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);

      const ws = wsRef.current;
      wsRef.current = null;
      if (!ws) return;

      ws.onmessage = null;
      ws.onerror = null;
      ws.onclose = null;
      if (ws.readyState === WebSocket.CONNECTING) {
        ws.onopen = () => ws.close();
      } else {
        ws.close();
      }
    };
  }, [addEvent, setGateway, applySnapshot, setWsConnected]);
}
