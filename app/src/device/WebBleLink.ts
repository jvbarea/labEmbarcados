/**
 * Transporte BLE pelo navegador (Web Bluetooth), para rodar o app em
 * `npm run web` e falar com a ESP32 de verdade, sem development build.
 *
 * Funciona em Chrome e Edge (desktop e Android). Firefox e Safari nao
 * implementam Web Bluetooth, e no iOS nao ha caminho. Para o APK final entra um
 * BleLink nativo com react-native-ble-plx, implementando esta mesma interface —
 * as telas nao mudam nos dois casos.
 *
 * A API exige um GESTO DO USUARIO para abrir o seletor de dispositivos, entao
 * connect() so funciona a partir de um toque em botao. O auto-connect do
 * bootstrap falha de proposito, e a mensagem de erro diz o que fazer.
 *
 * Firmware do outro lado: teste_componentes/teste_esp/firmware_ble.
 */

import { Platform } from 'react-native';

import { AppMessage, DeviceMessage, decodeDeviceMessage, encodeAppMessage } from './protocol';
import {
  ConnectionHandler,
  DeviceLink,
  DeviceMessageHandler,
  LinkError,
  Unsubscribe,
} from './DeviceLink';

/** Nordic UART Service — os mesmos UUIDs do firmware. */
const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // app -> ESP (write)
const TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // ESP -> app (notify)

const DEVICE_NAME_PREFIX = 'Aliment';

/**
 * Acima disto a escrita depende de "long write", que nem toda pilha BLE aceita.
 * O firmware avisa no log quando uma mensagem passa do MTU; aqui avisamos no
 * console do navegador, para a causa aparecer nos dois lados.
 */
const SAFE_WRITE_BYTES = 180;

// --------------------------------------------------------------------- tipos
// Declaracoes minimas da Web Bluetooth API. Evitam depender de
// @types/web-bluetooth so por causa deste arquivo.

interface BleCharacteristic extends EventTarget {
  value?: DataView;
  startNotifications(): Promise<BleCharacteristic>;
  stopNotifications(): Promise<BleCharacteristic>;
  writeValueWithResponse(value: BufferSource): Promise<void>;
}

interface BleService {
  getCharacteristic(uuid: string): Promise<BleCharacteristic>;
}

interface BleServer {
  connected: boolean;
  connect(): Promise<BleServer>;
  disconnect(): void;
  getPrimaryService(uuid: string): Promise<BleService>;
}

interface BleDevice extends EventTarget {
  name?: string;
  gatt?: BleServer;
}

interface BleRequestOptions {
  filters?: { services?: string[]; namePrefix?: string }[];
  optionalServices?: string[];
}

interface WebBluetooth {
  requestDevice(options: BleRequestOptions): Promise<BleDevice>;
}

function bluetooth(): WebBluetooth | null {
  if (Platform.OS !== 'web') return null;
  if (typeof navigator === 'undefined') return null;
  return (navigator as unknown as { bluetooth?: WebBluetooth }).bluetooth ?? null;
}

/** true quando o navegador atual tem Web Bluetooth. */
export function webBleAvailable(): boolean {
  return bluetooth() !== null;
}

// ------------------------------------------------------------------- erros

/**
 * Traduz os erros da API para algo que faca sentido na tela. O mais comum de
 * longe e o gesto do usuario: o bootstrap tenta conectar sozinho ao abrir o app
 * e o navegador recusa.
 */
function asLinkError(err: unknown): LinkError {
  const nome = (err as { name?: string })?.name ?? '';
  const msg = (err as { message?: string })?.message ?? '';

  if (msg.includes('user gesture')) {
    return new LinkError('Toque em Conectar para escolher o comedouro.', 'needs_gesture');
  }
  if (nome === 'NotFoundError') {
    // Tambem aparece quando a pessoa fecha o seletor sem escolher nada.
    return new LinkError('Nenhum comedouro escolhido. Ele esta ligado?', 'not_found');
  }
  if (nome === 'SecurityError') {
    return new LinkError('O navegador bloqueou o Bluetooth nesta pagina.', 'blocked');
  }
  if (nome === 'NetworkError') {
    return new LinkError('Falha ao conectar no comedouro. Tente de novo.', 'gatt_failed');
  }
  return new LinkError(msg || 'Falha no Bluetooth.', 'ble_error');
}

