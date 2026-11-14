/**
 * Persistencia local (AsyncStorage no celular, localStorage no navegador).
 *
 * Fica atras desta camada fina para que a troca por SQLite, quando o historico
 * crescer, nao toque nas telas.
 */

import AsyncStorage from '@react-native-async-storage/async-storage';

export const STORAGE_KEYS = {
  cats: 'feedcat.cats.v1',
  /** Historico do comedouro real. */
  events: 'feedcat.events.v1',
  /** Historico do simulador: separado, para os dados de exemplo nao se misturarem. */
  eventsSim: 'feedcat.events.sim.v1',
  settings: 'feedcat.settings.v1',
  /** Comedouro escolhido no Bluetooth nativo (id e nome), para reconectar direto. */
  bleDevice: 'feedcat.bleDevice.v1',
  /** Preferencia: simulador ou comedouro real. Nao e apagada com os dados. */
  link: 'feedcat.link.v1',
} as const;

/** O que "Apagar dados do app" apaga: tudo menos a escolha simulador x real. */
const DATA_KEYS = [
  STORAGE_KEYS.cats,
  STORAGE_KEYS.events,
  STORAGE_KEYS.eventsSim,
  STORAGE_KEYS.settings,
  STORAGE_KEYS.bleDevice,
];

export async function loadJson<T>(key: string, fallback: T): Promise<T> {
  try {
    const raw = await AsyncStorage.getItem(key);
    if (!raw) return fallback;
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

export async function saveJson(key: string, value: unknown): Promise<void> {
  try {
    await AsyncStorage.setItem(key, JSON.stringify(value));
  } catch {
    // sem persistencia (aba anonima, armazenamento cheio): o app continua funcionando
  }
}

/** Apaga tudo que o app guardou no aparelho (gatos, historicos, ajustes e o comedouro salvo). */
export async function clearAll(): Promise<void> {
  await Promise.all(DATA_KEYS.map((key) => removeKey(key)));
}

export async function removeKey(key: string): Promise<void> {
  try {
    await AsyncStorage.removeItem(key);
  } catch {
    // ignorado de proposito
  }
}
