import { useEffect } from 'react';
import { useWebSocket } from './hooks/useWebSocket';
import { useStore } from './store';
import { Header } from './components/Header';
import { InteractingCards } from './components/InteractingCards';
import { EventStream } from './components/EventStream';
import { Totals } from './components/Totals';

export function App() {
  useWebSocket();
  const tickNow = useStore((s) => s.tickNow);

  useEffect(() => {
    const id = setInterval(tickNow, 1000);
    return () => clearInterval(id);
  }, [tickNow]);

  return (
    <div className="min-h-screen bg-slate-50">
      <Header />
      <main className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-8 space-y-8">
        <InteractingCards />
        <div className="grid grid-cols-1 xl:grid-cols-[1fr_380px] gap-8">
          <EventStream />
          <Totals />
        </div>
      </main>
    </div>
  );
}
