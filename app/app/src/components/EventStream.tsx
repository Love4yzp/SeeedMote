import { useStore } from '../store';

const EVENT_TAIL = 50;

export function EventStream() {
  const events = useStore((s) => s.events.slice(0, EVENT_TAIL));

  return (
    <section className="bg-white rounded-xl shadow-sm border border-slate-200 overflow-hidden">
      <div className="p-5 border-b border-slate-100 bg-slate-50/50 flex justify-between items-center">
        <div>
          <h2 className="font-bold text-slate-800">互动记录</h2>
          <p className="text-xs text-slate-500 mt-0.5">最近 {EVENT_TAIL} 次商品互动</p>
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
                <th className="px-5 py-3 font-semibold">互动</th>
                <th className="px-5 py-3 font-semibold">商品</th>
                <th className="px-5 py-3 font-semibold">状态</th>
                <th className="px-5 py-3 font-semibold">诊断</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {events.map((ev, i) => {
                const ts = new Date(ev._received_at * 1000).toLocaleTimeString('zh-CN', { hour12: false });
                return (
                  <tr
                    key={`${ev.source.mote_mac}-${ev.source.packet_id}-${i}`}
                    className="hover:bg-slate-50/80 transition-colors"
                  >
                    <td className="px-5 py-3 text-slate-400 font-mono text-xs">{ts}</td>
                    <td className="px-5 py-3 font-semibold text-slate-900">{ev.action_label}</td>
                    <td className="px-5 py-3">
                      <div className="font-semibold text-slate-900">{ev.item_label}</div>
                      {ev.item && (
                        <div className="text-xs text-slate-400 mt-0.5">
                          {ev.item.color} · ¥{ev.item.price}
                        </div>
                      )}
                    </td>
                    <td className="px-5 py-3">
                      <span className={`inline-flex items-center rounded-full px-2 py-0.5 text-xs font-semibold border ${
                        ev.registered
                          ? 'bg-green-50 text-green-700 border-green-100'
                          : 'bg-amber-50 text-amber-700 border-amber-100'
                      }`}>
                        {ev.registered ? '已登记商品' : '未登记设备'}
                      </span>
                    </td>
                    <td className="px-5 py-3 text-xs text-slate-500">
                      <details>
                        <summary className="cursor-pointer font-semibold text-slate-600 hover:text-slate-900">
                          查看
                        </summary>
                        <dl className="mt-2 grid grid-cols-[72px_1fr] gap-x-3 gap-y-1 font-mono text-[11px]">
                          <dt className="text-slate-400">mote_mac</dt>
                          <dd className="text-slate-600">{ev.source.mote_mac}</dd>
                          <dt className="text-slate-400">packet_id</dt>
                          <dd className="text-slate-600">{ev.source.packet_id}</dd>
                          <dt className="text-slate-400">rssi</dt>
                          <dd className="text-slate-600">{ev.source.rssi}</dd>
                          <dt className="text-slate-400">gateway</dt>
                          <dd className="text-slate-600">{ev.source.gw_label}</dd>
                          {ev.source.gw_alias && (
                            <>
                              <dt className="text-slate-400">gw_id</dt>
                              <dd className="text-slate-600">{ev.source.gw_id}</dd>
                            </>
                          )}
                        </dl>
                      </details>
                    </td>
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
