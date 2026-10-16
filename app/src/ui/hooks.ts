/** Hooks de apoio das telas. */

import { useCallback, useEffect, useState } from 'react';
import { nowS } from '../domain/format';

export interface Feedback {
  tone: 'success' | 'danger';
  text: string;
}

/**
 * Executa acoes assincronas mostrando carregando e erro na tela.
 * Nada de acao silenciosa: todo erro vira mensagem visivel.
 */
export function useAction() {
  const [pending, setPending] = useState<string | null>(null);
  const [feedback, setFeedback] = useState<Feedback | null>(null);

  const run = useCallback(async (key: string, fn: () => Promise<void>, okText?: string) => {
    setPending(key);
    setFeedback(null);
    try {
      await fn();
      if (okText) setFeedback({ tone: 'success', text: okText });
    } catch (err) {
      setFeedback({
        tone: 'danger',
        text: err instanceof Error ? err.message : 'Não foi possível completar a ação.',
      });
    } finally {
      setPending(null);
    }
  }, []);

  const clear = useCallback(() => setFeedback(null), []);

  return { pending, feedback, run, clear, isPending: (key: string) => pending === key };
}

/** Relogio que atualiza a tela (para contagens regressivas). */
export function useNow(intervalMs = 1000): number {
  const [now, setNow] = useState(nowS);
  useEffect(() => {
    const timer = setInterval(() => setNow(nowS()), intervalMs);
    return () => clearInterval(timer);
  }, [intervalMs]);
  return now;
}
