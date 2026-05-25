import mqtt from 'mqtt';
import { EventStore, type GatewayStatus, type MotionEvent } from './eventStore.js';

const EVENT_TOPIC = 'mote/v1/+/event';
const STATUS_TOPIC = 'mote/v1/+/status';

export interface MqttOptions {
  broker: string;
  port: number;
  username?: string;
  password?: string;
}

export class MqttClient {
  readonly store = new EventStore();
  private client: mqtt.MqttClient;
  private connected = false;

  constructor(opts: MqttOptions) {
    const url = `mqtt://${opts.broker}:${opts.port}`;
    this.client = mqtt.connect(url, {
      clientId: 'seeedmote-retail',
      username: opts.username,
      password: opts.password,
      reconnectPeriod: 2000,
    });

    this.client.on('connect', () => {
      this.connected = true;
      console.log(`MQTT connected to ${url}`);
      this.client.subscribe([EVENT_TOPIC, STATUS_TOPIC], { qos: 1 });
    });

    this.client.on('disconnect', () => {
      this.connected = false;
      console.log('MQTT disconnected');
    });

    this.client.on('error', (err) => {
      console.warn('MQTT error:', err.message);
    });

    this.client.on('message', (topic, payload) => {
      let data: Record<string, unknown>;
      try {
        data = JSON.parse(payload.toString());
      } catch {
        console.warn(`non-JSON on ${topic}`);
        return;
      }
      if (topic.endsWith('/event')) {
        const stored = this.store.addEvent(data as Omit<MotionEvent, '_received_at'>);
        if (stored) this.onEvent?.(stored);
      } else if (topic.endsWith('/status')) {
        const parts = topic.split('/');
        const gwId = (data.gw_id as string) || parts[2] || '?';
        const stored = this.store.setGatewayStatus(gwId, data as Omit<GatewayStatus, '_received_at'>);
        this.onStatus?.(gwId, stored);
      }
    });
  }

  start(): void {
    // MQTT.js starts connecting immediately in the constructor.
  }

  isConnected(): boolean {
    return this.connected;
  }

  onEvent?: (ev: MotionEvent) => void;
  onStatus?: (gwId: string, status: GatewayStatus) => void;

  destroy(): void {
    this.client.end();
  }
}
