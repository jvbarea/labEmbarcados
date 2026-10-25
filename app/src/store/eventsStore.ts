/**
 * Historico de eventos.
 *
 * O comedouro guarda os eventos num buffer e o app sincroniza por numero de
 * sequencia (seq), entao nada se perde enquanto o Bluetooth fica desconectado.
 */

import { create } from 'zustand';
import { EventMsg } from '../device/protocol';
import { STORAGE_KEYS, loadJson, saveJson } from '../data/storage';
import { hourOf, startOfDay } from '../domain/format';

/** Quantos dias de historico ficam no celular. */
export const RETENTION_DAYS = 90;
const MAX_EVENTS = 2000;

export interface DaySummary {
  day: number;
  events: EventMsg[];
}

interface EventsStore {
  events: EventMsg[];
  lastSeq: number;
  loaded: boolean;

  load: () => Promise<void>;
  ingest: (event: EventMsg) => void;
  /**
   * O comedouro recomecou a numeracao (ESP regravada, ou troca do simulador
   * para o aparelho real): passa a sincronizar a partir do ultimo seq dele.
   * O historico ja guardado no celular fica como esta.
   */
  rebase: (lastSeq: number) => void;
  clear: () => Promise<void>;
}

function prune(events: EventMsg[]): EventMsg[] {
  const limite = startOfDay(Date.now() / 1000) - RETENTION_DAYS * 86400;
  return events.filter((e) => e.ts >= limite).slice(-MAX_EVENTS);
}

let persistTimer: ReturnType<typeof setTimeout> | null = null;

/** Grava em lote: chegam dezenas de eventos seguidos numa sincronizacao. */
function schedulePersist(get: () => EventsStore) {
  if (persistTimer) clearTimeout(persistTimer);
  persistTimer = setTimeout(() => {
    const state = get();
    void saveJson(STORAGE_KEYS.events, { events: state.events, lastSeq: state.lastSeq });
  }, 400);
}

export const useEventsStore = create<EventsStore>((set, get) => ({
  events: [],
  lastSeq: 0,
  loaded: false,

  load: async () => {
    const saved = await loadJson<{ events: EventMsg[]; lastSeq: number }>(STORAGE_KEYS.events, {
      events: [],
      lastSeq: 0,
    });
    set({ events: prune(saved.events), lastSeq: saved.lastSeq, loaded: true });
  },

  ingest: (event) => {
    const { events, lastSeq } = get();
    // o mesmo evento pode chegar duas vezes (notificacao + sincronizacao)
    if (events.some((e) => e.seq === event.seq && e.ts === event.ts)) return;

    const merged = prune([...events, event].sort((a, b) => a.ts - b.ts || a.seq - b.seq));
    set({ events: merged, lastSeq: Math.max(lastSeq, event.seq) });
    schedulePersist(get);
  },

  rebase: (lastSeq) => {
    set({ lastSeq: Math.max(0, lastSeq) });
    schedulePersist(get);
  },

  clear: async () => {
    set({ events: [], lastSeq: 0 });
    await saveJson(STORAGE_KEYS.events, { events: [], lastSeq: 0 });
  },
}));

// ------------------------------------------------------------------ seletores

/** Agrupa por dia, do mais recente para o mais antigo. */
export function groupByDay(events: EventMsg[]): DaySummary[] {
  const mapa = new Map<number, EventMsg[]>();
  events.forEach((e) => {
    const dia = startOfDay(e.ts);
    const lista = mapa.get(dia);
    if (lista) lista.push(e);
    else mapa.set(dia, [e]);
  });
  return Array.from(mapa.entries())
    .map(([day, list]) => ({ day, events: list.sort((a, b) => b.ts - a.ts) }))
    .sort((a, b) => b.day - a.day);
}

export interface CatDayStats {
  cat: number;
  visits: number;
  opened: number;
  denied: number;
}

/** Resumo do dia por gato (usado na tela Inicio). */
export function summarizeDay(events: EventMsg[], day: number): CatDayStats[] {
  const doDia = events.filter((e) => startOfDay(e.ts) === day && e.kind === 'visit');
  const mapa = new Map<number, CatDayStats>();
  doDia.forEach((e) => {
    const cat = e.cat ?? 0;
    const atual = mapa.get(cat) ?? { cat, visits: 0, opened: 0, denied: 0 };
    atual.visits += 1;
    if (e.door === 'opened') atual.opened += 1;
    else atual.denied += 1;
    mapa.set(cat, atual);
  });
  return Array.from(mapa.values()).sort((a, b) => a.cat - b.cat);
}

/** Medicoes coletadas no modo adaptacao, prontas para a identificacao. */
export function learningSamples(events: EventMsg[]): { wG: number; ts: number }[] {
  return events
    .filter((e) => e.kind === 'visit' && e.learn === 1 && typeof e.wG === 'number')
    .map((e) => ({ wG: e.wG as number, ts: e.ts }));
}

/** Visitas por hora (0-23) para o grafico de barras. */
export function visitsByHour(events: EventMsg[], day: number): number[] {
  const horas = new Array(24).fill(0);
  events
    .filter((e) => e.kind === 'visit' && startOfDay(e.ts) === day)
    .forEach((e) => {
      horas[hourOf(e.ts)] += 1;
    });
  return horas;
}
