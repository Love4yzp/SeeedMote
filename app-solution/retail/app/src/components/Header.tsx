import { useStore } from '../store';

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
  const { brokerConnected, wsConnected, gateways, total, mock, events } = useStore();

  const gwList = Object.values(gateways);
  const onlineGws = gwList.filter((g) => g.online).length;
  const gwValue = gwList.length ? `${onlineGws}/${gwList.length}` : '—';
  const now = Date.now() / 1000;
  const activePickups = new Set(
    events
      .filter((ev) => ev.vibration && now - ev._received_at <= PICKUP_WINDOW_S)
      .map((ev) => ev.mote_mac),
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
              label="Active Pickups"
              value={String(activePickups)}
              valueClass="text-slate-700"
            />
            <StatItem
              label="Gateways Online"
              value={gwValue}
              valueClass={onlineGws > 0 ? 'text-slate-700' : 'text-red-500'}
            />
            <StatItem
              label="Events"
              value={String(total)}
              valueClass="text-slate-700"
            />
          </div>

          {/* Connection status */}
          <div className="flex items-center gap-2 px-3 py-1.5 bg-slate-50 rounded-full border border-slate-200">
            <div className={`w-2 h-2 rounded-full ${transportConnected ? 'bg-green-500 animate-pulse' : 'bg-red-500'}`} />
            <div className="flex flex-col leading-none">
              <span className="text-[10px] text-slate-400 font-bold">{mock ? 'MOCK' : 'MQTT'} / WS</span>
              <span className={`text-xs font-semibold ${transportConnected ? 'text-green-700' : 'text-red-600'}`}>
                {transportConnected ? 'Connected' : 'Disconnected'}
              </span>
            </div>
          </div>
        </div>
      </div>
    </header>
  );
}
