// Scripted virtual-customer timeline.
// Real mote frames always carry moving=true; put-back is inferred by consumers
// when no recent pickup pulse arrives.

import { EventStore, type GatewayStatus, type MotionEvent } from './eventStore.js';

const GW_ID = '441bf6804166';
const MOTE_SH001 = 'f0e3912cec19';
const MOTE_SH002 = 'f0e3912cec20';
const MOTE_SH003 = 'f0e3912cec21';

const TIMELINE: [number, string, boolean, boolean][] = [
  [0.0,  MOTE_SH001, true,  true],
  [0.5,  MOTE_SH001, true,  false],
  [2.0,  MOTE_SH001, true,  false],
  [4.0,  MOTE_SH001, true,  true],
  [8.0,  MOTE_SH002, true,  true],
  [9.0,  MOTE_SH002, true,  false],
  [14.0, MOTE_SH003, true,  true],
  [15.0, MOTE_SH003, true,  false],
  [16.5, MOTE_SH002, true,  true],
  [18.0, MOTE_SH002, true,  false],
];
const CYCLE_SECONDS = 28_000;

const RSSI: Record<string, number> = {
  [MOTE_SH001]: -58,
  [MOTE_SH002]: -62,
  [MOTE_SH003]: -70,
};

export class MockSource {
  readonly store = new EventStore();
  private counters = new Map<string, number>();
  private timer?: NodeJS.Timeout;
  private cycleStart = Date.now();
  private idx = 0;

  onEvent?: (ev: MotionEvent) => void;
  onStatus?: (gwId: string, status: GatewayStatus) => void;

  constructor() {
    const gwStatus = {
      online: true, reason: 'connected', gw_id: GW_ID,
      ip: '192.168.1.42', wifi_ssid: 'MOCK',
    };
    const storedStatus = this.store.setGatewayStatus(GW_ID, gwStatus);
    // Fire initial status so UI shows Online right away
    setTimeout(() => this.onStatus?.(GW_ID, storedStatus), 50);
  }

  start(): void {
    this.tick();
  }

  stop(): void {
    if (this.timer) clearTimeout(this.timer);
  }

  isConnected(): boolean { return true; }

  private nextCtr(mac: string): number {
    const c = (this.counters.get(mac) ?? 0) + 1;
    this.counters.set(mac, c);
    return c;
  }

  private emit(mac: string, moving: boolean, vibration: boolean): void {
    const ctr = this.nextCtr(mac);
    const ev: Omit<MotionEvent, '_received_at'> = {
      ts: Date.now(),
      gw_id: GW_ID,
      mote_mac: mac,
      rssi: RSSI[mac] ?? -65,
      moving,
      vibration,
      pid: ctr & 0xff,
      ctr,
    };
    const stored = this.store.addEvent(ev);
    if (stored) this.onEvent?.(stored);
  }

  private tick(): void {
    const now = Date.now();
    const inCycle = now - this.cycleStart;

    if (this.idx >= TIMELINE.length) {
      if (inCycle >= CYCLE_SECONDS) {
        this.cycleStart = now;
        this.idx = 0;
        this.timer = setTimeout(() => this.tick(), 0);
      } else {
        this.timer = setTimeout(() => this.tick(), 200);
      }
      return;
    }

    const [offset, mac, moving, vibration] = TIMELINE[this.idx];
    const offsetMs = offset * 1000;
    if (inCycle >= offsetMs) {
      this.emit(mac, moving, vibration);
      this.idx++;
      this.timer = setTimeout(() => this.tick(), 0);
    } else {
      this.timer = setTimeout(() => this.tick(), Math.min(200, offsetMs - inCycle));
    }
  }
}
