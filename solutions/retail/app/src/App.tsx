import { useEffect, useState } from 'react';
import { useWebSocket } from './hooks/useWebSocket';
import { Header } from './components/Header';
import { InteractingCards } from './components/InteractingCards';
import { EventStream } from './components/EventStream';
import { Totals } from './components/Totals';
import type { ShoeInfo } from './types';

export function App() {
  useWebSocket();
  const [shoes, setShoes] = useState<Record<string, ShoeInfo>>({});

  useEffect(() => {
    fetch('/api/shoes').then((r) => r.json()).then(setShoes).catch(() => {});
  }, []);

  return (
    <div className="min-h-screen bg-slate-50">
      <Header />
      <main className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-8 space-y-8">
        <InteractingCards shoes={shoes} />
        <div className="grid grid-cols-1 xl:grid-cols-[1fr_380px] gap-8">
          <EventStream shoes={shoes} />
          <Totals shoes={shoes} />
        </div>
      </main>
    </div>
  );
}
