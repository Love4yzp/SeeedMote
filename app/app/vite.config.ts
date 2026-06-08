import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const frontendPort = Number(process.env.SEEEDMOTE_FRONTEND_PORT ?? 5173);
const backendTarget = process.env.SEEEDMOTE_BACKEND_URL ?? 'http://localhost:3001';
const backendWsTarget = backendTarget.replace(/^http:/, 'ws:').replace(/^https:/, 'wss:');

export default defineConfig({
  plugins: [react()],
  server: {
    port: frontendPort,
    strictPort: false,
    proxy: {
      '/api': backendTarget,
      '/assets': backendTarget,
      '/ws': { target: backendWsTarget, ws: true },
    },
  },
  build: {
    outDir: 'dist',
  },
});