// -------------------------------------------------------------------- link

export class WebBleLink implements DeviceLink {
  readonly kind = 'ble' as const;
  readonly label = 'Feed Cat (Bluetooth)';

  private device: BleDevice | null = null;
  private rx: BleCharacteristic | null = null;
  private tx: BleCharacteristic | null = null;
  private connected = false;

  private messageHandlers = new Set<DeviceMessageHandler>();
  private connectionHandlers = new Set<ConnectionHandler>();

  private readonly onValueChanged = (event: Event) => {
    const alvo = event.target as BleCharacteristic;
    if (!alvo?.value) return;
    const texto = new TextDecoder().decode(alvo.value);
    // Ruido na linha nunca pode derrubar o app: decodeDeviceMessage devolve
    // null para qualquer coisa que nao siga o protocolo.
    const msg = decodeDeviceMessage(texto);
    if (msg) this.emitMessage(msg);
  };

  private readonly onDisconnected = () => {
    this.connected = false;
    this.rx = null;
    this.tx = null;
    this.emitConnection(false);
  };

  isConnected(): boolean {
    return this.connected;
  }

  async connect(): Promise<void> {
    const ble = bluetooth();
    if (!ble) {
      throw new LinkError(
        'Este navegador nao tem Bluetooth. Use Chrome ou Edge.',
        'unsupported',
      );
    }
    if (this.connected) return;

    try {
      // O filtro por servico faz o seletor mostrar so comedouros. O namePrefix
      // entra como segunda opcao para o caso de um firmware antigo que ainda
      // nao anuncia o UUID.
      this.device = await ble.requestDevice({
        filters: [{ services: [SERVICE_UUID] }, { namePrefix: DEVICE_NAME_PREFIX }],
        optionalServices: [SERVICE_UUID],
      });

      this.device.addEventListener('gattserverdisconnected', this.onDisconnected);

      const server = await this.device.gatt?.connect();
      if (!server) throw new LinkError('Comedouro sem GATT.', 'gatt_failed');

      const service = await server.getPrimaryService(SERVICE_UUID);
      this.rx = await service.getCharacteristic(RX_UUID);
      this.tx = await service.getCharacteristic(TX_UUID);

      await this.tx.startNotifications();
      this.tx.addEventListener('characteristicvaluechanged', this.onValueChanged);

      this.connected = true;
      this.emitConnection(true);
    } catch (err) {
      await this.cleanup();
      throw asLinkError(err);
    }
  }

  async disconnect(): Promise<void> {
    await this.cleanup();
    if (this.connected) {
      this.connected = false;
      this.emitConnection(false);
    }
  }

  async send(msg: AppMessage): Promise<void> {
    if (!this.connected || !this.rx) {
      throw new LinkError('Sem conexao com o comedouro.', 'disconnected');
    }

    const bytes = new TextEncoder().encode(encodeAppMessage(msg));
    if (bytes.length > SAFE_WRITE_BYTES) {
      console.warn(
        `[ble] mensagem de ${bytes.length} bytes passa do MTU seguro (${SAFE_WRITE_BYTES}); ` +
          'pode falhar ou chegar truncada',
      );
    }

    try {
      await this.rx.writeValueWithResponse(bytes);
    } catch (err) {
      throw asLinkError(err);
    }
  }

  onMessage(handler: DeviceMessageHandler): Unsubscribe {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  onConnectionChange(handler: ConnectionHandler): Unsubscribe {
    this.connectionHandlers.add(handler);
    return () => this.connectionHandlers.delete(handler);
  }

  // ------------------------------------------------------------- internos

  private async cleanup(): Promise<void> {
    try {
      this.tx?.removeEventListener('characteristicvaluechanged', this.onValueChanged);
      await this.tx?.stopNotifications();
    } catch {
      // ja desconectado; nao ha o que limpar
    }
    this.device?.removeEventListener('gattserverdisconnected', this.onDisconnected);
    try {
      if (this.device?.gatt?.connected) this.device.gatt.disconnect();
    } catch {
      // idem
    }
    this.rx = null;
    this.tx = null;
    this.device = null;
  }

  private emitMessage(msg: DeviceMessage): void {
    this.messageHandlers.forEach((h) => h(msg));
  }

  private emitConnection(connected: boolean): void {
    this.connectionHandlers.forEach((h) => h(connected));
  }
}
