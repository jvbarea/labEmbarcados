/**
 * Escolhe o transporte na hora que o app carrega.
 *
 * O padrao continua sendo o SIMULADOR: e dele que dependem a demonstracao e os
 * testes, e o navegador tem Web Bluetooth mesmo quando nao ha comedouro por
 * perto. O BLE e opt-in explicito:
 *
 *   http://localhost:8081/?ble=1     liga o Bluetooth (e lembra da escolha)
 *   http://localhost:8081/?ble=0     volta para o simulador
 *
 * A escolha fica no localStorage, entao vale para as recargas seguintes sem
 * precisar do parametro na URL.
 */

import { Platform } from 'react-native';

import { DeviceLink } from './DeviceLink';
import { MockLink } from './MockLink';
import { WebBleLink, webBleAvailable } from './WebBleLink';

const STORAGE_KEY = 'feedcat.link';

/** Le a preferencia da URL (que tem prioridade) e depois do localStorage. */
export function bleRequested(): boolean {
  if (Platform.OS !== 'web') return false;
  if (typeof window === 'undefined') return false;

  try {
    const busca = window.location?.search ?? '';
    if (busca.includes('ble=1')) {
      window.localStorage?.setItem(STORAGE_KEY, 'ble');
      return true;
    }
    if (busca.includes('ble=0')) {
      window.localStorage?.removeItem(STORAGE_KEY);
      return false;
    }
    return window.localStorage?.getItem(STORAGE_KEY) === 'ble';
  } catch {
    // Modo anonimo ou storage bloqueado: cai no simulador, que sempre funciona.
    return false;
  }
}

export function createLink(): DeviceLink {
  if (bleRequested()) {
    if (webBleAvailable()) return new WebBleLink();
    console.warn('[link] ?ble=1 pedido, mas este navegador nao tem Web Bluetooth; usando o simulador');
  }
  return new MockLink();
}
