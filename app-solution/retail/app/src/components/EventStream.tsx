import { useStore } from '../store';
import type { ShoeInfo } from '../types';

const EVENT_TAIL = 50;

interface EventBadgeProps {
  moving: boolean;
  vibration: boolean;
}

function EventBadge({ moving, vibration }: EventBadgeProps) {
  if (vibration) {
    return (
      <span className="relative pl-3 text-xs font-semibold text-blue-700 before:absolute before:left-0 before:top-1/2 before:-translate-y-1/2 before:w-1.5 before:h-1.5 before:rounded-full before:bg-blue-500">
        振动
      </span>
    );
  }
  if (moving) {
    return (
      <span className="relative pl-3 text-xs font-semibold text-slate-600 before:absolute before:left-0 before:top-1/2 before:-translate-y-1/2 before:w-1.5 before:h-1.5 before:rounded-full before:bg-slate-400">
        移动
      </span>
    );
  }
  return (
    <span className="relative pl-3 text-xs font-semibold text-slate-400 before:absolute before:left-0 before:top-1/2 before:-translate-y-1/2 before:w-1.5 before:h-1.5 before:rounded-full before:bg-slate-200">
      静止
    </span>
  );
}

interface Props {
  shoes: Record<string, ShoeInfo>;
}

export function EventStream({ shoes }: Props) {
  const events = useStore((s) => s.events.slice(0, EVENT_TAIL));

  return (
    <section className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
      <div className="p-5 border-b border-slate-100 bg-slate-50/50 flex justify-between items-center">
        <div>
          <h2 className="font-bold text-slate-800">实时事件流</h2>
          <p className="text-xs text-slate-500 mt-0.5">latest {EVENT_TAIL} events</p>
        </div>
        <span className="text-xs px-2 py-0.5 rounded-full bg-slate-100 text-slate-600 border border-slate-200 font-semibold">
          {events.length} 条
        </span>
      </div>

      {events.length === 0 ? (
        <div className="text-center py-12 text-slate-400 text-sm">尚无事件</div>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-sm text-left">
            <thead className="text-xs text-slate-500 uppercase bg-slate-50 border-b border-slate-100">
              <tr>
                <th className="px-5 py-3 font-semibold">时间</th>
                <th className="px-5 py-3 font-semibold">SKU</th>
                <th className="px-5 py-3 font-semibold">事件</th>
                <th className="px-5 py-3 font-semibold text-right">ctr</th>
                <th className="px-5 py-3 font-semibold text-right">rssi</th>
                <th className="px-5 py-3 font-semibold">mote</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {events.map((ev, i) => {
                const ts = new Date(ev._received_at * 1000).toLocaleTimeString('zh-CN', { hour12: false });
                const meta = shoes[ev.mote_mac];
                const sku = meta?.sku ?? `(${ev.mote_mac.slice(-4)})`;
                return (
                  <tr
                    key={`${ev.mote_mac}-${ev.ctr}-${i}`}
                    className="hover:bg-slate-50/80 transition-colors"
                  >
                    <td className="px-5 py-3 text-slate-400 font-mono text-xs">{ts}</td>
                    <td className="px-5 py-3 font-semibold text-slate-900">{sku}</td>
                    <td className="px-5 py-3">
                      <EventBadge moving={ev.moving} vibration={ev.vibration} />
                    </td>
                    <td className="px-5 py-3 text-right text-slate-500 font-mono text-xs">{ev.ctr || '—'}</td>
                    <td className="px-5 py-3 text-right text-slate-500 font-mono text-xs">{ev.rssi}</td>
                    <td className="px-5 py-3 text-slate-400 font-mono text-xs">{ev.mote_mac.slice(-6)}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}
