/**
 * Transporte BLE nativo, para o APK (react-native-ble-plx).
 *
 * Mesma interface e mesmas regras do WebBleLink (Bluetooth pelo navegador), com
 * o que o celular permite a mais:
 *  - conecta sozinho ao abrir o app, sem seletor e sem toque;
 *  - lembra o comedouro escolhido (AsyncStorage) e da preferencia a ele;
 *  - negocia o MTU e escreve pacotes do tamanho certo para ele.
 *
 * O resto e comum aos dois transportes: mensagens em pedacos "#i/n:"
 * (framing.ts), fila de escrita e reconexao com espera crescente (bleSession.ts).
 *
 * Nao funciona no Expo Go: precisa do APK gerado por scripts/build-apk.ps1.
 * Firmware do outro lado: teste_componentes/teste_esp/firmware_ble.
 */

import { PermissionsAndroid, Platform } from 'react-native';
import { BleError, BleErrorCode, BleManager, Device, State, Subscription } from 'react-native-ble-plx';

import { base64ToBytes, bytesToBase64 } from './base64';
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
import { STORAGE_KEYS, loadJson, removeKey, saveJson } from '../data/storage';

/** Quanto tempo procurar o comedouro antes de desistir. */
export const SCAN_TIMEOUT_MS = 10000;
/** Tempo maximo de uma tentativa de conexao (o Android sozinho espera ~30 s). */
const CONNECT_TIMEOUT_MS = 10000;
/** O mesmo MTU que o firmware pede (BLE_MTU_REQUEST). */
const MTU_REQUEST = 185;
/** Menor pacote do BLE (MTU 23 - 3 bytes de cabecalho). */
const MIN_CHUNK = 20;
/** Quanto esperar o Bluetooth sair de "Unknown"/"Resetting" logo depois de abrir o app. */
const STATE_WAIT_MS = 3000;

interface SavedDevice {
  id: string;
  name: string | null;
}

function disconnectedError(): LinkError {
  return new LinkError('Sem conexão com o comedouro.', 'disconnected');
}

/** Traduz os erros do ble-plx e do Android para a tela. */
export function asLinkError(err: unknown): LinkError {
  if (err instanceof LinkError) return err;
  if (err instanceof BleError) {
    switch (err.errorCode) {
      case BleErrorCode.BluetoothPoweredOff:
        return new LinkError('Ligue o Bluetooth do celular.', 'bluetooth_off');
      case BleErrorCode.BluetoothUnauthorized:
        return new LinkError(
          'O FELIS precisa da permissão de Bluetooth (Dispositivos próximos).',
          'permission_denied',
        );
      case BleErrorCode.BluetoothUnsupported:
        return new LinkError('Este celular não tem Bluetooth de baixo consumo (BLE).', 'unsupported');
      case BleErrorCode.DeviceConnectionFailed:
      case BleErrorCode.OperationTimedOut:
      case BleErrorCode.DeviceNotFound:
        return new LinkError('Falha ao conectar no comedouro. Ele está ligado e perto?', 'gatt_failed');
      case BleErrorCode.DeviceDisconnected:
      case BleErrorCode.OperationCancelled:
        return disconnectedError();
      default:
        return new LinkError(err.reason || err.message || 'Falha no Bluetooth.', 'ble_error');
    }
  }
  const msg = (err as { message?: string })?.message ?? '';
  return new LinkError(msg || 'Falha no Bluetooth.', 'ble_error');
}

function isFeeder(device: Device): boolean {
  const uuids = (device.serviceUUIDs ?? []).map((u) => u.toLowerCase());
  if (uuids.includes(FEEDER_SERVICE_UUID)) return true;
  const nome = device.name ?? device.localName ?? '';
  return nome.startsWith(FEEDER_NAME_PREFIX);
}

export class NativeBleLink implements DeviceLink {
  readonly kind = 'ble' as const;
  readonly label = 'FELIS (Bluetooth do celular)';

  /** Criado so na primeira conexao: no web e nos testes o modulo nativo nao existe. */
  private manager: BleManager | null = null;

  private deviceId: string | null = null;
  /** Aparelho da tentativa de conexao em curso (para desfazer se ela falhar no meio). */
  private attemptId: string | null = null;
  private connected = false;
  /** Tamanho de cada pacote escrito, conforme o MTU negociado. */
  private chunk = MIN_CHUNK;

  private readonly assembler = new FrameAssembler();
  private readonly queue = new WriteQueue();
  private monitorSub: Subscription | null = null;
  private disconnectSub: Subscription | null = null;

  /** undefined = ainda nao leu do armazenamento. */
  private saved: SavedDevice | null | undefined = undefined;

  /** true depois de disconnect(): a queda foi pedida, entao nao reconecta. */
  private userDisconnected = false;
  private readonly reconnector = new Reconnector({
    attempt: () => this.openSession(this.saved?.id ?? ''),
    shouldTry: () => !this.connected && !this.userDisconnected && !!this.saved?.id,
    onFailedAttempt: () => this.closeQuietly(),
    onGiveUp: () => console.warn('[ble] desisti de reconectar sozinho; toque em Conectar'),
  });

