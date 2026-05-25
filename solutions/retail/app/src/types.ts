export interface MotionEvent {
  ts: number;
  gw_id: string;
  mote_mac: string;
  rssi: number;
  moving: boolean;
  vibration: boolean;
  pid: number;
  ctr: number;
  _received_at: number;
}

export interface GatewayStatus {
  online: boolean;
  reason?: string;
  gw_id?: string;
  ip?: string;
  wifi_ssid?: string;
  _received_at: number;
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
  | { type: 'status'; gwId: string; payload: GatewayStatus };
