import { useStore } from '../store';
import type { ShoeInfo } from '../types';

function fmtAge(ts: number): string {
  const delta = Math.max(0, Math.floor(Date.now() / 1000 - ts));
  if (delta < 60) return `${delta}s 前`;
  if (delta < 3600) return `${Math.floor(delta / 60)}m 前`;
  return `${Math.floor(delta / 3600)}h 前`;
}

interface Props {
  shoes: Record<string, ShoeInfo>;
}

export function Totals({ shoes }: Props) {
  const events = useStore((s) => s.events);

  const counts = new Map<string, number>();
  const latest = new Map<string, number>();
  for (const ev of events) {
    counts.set(ev.mote_mac, (counts.get(ev.mote_mac) ?? 0) + 1);
    latest.set(ev.mote_mac, Math.max(latest.get(ev.mote_mac) ?? 0, ev._received_at));
  }

  const rows = [...counts.entries()]
    .sort((a, b) => b[1] - a[1])
    .map(([mac, n]) => ({ mac, n, meta: shoes[mac], lat: latest.get(mac) ?? 0 }));

  return (
    <section className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
      <div className="p-5 border-b border-slate-100 bg-slate-50/50">
        <h2 className="font-bold text-slate-800">累计互动</h2>
        <p className="text-xs text-slate-500 mt-0.5">本次会话内，按事件数排序</p>
      </div>

      {rows.length === 0 ? (
        <div className="text-center py-12 text-slate-400 text-sm">暂无累计数据</div>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-sm text-left">
            <thead className="text-xs text-slate-500 uppercase bg-slate-50 border-b border-slate-100">
              <tr>
                <th className="px-5 py-3 font-semibold">SKU</th>
                <th className="px-5 py-3 font-semibold">名称</th>
                <th className="px-5 py-3 font-semibold text-right">事件数</th>
                <th className="px-5 py-3 font-semibold">最近活动</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {rows.map(({ mac, n, meta, lat }, idx) => (
                <tr key={mac} className="hover:bg-slate-50/80 transition-colors group">
                  <td className="px-5 py-3 font-semibold text-slate-900">
                    {meta?.sku ?? '未登记'}
                  </td>
                  <td className="px-5 py-3 text-slate-600">{meta?.name ?? mac}</td>
                  <td className="px-5 py-3 text-right">
                    <span
                      className="font-bold text-base"
                      style={{ color: idx === 0 ? '#0FAE3C' : '#334155' }}
                    >
                      {n}
                    </span>
                  </td>
                  <td className="px-5 py-3 text-slate-400 text-xs">{fmtAge(lat)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}
