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
 * a primeira conexao so funciona a partir de um toque em botao. O auto-connect
 * do bootstrap falha de proposito, e a mensagem de erro diz o que fazer. Depois
 * que o comedouro foi escolhido uma vez, reconectar nao precisa do seletor.
 *
 * Tres cuidados que este arquivo resolve:
 *  - o GATT faz uma operacao por vez: as escritas passam por uma fila;
 *  - mensagem maior que um pacote vai em pedacos (ver framing.ts);
 *  - queda de conexao: tenta reconectar sozinho, com espera crescente.
 *
 * Firmware do outro lado: teste_componentes/teste_esp/firmware_ble.
 */

import { Platform } from 'react-native';

import {
  FEEDER_NAME_PREFIX,
  FEEDER_RX_UUID,
  FEEDER_SERVICE_UUID,
  FEEDER_TX_UUID,
  Reconnector,
  WriteQueue,
} from './bleSession';
import { APP_CHUNK_BYTES, FrameAssembler, FrameTooBigError, encodeFrames } from './framing';
import { AppMessage, DeviceMessage, decodeDeviceMessage, encodeAppMessage } from './protocol';
import {
  ConnectionHandler,
  DeviceLink,
  DeviceMessageHandler,
  LinkError,
  Unsubscribe,
} from './DeviceLink';

const SERVICE_UUID = FEEDER_SERVICE_UUID;
const RX_UUID = FEEDER_RX_UUID;
const TX_UUID = FEEDER_TX_UUID;

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

function disconnectedError(): LinkError {
  return new LinkError('Sem conexão com o comedouro.', 'disconnected');
}

/**
 * Traduz os erros da API para algo que faca sentido na tela. O mais comum de
 * longe e o gesto do usuario: o bootstrap tenta conectar sozinho ao abrir o app
 * e o navegador recusa.
 */
function asLinkError(err: unknown): LinkError {
  if (err instanceof LinkError) return err;
  const nome = (err as { name?: string })?.name ?? '';
  const msg = (err as { message?: string })?.message ?? '';

  if (msg.includes('user gesture')) {
    return new LinkError('Toque em Conectar para escolher o comedouro.', 'needs_gesture');
  }
  if (nome === 'NotFoundError') {
    // Tambem aparece quando a pessoa fecha o seletor sem escolher nada.
    return new LinkError('Nenhum comedouro escolhido. Ele está ligado?', 'not_found');
  }
  if (nome === 'SecurityError') {
    return new LinkError('O navegador bloqueou o Bluetooth nesta página.', 'blocked');
  }
  if (nome === 'NetworkError') {
    return new LinkError('Falha ao conectar no comedouro. Tente de novo.', 'gatt_failed');
  }
  return new LinkError(msg || 'Falha no Bluetooth.', 'ble_error');
}

// -------------------------------------------------------------------- link

export class WebBleLink implements DeviceLink {
  readonly kind = 'ble' as const;
  readonly label = 'FELIS (Bluetooth)';

  /** O comedouro escolhido no seletor. Fica guardado para reconectar sem o seletor. */
  private device: BleDevice | null = null;
  private rx: BleCharacteristic | null = null;
  private tx: BleCharacteristic | null = null;
  private connected = false;

  private readonly assembler = new FrameAssembler();

  /** Fila de escrita: cada mensagem so comeca quando a anterior terminou. */
  private readonly queue = new WriteQueue();

  /** true depois de disconnect(): a queda foi pedida, entao nao reconecta. */
  private userDisconnected = false;
  private readonly reconnector = new Reconnector({
    attempt: () => this.openSession(),
    shouldTry: () => !this.connected && !this.userDisconnected && this.device !== null,
    onFailedAttempt: () => this.closeGattQuietly(),
    onGiveUp: () => console.warn('[ble] desisti de reconectar sozinho; toque em Conectar'),
  });

  private messageHandlers = new Set<DeviceMessageHandler>();
  private connectionHandlers = new Set<ConnectionHandler>();

  private readonly onValueChanged = (event: Event) => {
    const alvo = event.target as BleCharacteristic;
    const view = alvo?.value;
    if (!view) return;
    const pacote = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
    const texto = this.assembler.push(pacote);
    if (texto === null) return; // pedaco de uma mensagem maior
    // Ruido na linha nunca pode derrubar o app: decodeDeviceMessage devolve
    // null para qualquer coisa que nao siga o protocolo.
    const msg = decodeDeviceMessage(texto);
    if (msg) this.emitMessage(msg);
  };

  private readonly onDisconnected = () => {
    const estavaConectado = this.connected;
    this.dropSession();
    if (estavaConectado) this.emitConnection(false);
    // So reconecta sozinho uma sessao que chegou a funcionar e caiu sem ninguem pedir.
    if (estavaConectado && !this.userDisconnected) this.reconnector.schedule();
  };

