import { create } from 'zustand';
import type { InteractionEvent, GatewayStatus } from './types';

const EVENT_HISTORY_MAX = 500;

interface Store {
  events: InteractionEvent[];
  gateways: Record<string, GatewayStatus>;
  total: number;
  brokerConnected: boolean;
  wsConnected: boolean;
  mock: boolean;
  // Monotonic 1Hz clock tick (seconds). Components that filter events by an
  // age window must subscribe to this so they re-render when entries expire
  // even without a new MQTT event.
  nowSec: number;

  addEvent: (ev: InteractionEvent) => void;
  setGateway: (gwId: string, status: GatewayStatus) => void;
  applySnapshot: (s: { events: InteractionEvent[]; gateways: Record<string, GatewayStatus>; total: number; connected: boolean; mock: boolean }) => void;
  setWsConnected: (v: boolean) => void;
  setBrokerConnected: (v: boolean) => void;
  tickNow: () => void;
}

export const useStore = create<Store>((set) => ({
  events: [],
  gateways: {},
  total: 0,
  brokerConnected: false,
  wsConnected: false,
  mock: false,
  nowSec: Math.floor(Date.now() / 1000),

  addEvent: (ev) =>
    set((s) => {
      const events = [ev, ...s.events].slice(0, EVENT_HISTORY_MAX);
      return { events, total: s.total + 1 };
    }),

  setGateway: (gwId, status) =>
    set((s) => ({ gateways: { ...s.gateways, [gwId]: status } })),

  applySnapshot: ({ events, gateways, total, connected, mock }) =>
    set({ events, gateways, total, brokerConnected: connected, mock }),

  setWsConnected: (wsConnected) => set({ wsConnected }),
  setBrokerConnected: (brokerConnected) => set({ brokerConnected }),
  tickNow: () => set({ nowSec: Math.floor(Date.now() / 1000) }),
}));
