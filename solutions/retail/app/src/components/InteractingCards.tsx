import { useStore } from '../store';
import type { ShoeInfo } from '../types';

const PICKUP_WINDOW_S = 30;

function fmtAge(receivedAt: number): string {
  const delta = Math.max(0, Math.floor(Date.now() / 1000 - receivedAt));
  if (delta < 60) return `${delta}s 前`;
  if (delta < 3600) return `${Math.floor(delta / 60)}m 前`;
  return `${Math.floor(delta / 3600)}h 前`;
}

interface Props {
  shoes: Record<string, ShoeInfo>;
}

export function InteractingCards({ shoes }: Props) {
  const events = useStore((s) => s.events);
  const now = Date.now() / 1000;

  const latestByMac = new Map<string, typeof events[0]>();
  const counts = new Map<string, number>();

  for (const ev of events) {
    if (!ev.vibration) continue;
    if (now - ev._received_at > PICKUP_WINDOW_S) continue;
    counts.set(ev.mote_mac, (counts.get(ev.mote_mac) ?? 0) + 1);
    if (!latestByMac.has(ev.mote_mac)) latestByMac.set(ev.mote_mac, ev);
  }

  const items = [...latestByMac.entries()];

  return (
    <section>
      <div className="flex justify-between items-end mb-4">
        <div>
          <h2 className="text-xl font-bold text-slate-900">正在互动</h2>
          <p className="text-slate-500 text-sm mt-0.5">{PICKUP_WINDOW_S}s pickup window · 实时传感器更新</p>
        </div>
        <div className="flex gap-2">
          <span className="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-semibold bg-green-100 text-green-800">
            正在把玩
          </span>
          <span className="inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-semibold bg-slate-100 text-slate-700">
            已放回
          </span>
        </div>
      </div>

      {items.length === 0 ? (
        <div className="text-center py-10 bg-white rounded-xl border border-dashed border-slate-300">
          <p className="text-slate-400 text-sm">暂无互动 — 等待顾客拿起商品...</p>
        </div>
      ) : (
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
          {items.map(([mac, ev]) => {
            const meta = shoes[mac];
            const count = counts.get(mac) ?? 0;
            return (
              <div
                key={mac}
                className="relative bg-white rounded-xl border-2 p-5 shadow-sm transition-all duration-300"
                style={{ borderColor: '#0FAE3C', boxShadow: '0 8px 24px -4px rgba(15,174,60,0.12)' }}
              >
                <div className="flex justify-between items-start mb-3">
                  <div
                    className="w-10 h-10 rounded-lg flex items-center justify-center text-xl"
                    style={{ backgroundColor: '#f0fdf4', color: '#0FAE3C' }}
                  >
                    ⚡
                  </div>
                  <span className="text-[10px] font-mono text-slate-400 bg-slate-50 px-1.5 py-0.5 rounded border border-slate-100">
                    {mac.slice(-4)}
                  </span>
                </div>

                <div className="flex gap-3 items-center">
                  <div className="w-16 h-12 border border-slate-100 rounded-lg bg-[#f0fdf4] grid place-items-center overflow-hidden flex-shrink-0">
                    {meta ? (
                      <img
                        src={`/assets/${meta.image.replace('assets/', '')}`}
                        alt={meta.sku}
                        className="w-12 h-9 object-contain"
                      />
                    ) : (
                      <span className="text-lg">👟</span>
                    )}
                  </div>
                  <div className="min-w-0 flex-1">
                    <h3 className="font-bold text-base leading-tight" style={{ color: '#15803d' }}>
                      {meta?.sku ?? '未登记'}
                    </h3>
                    <p className="text-xs text-slate-500 truncate mt-0.5">
                      {meta ? meta.name : `mote ${mac.slice(-6)}`}
                    </p>
                    {meta && (
                      <p className="text-xs text-slate-400 mt-0.5">{meta.color} · ¥{meta.price}</p>
                    )}
                  </div>
                  <div className="text-right flex-shrink-0">
                    <div className="text-2xl font-extrabold text-slate-800 leading-none">{count}</div>
                    <div className="text-[10px] text-slate-400 mt-0.5">events</div>
                  </div>
                </div>

                <div className="mt-3 pt-3 border-t border-slate-100 flex justify-between items-center">
                  <span className="text-[11px] font-semibold" style={{ color: '#0FAE3C' }}>PICKED UP</span>
                  <span className="text-[10px] text-slate-400">{fmtAge(ev._received_at)}</span>
                </div>
              </div>
            );
          })}
        </div>
      )}
    </section>
  );
}
