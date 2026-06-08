import { useState } from 'react';
import { useStore } from '../store';
import { MqttSettingsModal } from './MqttSettingsModal';

const PICKUP_WINDOW_S = 30;

interface StatItemProps {
  label: string;
  value: string;
  valueClass?: string;
}

function StatItem({ label, value, valueClass = 'text-slate-700' }: StatItemProps) {
  return (
    <div className="flex flex-col items-center">
      <span className="text-slate-400 text-xs font-semibold uppercase tracking-wider">{label}</span>
      <span className={`font-bold text-lg leading-tight mt-0.5 ${valueClass}`}>{value}</span>
    </div>
  );
}

export function Header() {
  const { brokerConnected, wsConnected, gateways, total, mock, events, nowSec } = useStore();
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [gatewayMenuOpen, setGatewayMenuOpen] = useState(false);
  const [locatingGw, setLocatingGw] = useState<string | null>(null);
  const [locateResult, setLocateResult] = useState<{ gwId: string; ok: boolean } | null>(null);

  const gwList = Object.values(gateways);
  const onlineGws = gwList.filter((g) => g.online).length;
  const gwValue = gwList.length ? `${onlineGws}/${gwList.length}` : '—';
  const activePickups = new Set(
    events
      .filter((ev) => nowSec - ev._received_at <= PICKUP_WINDOW_S)
      .map((ev) => ev.source.mote_mac),
  ).size;
  const transportConnected = wsConnected && brokerConnected;

  async function locateGateway(gwId: string) {
    setLocatingGw(gwId);
    setLocateResult(null);
    try {
      const res = await fetch(`/api/gateways/${encodeURIComponent(gwId)}/command`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ command: 'locate' }),
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      setLocateResult({ gwId, ok: true });
    } catch {
      setLocateResult({ gwId, ok: false });
    } finally {
      setLocatingGw(null);
    }
  }

  return (
    <header className="bg-white border-b border-slate-200 sticky top-0 z-40 shadow-sm">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
        <div className="flex justify-between items-center h-16">
          {/* Brand */}
          <div className="flex items-center gap-3">
            <div className="w-8 h-8 rounded-lg flex items-center justify-center text-white font-bold text-lg"
                 style={{ backgroundColor: '#0FAE3C' }}>
              S
            </div>
            <div>
              <h1 className="text-lg font-bold tracking-tight text-slate-900 leading-none">SeeedMote Retail</h1>
              <p className="text-xs text-slate-400 mt-0.5">{mock ? 'Mock source' : 'MQTT source'}</p>
            </div>
          </div>

          {/* Stats */}
          <div className="hidden md:flex items-center gap-8">
            <StatItem
              label="正在互动"
              value={String(activePickups)}
              valueClass="text-slate-700"
            />
            <StatItem
              label="网关在线"
              value={gwValue}
              valueClass={onlineGws > 0 ? 'text-slate-700' : 'text-red-500'}
            />
            <StatItem
              label="互动次数"
              value={String(total)}
              valueClass="text-slate-700"
            />
          </div>

          {/* Connection status + settings */}
          <div className="flex items-center gap-2">
            <div className="flex items-center gap-2 px-3 py-1.5 bg-slate-50 rounded-full border border-slate-200">
              <div className={`w-2 h-2 rounded-full ${transportConnected ? 'bg-green-500 animate-pulse' : 'bg-red-500'}`} />
              <div className="flex flex-col leading-none">
                <span className="text-[10px] text-slate-400 font-bold">{mock ? 'MOCK' : 'MQTT'} / WS</span>
                <span className={`text-xs font-semibold ${transportConnected ? 'text-green-700' : 'text-red-600'}`}>
                  {transportConnected ? 'Connected' : 'Disconnected'}
                </span>
              </div>
            </div>
            {!mock && (
              <button
                onClick={() => setSettingsOpen(true)}
                title="MQTT Settings"
                className="p-2 rounded-full text-slate-400 hover:text-slate-700 hover:bg-slate-100"
              >
                <svg xmlns="http://www.w3.org/2000/svg" className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth={2}>
                  <path strokeLinecap="round" strokeLinejoin="round" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z" />
                  <path strokeLinecap="round" strokeLinejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
                </svg>
              </button>
            )}
            <div className="relative">
              <button
                onClick={() => setGatewayMenuOpen((open) => !open)}
                title="Locate gateway"
                disabled={!gwList.length}
                className="p-2 rounded-full text-slate-400 hover:text-slate-700 hover:bg-slate-100 disabled:opacity-40 disabled:hover:bg-transparent"
              >
                <svg xmlns="http://www.w3.org/2000/svg" className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth={2}>
                  <path strokeLinecap="round" strokeLinejoin="round" d="M12 21s6-5.686 6-11A6 6 0 006 10c0 5.314 6 11 6 11z" />
                  <path strokeLinecap="round" strokeLinejoin="round" d="M12 12.5a2.5 2.5 0 100-5 2.5 2.5 0 000 5z" />
                </svg>
              </button>
              {gatewayMenuOpen && (
                <div className="absolute right-0 mt-2 w-80 max-w-[calc(100vw-2rem)] rounded-lg border border-slate-200 bg-white shadow-lg p-2 z-50">
                  <div className="px-2 py-1.5 text-xs font-semibold text-slate-400 uppercase">Gateway</div>
                  <div className="space-y-1 max-h-64 overflow-auto">
                    {gwList.map((gw) => {
                      const pending = locatingGw === gw.gw_id;
                      const disabled = mock || !transportConnected || !gw.online || pending;
                      return (
                        <div key={gw.gw_id} className="flex items-center justify-between gap-3 rounded-md px-2 py-2 hover:bg-slate-50">
                          <div className="min-w-0">
                            <div className="truncate text-sm font-semibold text-slate-700">{gw.gw_label}</div>
                            <div className="truncate text-xs text-slate-400">{gw.gw_id}</div>
                          </div>
                          <button
                            onClick={() => locateGateway(gw.gw_id)}
                            disabled={disabled}
                            className="shrink-0 rounded-md border border-slate-200 px-2.5 py-1 text-xs font-semibold text-slate-600 hover:border-green-300 hover:text-green-700 disabled:opacity-40 disabled:hover:border-slate-200 disabled:hover:text-slate-600"
                          >
                            {pending ? '发送中' : '定位'}
                          </button>
                        </div>
                      );
                    })}
                  </div>
                  {locateResult && (
                    <div className={`mt-2 px-2 py-1.5 text-xs ${locateResult.ok ? 'text-green-700' : 'text-red-600'}`}>
                      {locateResult.ok ? '定位指令已发送' : '定位指令发送失败'}
                    </div>
                  )}
                </div>
              )}
            </div>
          </div>
        </div>
      </div>
      {settingsOpen && <MqttSettingsModal onClose={() => setSettingsOpen(false)} />}
    </header>
  );
}