  isConnected(): boolean {
    return this.connected;
  }

  async connect(): Promise<void> {
    const ble = bluetooth();
    if (!ble) {
      throw new LinkError('Este navegador não tem Bluetooth. Use Chrome ou Edge.', 'unsupported');
    }
    if (this.connected) return;

    this.userDisconnected = false;
    this.reconnector.cancel();

    // Comedouro ja escolhido nesta aba: reconecta direto, sem abrir o seletor.
    if (this.device?.gatt) {
      try {
        await this.openSession();
        return;
      } catch {
        // Nao achou o mesmo aparelho. Esquece ele, para o proximo toque ir
        // direto ao seletor em vez de repetir esta tentativa.
        this.closeGattQuietly();
        this.adoptDevice(null);
      }
    }

    try {
      // O filtro por servico faz o seletor mostrar so comedouros. O namePrefix
      // entra como segunda opcao para o caso de um firmware antigo que ainda
      // nao anuncia o UUID.
      const escolhido = await ble.requestDevice({
        filters: [{ services: [SERVICE_UUID] }, { namePrefix: FEEDER_NAME_PREFIX }],
        optionalServices: [SERVICE_UUID],
      });
      this.adoptDevice(escolhido);
      await this.openSession();
    } catch (err) {
      this.closeGattQuietly();
      throw asLinkError(err);
    }
  }

  async disconnect(): Promise<void> {
    this.userDisconnected = true;
    this.reconnector.cancel();
    const estavaConectado = this.connected;

    try {
      this.tx?.removeEventListener('characteristicvaluechanged', this.onValueChanged);
      await this.tx?.stopNotifications();
    } catch {
      // ja desconectado; nao ha o que limpar
    }
    this.dropSession();
    this.closeGattQuietly();
    // O aparelho continua guardado: o proximo Conectar volta nele sem o seletor.

    if (estavaConectado) this.emitConnection(false);
  }

  async send(msg: AppMessage): Promise<void> {
    if (!this.connected || !this.rx) throw disconnectedError();

    let pacotes: Uint8Array<ArrayBuffer>[];
    try {
      pacotes = encodeFrames(encodeAppMessage(msg), APP_CHUNK_BYTES);
    } catch (err) {
      if (err instanceof FrameTooBigError) {
        throw new LinkError('Mensagem grande demais para o comedouro.', 'too_big');
      }
      throw err;
    }

    try {
      await this.queue.run(
        pacotes,
        async (pacote) => {
          // A conexao caiu enquanto esta mensagem esperava na fila.
          if (!this.connected || !this.rx) throw disconnectedError();
          await this.rx.writeValueWithResponse(pacote as Uint8Array<ArrayBuffer>);
        },
        disconnectedError,
      );
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

  /** Troca o aparelho guardado, movendo o aviso de desconexao junto. */
  private adoptDevice(device: BleDevice | null): void {
    if (this.device === device) return;
    this.device?.removeEventListener('gattserverdisconnected', this.onDisconnected);
    this.device = device;
    this.device?.addEventListener('gattserverdisconnected', this.onDisconnected);
  }

  /** Conecta no GATT do aparelho guardado e liga as notificacoes. */
  private async openSession(): Promise<void> {
    const gatt = this.device?.gatt;
    if (!gatt) throw new LinkError('Comedouro sem GATT.', 'gatt_failed');

    const server = await gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);
    const rx = await service.getCharacteristic(RX_UUID);
    const tx = await service.getCharacteristic(TX_UUID);

    this.assembler.reset();
    // addEventListener ignora o mesmo par alvo+funcao repetido, entao reconectar
    // no mesmo objeto nao duplica as mensagens.
    tx.addEventListener('characteristicvaluechanged', this.onValueChanged);
    await tx.startNotifications();

    this.rx = rx;
    this.tx = tx;
    this.queue.reset();
    this.connected = true;
    this.emitConnection(true);
  }

  /** Esquece a sessao atual: caracteristicas, pedacos pela metade e a fila. */
  private dropSession(): void {
    this.connected = false;
    this.rx = null;
    this.tx = null;
    this.queue.reset(); // escritas que ainda estao na fila desistem com 'disconnected'
    this.assembler.reset();
  }

  private closeGattQuietly(): void {
    try {
      if (this.device?.gatt?.connected) this.device.gatt.disconnect();
    } catch {
      // ja desconectado
    }
  }

  private emitMessage(msg: DeviceMessage): void {
    this.messageHandlers.forEach((h) => h(msg));
  }

  private emitConnection(connected: boolean): void {
    this.connectionHandlers.forEach((h) => h(connected));
  }
}
