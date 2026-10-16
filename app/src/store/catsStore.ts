/** Cadastro dos dois gatos: persistencia local + envio da configuracao ao comedouro. */

import { create } from 'zustand';
import {
  Cat,
  CatId,
  FIXED_TOLERANCE_G,
  makeDefaultCat,
  validateCat,
  hasErrors,
} from '../domain/cats';
import { STORAGE_KEYS, clearAll, loadJson, saveJson } from '../data/storage';
import { simulator, useDeviceStore } from './deviceStore';
import { useEventsStore } from './eventsStore';

interface CatsStore {
  cats: Cat[];
  loaded: boolean;
  /** true quando os dois gatos ja tem nome (cadastro concluido). */
  configured: boolean;

  load: () => Promise<void>;
  saveCat: (cat: Cat) => Promise<void>;
  getCat: (id: CatId) => Cat | undefined;
  syncToDevice: () => Promise<void>;
  /**
   * Celular novo ou app reinstalado: puxa de volta o cadastro que esta
   * guardado no comedouro. Devolve true quando conseguiu recuperar.
   */
  recoverFromDevice: () => Promise<boolean>;
  /** Apaga gatos e historico e devolve o comedouro ao estado de fabrica. */
  resetAll: () => Promise<void>;
}

function isConfigured(cats: Cat[]): boolean {
  return cats.length === 2 && cats.every((c) => c.name.trim().length > 0);
}

export const useCatsStore = create<CatsStore>((set, get) => ({
  cats: [],
  loaded: false,
  configured: false,

  load: async () => {
    const saved = await loadJson<Cat[]>(STORAGE_KEYS.cats, []);
    const cats = saved.length === 2 ? saved : [makeDefaultCat(1), makeDefaultCat(2)];
    set({ cats, loaded: true, configured: isConfigured(cats) });
  },

  saveCat: async (cat) => {
    const others = get().cats.filter((c) => c.id !== cat.id);
    if (hasErrors(validateCat(cat, others))) {
      throw new Error('Cadastro inválido.');
    }
    const cats = get()
      .cats.map((c) => (c.id === cat.id ? cat : c))
      .sort((a, b) => a.id - b.id);

    set({ cats, configured: isConfigured(cats) });
    await saveJson(STORAGE_KEYS.cats, cats);

    if (useDeviceStore.getState().connection === 'connected' && isConfigured(cats)) {
      await useDeviceStore.getState().pushCats(cats);
      // copia de seguranca: nomes/emoji/cores tambem ficam no comedouro
      await useDeviceStore.getState().pushProfile(encodeProfile(cats));
    }
  },

  getCat: (id) => get().cats.find((c) => c.id === id),

  syncToDevice: async () => {
    const { cats, configured } = get();
    if (!configured) return;
    await useDeviceStore.getState().pushCats(cats);
  },

  recoverFromDevice: async () => {
    const device = useDeviceStore.getState();
    if (device.connection !== 'connected') return false;

    const dump = await device.fetchConfig();
    if (!dump || dump.cats.length === 0) return false;

    const perfis = decodeProfile(dump.profile);
    const cats: Cat[] = dump.cats.map((c) => {
      const id = c.k as CatId;
      const base = makeDefaultCat(id);
      const perfil = perfis.find((p) => p.id === id);
      return {
        ...base,
        name: perfil?.name ?? '',
        emoji: perfil?.emoji ?? base.emoji,
        color: perfil?.color ?? base.color,
        weightG: Math.round((c.minG + c.maxG) / 2),
        toleranceG: FIXED_TOLERANCE_G,
        rule: c.rule,
      };
    });

    if (!isConfigured(cats)) return false;

    set({ cats, configured: true });
    await saveJson(STORAGE_KEYS.cats, cats);
    return true;
  },

  resetAll: async () => {
    await clearAll();
    await useEventsStore.getState().clear();
    // sem isso, a proxima sincronizacao (ou a recuperacao) traria tudo de volta
    simulator()?.factoryReset();

    const cats = [makeDefaultCat(1), makeDefaultCat(2)];
    set({ cats, configured: false });

    // o comedouro tambem precisa esquecer: sem gatos e fora do modo adaptacao
    if (useDeviceStore.getState().connection === 'connected') {
      try {
        await useDeviceStore.getState().pushCats([]);
        await useDeviceStore.getState().setMode('auto');
      } catch {
        // sem conexao o app ja esta limpo; o comedouro recebe no proximo hello
      }
    }
  },
}));

/** Nome pronto para a tela: "Mimi" ou "Gato 1" quando ainda nao foi preenchido. */
export function catLabel(cat: Cat | undefined, fallbackId?: number): string {
  if (cat?.name.trim()) return cat.name.trim();
  if (cat) return `Gato ${cat.id}`;
  if (fallbackId === 0 || fallbackId === undefined) return 'Desconhecido';
  return `Gato ${fallbackId}`;
}

// ------------------------------------------- perfil guardado no comedouro

interface PerfilSalvo {
  id: CatId;
  name: string;
  emoji: string;
  color: Cat['color'];
}

/** Serializa so o que o dispositivo nao entende (e nem precisa entender). */
export function encodeProfile(cats: Cat[]): string {
  const enxuto: PerfilSalvo[] = cats.map((c) => ({
    id: c.id,
    name: c.name.trim(),
    emoji: c.emoji,
    color: c.color,
  }));
  return JSON.stringify(enxuto);
}

export function decodeProfile(blob: string | undefined): PerfilSalvo[] {
  if (!blob) return [];
  try {
    const data = JSON.parse(blob);
    if (!Array.isArray(data)) return [];
    return data.filter(
      (p): p is PerfilSalvo =>
        typeof p === 'object' &&
        p !== null &&
        (p.id === 1 || p.id === 2) &&
        typeof p.name === 'string' &&
        typeof p.emoji === 'string',
    );
  } catch {
    return [];
  }
}
