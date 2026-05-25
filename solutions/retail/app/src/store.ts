import { create } from 'zustand';
import type { MotionEvent, GatewayStatus } from './types';

const EVENT_HISTORY_MAX = 500;

interface Store {
  events: MotionEvent[];
  gateways: Record<string, GatewayStatus>;
  total: number;
  brokerConnected: boolean;
  wsConnected: boolean;
  mock: boolean;

  addEvent: (ev: MotionEvent) => void;
  setGateway: (gwId: string, status: GatewayStatus) => void;
  applySnapshot: (s: { events: MotionEvent[]; gateways: Record<string, GatewayStatus>; total: number; connected: boolean; mock: boolean }) => void;
  setWsConnected: (v: boolean) => void;
}

export const useStore = create<Store>((set) => ({
  events: [],
  gateways: {},
  total: 0,
  brokerConnected: false,
  wsConnected: false,
  mock: false,

  addEvent: (ev) =>
    set((s) => {
      const events = [ev, ...s.events].slice(0, EVENT_HISTORY_MAX);
      return { events, total: s.total + 1, brokerConnected: true };
    }),

  setGateway: (gwId, status) =>
    set((s) => ({ gateways: { ...s.gateways, [gwId]: status } })),

  applySnapshot: ({ events, gateways, total, connected, mock }) =>
    set({ events, gateways, total, brokerConnected: connected, mock }),

  setWsConnected: (wsConnected) => set({ wsConnected }),
}));
