// Thread-safe (single-threaded JS) event buffer with dedup by (mote_mac, ctr).
// Mirrors the Python EventStore logic exactly.

export interface MotionEvent {
  ts: number;
  gw_id: string;
  mote_mac: string;
  rssi: number;
  moving: boolean;
  vibration: boolean;
  pid: number;
  ctr: number;
  _received_at: number; // added by store
}

export interface GatewayStatus {
  online: boolean;
  reason?: string;
  gw_id?: string;
  ip?: string;
  wifi_ssid?: string;
  _received_at: number;
}

const EVENT_HISTORY_MAX = 500;

export class EventStore {
  private events: MotionEvent[] = [];
  private seen = new Set<string>();
  private lastCtr = new Map<string, number>();
  private gwStatus = new Map<string, GatewayStatus>();
  private totalCount = 0;

  addEvent(ev: Omit<MotionEvent, '_received_at'>): MotionEvent | null {
    const mac = ev.mote_mac;
    if (!mac) return null;
    const ctr = ev.ctr ?? 0;

    if (ctr) {
      const last = this.lastCtr.get(mac);
      if (last !== undefined && ctr < last) {
        // mote rebooted — clear its dedup keys
        for (const key of this.seen) {
          if (key.startsWith(mac + ':')) this.seen.delete(key);
        }
      }
      this.lastCtr.set(mac, ctr);
      const key = `${mac}:${ctr}`;
      if (this.seen.has(key)) return null;
      this.seen.add(key);
    }

    const stored: MotionEvent = { ...ev, _received_at: Date.now() / 1000 };
    this.events.unshift(stored); // newest first
    if (this.events.length > EVENT_HISTORY_MAX) this.events.pop();
    this.totalCount++;
    return stored;
  }

  setGatewayStatus(gwId: string, status: Omit<GatewayStatus, '_received_at'>): GatewayStatus {
    const stored = { ...status, _received_at: Date.now() / 1000 };
    this.gwStatus.set(gwId, stored);
    return stored;
  }

  getEvents(): MotionEvent[] {
    return [...this.events];
  }

  getGateways(): Record<string, GatewayStatus> {
    return Object.fromEntries(this.gwStatus);
  }

  getTotal(): number {
    return this.totalCount;
  }
}
