export interface MotionEvent {
  mote_mac: string;
  gw_id: string;
  rssi: number;
  packet_id: number;
  _received_at: number;
}

export interface GatewayStatus {
  gw_id: string;
  online: boolean;
  last_seen: number;
  last_rssi: number | null;
}

export interface ShoeInfo {
  sku: string;
  name: string;
  color: string;
  price: number;
  image: string;
}

export type WsMessage =
  | { type: 'snapshot'; events: MotionEvent[]; gateways: Record<string, GatewayStatus>; total: number; connected: boolean; mock: boolean }
  | { type: 'event'; payload: MotionEvent }
  | { type: 'gateway'; gwId: string; payload: GatewayStatus };
