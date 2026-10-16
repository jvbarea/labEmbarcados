/**
 * Persistencia local (AsyncStorage no celular, localStorage no navegador).
 *
 * Fica atras desta camada fina para que a troca por SQLite, quando o historico
 * crescer, nao toque nas telas.
 */

import AsyncStorage from '@react-native-async-storage/async-storage';

export const STORAGE_KEYS = {
  cats: 'feedcat.cats.v1',
  events: 'feedcat.events.v1',
  settings: 'feedcat.settings.v1',
} as const;

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

/** Apaga tudo que o app guardou no aparelho (gatos, historico e ajustes). */
export async function clearAll(): Promise<void> {
  await Promise.all(Object.values(STORAGE_KEYS).map((key) => removeKey(key)));
}

export async function removeKey(key: string): Promise<void> {
  try {
    await AsyncStorage.removeItem(key);
  } catch {
    // ignorado de proposito
  }
}
