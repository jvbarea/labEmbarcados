/**
 * Qual transporte usar: o simulador ou o comedouro real.
 *
 * O usuario escolhe na aba Comedouro ("Comedouro real" / "Simulador") e a
 * escolha fica salva. Por plataforma:
 *
 *   Android/iOS (APK)  real = NativeBleLink (react-native-ble-plx)
 *   navegador          real = WebBleLink (Web Bluetooth, Chrome/Edge)
 *   qualquer um        simulador = MockLink
 *
 * Sem escolha salva, o celular comeca no comedouro real e o navegador no
 * simulador. Na web a URL ainda vale e tem prioridade:
 *
 *   http://localhost:8081/?ble=1     comedouro real
 *   http://localhost:8081/?ble=0     simulador
 */

import { Platform } from 'react-native';

import { DeviceLink } from './DeviceLink';
import { MockLink } from './MockLink';
import { NativeBleLink } from './NativeBleLink';
import { WebBleLink, webBleAvailable } from './WebBleLink';

export type LinkKind = DeviceLink['kind'];

/** Chave antiga (so web), de antes da escolha existir no app. */
const LEGACY_WEB_KEY = 'feedcat.link';

/** Na web, `?ble=1` / `?ble=0` na URL. null quando nao veio nada. */
export function urlLinkOverride(): LinkKind | null {
  if (Platform.OS !== 'web' || typeof window === 'undefined') return null;
  try {
    const busca = window.location?.search ?? '';
    if (busca.includes('ble=1')) return 'ble';
    if (busca.includes('ble=0')) return 'mock';
  } catch {
    // sem window.location (testes): segue o padrao
  }
  return null;
}

/** Escolha de quem ainda nao escolheu nada no app. */
export function defaultLinkKind(): LinkKind {
  if (Platform.OS !== 'web') return 'ble';
  try {
    if (typeof window !== 'undefined' && window.localStorage?.getItem(LEGACY_WEB_KEY) === 'ble') {
      return 'ble';
    }
  } catch {
    // modo anonimo ou storage bloqueado
  }
  return 'mock';
}

/** O comedouro real existe nesta plataforma? (Firefox e Safari nao tem Web Bluetooth.) */
export function realLinkAvailable(): boolean {
  return Platform.OS !== 'web' || webBleAvailable();
}

export function createLink(kind: LinkKind): DeviceLink {
  if (kind === 'ble') {
    if (Platform.OS !== 'web') return new NativeBleLink();
    if (webBleAvailable()) return new WebBleLink();
    console.warn('[link] este navegador nao tem Web Bluetooth; usando o simulador');
  }
  return new MockLink();
}
