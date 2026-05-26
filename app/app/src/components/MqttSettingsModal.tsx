import { useEffect, useRef, useState } from 'react';

interface Config {
  broker: string;
  port: number;
  user: string;
}

interface Props {
  onClose: () => void;
}

export function MqttSettingsModal({ onClose }: Props) {
  const [broker, setBroker] = useState('');
  const [port, setPort] = useState('1883');
  const [user, setUser] = useState('');
  const [password, setPassword] = useState('');
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [saved, setSaved] = useState(false);
  const overlayRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    fetch('/api/config')
      .then((r) => r.json())
      .then((cfg: Config) => {
        setBroker(cfg.broker ?? '');
        setPort(String(cfg.port ?? 1883));
        setUser(cfg.user ?? '');
      })
      .catch(() => setError('Failed to load config'))
      .finally(() => setLoading(false));
  }, []);

  function handleOverlayClick(e: React.MouseEvent) {
    if (e.target === overlayRef.current) onClose();
  }

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setSaving(true);
    setError(null);
    setSaved(false);
    try {
      const res = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          broker,
          port: Number(port),
          user: user || null,
          password: password || null,
        }),
      });
      if (!res.ok) {
        const body = await res.json().catch(() => ({}));
        throw new Error(body.detail ?? `HTTP ${res.status}`);
      }
      setSaved(true);
      setPassword('');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Unknown error');
    } finally {
      setSaving(false);
    }
  }

  return (
    <div
      ref={overlayRef}
      onClick={handleOverlayClick}
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/40"
    >
      <div className="bg-white rounded-xl shadow-xl w-full max-w-sm mx-4 p-6">
        <div className="flex items-center justify-between mb-5">
          <h2 className="text-base font-bold text-slate-900">MQTT Connection</h2>
          <button onClick={onClose} className="text-slate-400 hover:text-slate-600 text-xl leading-none">×</button>
        </div>

        {loading ? (
          <p className="text-sm text-slate-400 py-4 text-center">Loading…</p>
        ) : (
          <form onSubmit={handleSubmit} className="space-y-4">
            <Field label="Broker">
              <input
                className={inputCls}
                value={broker}
                onChange={(e) => setBroker(e.target.value)}
                placeholder="192.168.1.100"
                required
              />
            </Field>
            <Field label="Port">
              <input
                className={inputCls}
                type="number"
                min={1}
                max={65535}
                value={port}
                onChange={(e) => setPort(e.target.value)}
                required
              />
            </Field>
            <Field label="Username">
              <input
                className={inputCls}
                value={user}
                onChange={(e) => setUser(e.target.value)}
                placeholder="(optional)"
                autoComplete="username"
              />
            </Field>
            <Field label="Password">
              <input
                className={inputCls}
                type="password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                placeholder="leave blank to keep existing"
                autoComplete="current-password"
              />
            </Field>

            {error && <p className="text-xs text-red-600">{error}</p>}
            {saved && <p className="text-xs text-green-600">Reconnecting with new settings…</p>}

            <div className="flex gap-3 pt-1">
              <button
                type="button"
                onClick={onClose}
                className="flex-1 py-2 rounded-lg border border-slate-200 text-sm text-slate-600 hover:bg-slate-50"
              >
                Cancel
              </button>
              <button
                type="submit"
                disabled={saving}
                className="flex-1 py-2 rounded-lg bg-green-600 text-white text-sm font-semibold hover:bg-green-700 disabled:opacity-50"
              >
                {saving ? 'Saving…' : 'Apply'}
              </button>
            </div>
          </form>
        )}
      </div>
    </div>
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <label className="block">
      <span className="text-xs font-semibold text-slate-500 uppercase tracking-wider">{label}</span>
      <div className="mt-1">{children}</div>
    </label>
  );
}

const inputCls =
  'w-full rounded-lg border border-slate-200 px-3 py-2 text-sm text-slate-800 focus:outline-none focus:ring-2 focus:ring-green-500';
