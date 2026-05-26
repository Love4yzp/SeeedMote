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

  const gwList = Object.values(gateways);
  const onlineGws = gwList.filter((g) => g.online).length;
  const gwValue = gwList.length ? `${onlineGws}/${gwList.length}` : '—';
  const activePickups = new Set(
    events
      .filter((ev) => nowSec - ev._received_at <= PICKUP_WINDOW_S)
      .map((ev) => ev.source.mote_mac),
  ).size;
  const transportConnected = wsConnected && brokerConnected;

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
          </div>
        </div>
      </div>
      {settingsOpen && <MqttSettingsModal onClose={() => setSettingsOpen(false)} />}
    </header>
  );
}
