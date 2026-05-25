import express from 'express';
import { createServer } from 'http';
import { WebSocketServer, WebSocket } from 'ws';
import { fileURLToPath } from 'url';
import { dirname, join, resolve } from 'path';
import fs from 'fs';
import yaml from 'js-yaml';
import cors from 'cors';
import { MqttClient } from './mqttClient.js';
import { MockSource } from './mockSource.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

// --- CLI args ---
const args = process.argv.slice(2);
const mock = args.includes('--mock');
const brokerIdx = args.indexOf('--broker');
const portIdx = args.indexOf('--port');
const broker = brokerIdx >= 0 ? args[brokerIdx + 1] : (process.env.SEEEDMOTE_BROKER ?? 'localhost');
const mqttPort = portIdx >= 0 ? parseInt(args[portIdx + 1]) : parseInt(process.env.SEEEDMOTE_BROKER_PORT ?? '1883');

// --- Shoes config ---
const shoesFile = resolve(__dirname, '../../shoes.yaml');
const shoesData = yaml.load(fs.readFileSync(shoesFile, 'utf-8')) as any;
const shoes: Record<string, any> = shoesData?.shoes ?? {};

// --- Source (MQTT or mock) ---
const source = mock
  ? new MockSource()
  : new MqttClient({ broker, port: mqttPort,
      username: process.env.SEEEDMOTE_BROKER_USER,
      password: process.env.SEEEDMOTE_BROKER_PASS });

// --- Express app ---
const app = express();
app.use(cors());
app.use(express.json());

// Serve shoes config and assets
app.get('/api/shoes', (_req, res) => res.json(shoes));
app.get('/api/status', (_req, res) => res.json({
  connected: source.isConnected(),
  gateways: source.store.getGateways(),
  total: source.store.getTotal(),
  mock,
}));

// Serve assets from retail dir
const assetsDir = resolve(__dirname, '../../assets');
if (fs.existsSync(assetsDir)) {
  app.use('/assets', express.static(assetsDir));
}

// Serve frontend build (for production)
const frontendDist = resolve(__dirname, '../../app/dist');
if (fs.existsSync(frontendDist)) {
  app.use(express.static(frontendDist));
  app.get('*', (_req, res) => res.sendFile(join(frontendDist, 'index.html')));
}

// --- HTTP + WebSocket server ---
const server = createServer(app);
const wss = new WebSocketServer({ server });

function broadcast(msg: object): void {
  const data = JSON.stringify(msg);
  for (const client of wss.clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(data);
    }
  }
}

// Wire events to WebSocket broadcast
source.onEvent = (ev) => broadcast({ type: 'event', payload: ev });
source.onStatus = (gwId, status) => broadcast({ type: 'status', gwId, payload: status });

wss.on('connection', (ws) => {
  // Send snapshot on connect so new clients don't wait for next event
  ws.send(JSON.stringify({
    type: 'snapshot',
    events: source.store.getEvents(),
    gateways: source.store.getGateways(),
    total: source.store.getTotal(),
    connected: source.isConnected(),
    mock,
  }));
});

// Start
source.start();
const HTTP_PORT = parseInt(process.env.PORT ?? '3001');
server.listen(HTTP_PORT, () => {
  console.log(`Server on http://localhost:${HTTP_PORT} (${mock ? 'mock' : `mqtt://${broker}:${mqttPort}`})`);
  console.log(`WebSocket on ws://localhost:${HTTP_PORT}/ws`);
});
