export interface GatewayStatus {
  gw_id: string;
  alias: string | null;
  gw_alias: string | null;
  gw_label: string;
  online: boolean;
  last_seen: number;
  last_rssi: number | null;
  version?: string;
}

export interface ShoeInfo {
  sku: string;
  name: string;
  color: string;
  price: number;
  image: string;
}

export interface InteractionEvent {
  action: 'picked_up';
  action_label: string;
  item_label: string;
  registered: boolean;
  item: ShoeInfo | null;
  source: {
    mote_mac: string;
    packet_id: number;
    rssi: number;
    gw_id: string;
    gw_alias: string | null;
    gw_label: string;
  };
  _received_at: number;
}

export type WsMessage =
  | { type: 'snapshot'; events: InteractionEvent[]; gateways: Record<string, GatewayStatus>; total: number; connected: boolean; mock: boolean }
  | { type: 'event'; payload: InteractionEvent }
  | { type: 'gateway'; gwId: string; payload: GatewayStatus }
  | { type: 'transport'; connected: boolean };