  private messageHandlers = new Set<DeviceMessageHandler>();
  private connectionHandlers = new Set<ConnectionHandler>();

  isConnected(): boolean {
    return this.connected;
  }

  /** MTU em uso (para a tela de diagnostico). */
  get packetSize(): number {
    return this.chunk;
  }

  async connect(): Promise<void> {
    if (this.connected) return;
    this.userDisconnected = false;
    this.reconnector.cancel();

    try {
      await this.ensureReady();
      const saved = await this.loadSaved();
      const escolhido = await this.scan(saved?.id ?? null);
      await this.openSession(escolhido.id);
      if (saved?.id !== escolhido.id) {
        this.saved = { id: escolhido.id, name: escolhido.name ?? escolhido.localName };
        await saveJson(STORAGE_KEYS.bleDevice, this.saved);
      }
    } catch (err) {
      this.closeQuietly();
      throw asLinkError(err);
    }
  }

  async disconnect(): Promise<void> {
    this.userDisconnected = true;
    this.reconnector.cancel();
    const estavaConectado = this.connected;
    const id = this.deviceId;
    this.dropSession();
    if (id && this.manager) {
      try {
        await this.manager.cancelDeviceConnection(id);
      } catch {
        // ja desconectado
      }
    }
    if (estavaConectado) this.emitConnection(false);
  }

  async forget(): Promise<void> {
    await this.disconnect();
    this.saved = null;
    await removeKey(STORAGE_KEYS.bleDevice);
  }

  async send(msg: AppMessage): Promise<void> {
    if (!this.connected || !this.deviceId) throw disconnectedError();

    let pacotes: Uint8Array[];
    try {
      pacotes = encodeFrames(encodeAppMessage(msg), this.chunk);
    } catch (err) {
      if (err instanceof FrameTooBigError) {
        throw new LinkError('Mensagem grande demais para o comedouro.', 'too_big');
      }
      throw err;
    }

    const id = this.deviceId;
    try {
      await this.queue.run(
        pacotes,
        async (pacote) => {
          if (!this.connected || this.deviceId !== id) throw disconnectedError();
          await this.ble().writeCharacteristicWithResponseForDevice(
            id,
            FEEDER_SERVICE_UUID,
            FEEDER_RX_UUID,
            bytesToBase64(pacote),
          );
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

  private ble(): BleManager {
    if (!this.manager) this.manager = new BleManager();
    return this.manager;
  }

  private async loadSaved(): Promise<SavedDevice | null> {
    if (this.saved === undefined) {
      this.saved = await loadJson<SavedDevice | null>(STORAGE_KEYS.bleDevice, null);
    }
    return this.saved;
  }

  /** Permissoes do Android e Bluetooth ligado. */
  private async ensureReady(): Promise<void> {
    if (Platform.OS === 'android') {
      const versao = typeof Platform.Version === 'number' ? Platform.Version : Number(Platform.Version);
      const pedidas =
        versao >= 31
          ? [
              PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
              PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
            ]
          : [PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION];
      const resultado = await PermissionsAndroid.requestMultiple(pedidas);
      const negada = pedidas.some((p) => resultado[p] !== PermissionsAndroid.RESULTS.GRANTED);
      if (negada) {
        throw new LinkError(
          versao >= 31
            ? 'Sem a permissão "Dispositivos próximos" o app não acha o comedouro. Libere nas configurações do Android.'
            : 'Sem a permissão de localização o Android não deixa procurar o comedouro. Libere nas configurações.',
          'permission_denied',
        );
      }
    }

    const estado = await this.waitForBluetoothState();
    if (estado === State.PoweredOn) return;
    if (estado === State.PoweredOff) throw new LinkError('Ligue o Bluetooth do celular.', 'bluetooth_off');
    if (estado === State.Unauthorized) {
      throw new LinkError('O FELIS precisa da permissão de Bluetooth.', 'permission_denied');
    }
    if (estado === State.Unsupported) {
      throw new LinkError('Este celular não tem Bluetooth de baixo consumo (BLE).', 'unsupported');
    }
    throw new LinkError('O Bluetooth do celular ainda não está pronto. Tente de novo.', 'ble_error');
  }

  /** Logo depois de abrir o app o estado pode ser "Unknown" por alguns instantes. */
  private async waitForBluetoothState(): Promise<State> {
    const m = this.ble();
    const atual = (await m.state()) as State;
    if (atual !== State.Unknown && atual !== State.Resetting) return atual;
    return new Promise<State>((resolve) => {
      const timer = setTimeout(() => {
        sub.remove();
        resolve(atual);
      }, STATE_WAIT_MS);
      const sub = m.onStateChange((novo) => {
        const s = novo as State;
        if (s === State.Unknown || s === State.Resetting) return;
        clearTimeout(timer);
        sub.remove();
        resolve(s);
      }, false);
    });
  }

  /**
   * Procura comedouros. O guardado vence assim que aparece; sem guardado, o
   * primeiro que aparecer. Se so aparecer outro, fica com ele no fim do prazo.
   */
  private scan(preferido: string | null): Promise<Device> {
    const m = this.ble();
    return new Promise<Device>((resolve, reject) => {
      let terminou = false;
      let reserva: Device | null = null;

      const fim = (erro: unknown, device: Device | null) => {
        if (terminou) return;
        terminou = true;
        clearTimeout(timer);
        m.stopDeviceScan().catch(() => undefined);
        if (device) resolve(device);
        else reject(erro);
      };

      const timer = setTimeout(() => {
        if (reserva) fim(null, reserva);
        else {
          fim(
            new LinkError(
              'Não achei o comedouro. Ele está ligado e perto do celular? O app no PC (ou outro celular) não pode estar conectado nele.',
              'not_found',
            ),
            null,
          );
        }
      }, SCAN_TIMEOUT_MS);

      // Sem filtro de UUID no Android: alguns celulares nao filtram UUID de 128
      // bits direito. O filtro e feito aqui, pelo UUID ou pelo nome.
      m.startDeviceScan(null, { allowDuplicates: false }, async (erro, device) => {
        if (erro) {
          fim(erro, null);
          return;
        }
        if (!device || !isFeeder(device)) return;
        if (!preferido || device.id === preferido) fim(null, device);
        else reserva ??= device;
      }).catch((erro) => fim(erro, null));
    });
  }

  /** Conecta, negocia o MTU, descobre o servico e liga as notificacoes. */
  private async openSession(id: string): Promise<void> {
    if (!id) throw disconnectedError();
    const m = this.ble();
    this.attemptId = id;

    const device = await m.connectToDevice(id, {
      requestMTU: MTU_REQUEST,
      timeout: CONNECT_TIMEOUT_MS,
    });
    await device.discoverAllServicesAndCharacteristics();

    let mtu = device.mtu ?? 23;
    if (Platform.OS === 'android' && mtu < MTU_REQUEST) {
      try {
        mtu = (await m.requestMTUForDevice(id, MTU_REQUEST)).mtu ?? mtu;
      } catch {
        // fica com o que veio: os pedacos ficam menores, mas funciona
      }
    }
    this.chunk = Math.max(MIN_CHUNK, Math.min(APP_CHUNK_BYTES, mtu - 3));

    this.assembler.reset();
    this.queue.reset();
    this.deviceId = id;

    this.disconnectSub?.remove();
    this.disconnectSub = m.onDeviceDisconnected(id, () => this.handleDrop());

    this.monitorSub?.remove();
    this.monitorSub = m.monitorCharacteristicForDevice(
      id,
      FEEDER_SERVICE_UUID,
      FEEDER_TX_UUID,
      (erro, caracteristica) => {
        // Erro aqui e quase sempre a conexao caindo; quem trata e o onDeviceDisconnected.
        if (erro || !caracteristica?.value) return;
        this.onPacket(base64ToBytes(caracteristica.value));
      },
    );

    this.connected = true;
    this.emitConnection(true);
  }

  private onPacket(pacote: Uint8Array): void {
    const texto = this.assembler.push(pacote);
    if (texto === null) return; // pedaco de uma mensagem maior
    // Ruido na linha nunca pode derrubar o app.
    const msg = decodeDeviceMessage(texto);
    if (msg) this.emitMessage(msg);
  }

  private handleDrop(): void {
    const estavaConectado = this.connected;
    this.dropSession();
    if (estavaConectado) this.emitConnection(false);
    // So reconecta sozinho uma sessao que chegou a funcionar e caiu sem ninguem pedir.
    if (estavaConectado && !this.userDisconnected) this.reconnector.schedule();
  }

  /** Esquece a sessao atual: assinaturas, pedacos pela metade e a fila. */
  private dropSession(): void {
    this.connected = false;
    this.deviceId = null;
    this.monitorSub?.remove();
    this.monitorSub = null;
    this.disconnectSub?.remove();
    this.disconnectSub = null;
    this.queue.reset(); // escritas que ainda estao na fila desistem com 'disconnected'
    this.assembler.reset();
  }

  /** Depois de uma tentativa que falhou: nao deixa conexao pela metade. */
  private closeQuietly(): void {
    const id = this.attemptId;
    this.attemptId = null;
    if (!this.connected && id && this.manager) {
      this.manager.cancelDeviceConnection(id).catch(() => undefined);
    }
  }

  private emitMessage(msg: DeviceMessage): void {
    this.messageHandlers.forEach((h) => h(msg));
  }

  private emitConnection(connected: boolean): void {
    this.connectionHandlers.forEach((h) => h(connected));
  }
}
