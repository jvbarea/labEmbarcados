/**
 * Pecas comuns aos dois transportes Bluetooth: o do navegador (WebBleLink) e o
 * nativo do APK (NativeBleLink). Nao dependem de React Native, para os testes
 * rodarem direto.
 *
 *  - os UUIDs do Nordic UART Service, os mesmos do firmware;
 *  - WriteQueue: o GATT faz uma operacao por vez, entao as escritas passam por
 *    uma fila, e todos os pedacos de uma mensagem saem antes da proxima;
 *  - Reconnector: depois de uma queda, tenta reconectar com espera crescente.
 */

/** Nordic UART Service — os mesmos UUIDs de firmware_ble/src/config.h. */
export const FEEDER_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const FEEDER_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // app -> ESP (write)
export const FEEDER_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // ESP -> app (notify)
/** O firmware anuncia como "Alimentador". */
export const FEEDER_NAME_PREFIX = 'Aliment';

/** Espera entre tentativas de reconexao; depois da lista, RECONNECT_MAX_MS. */
export const RECONNECT_BACKOFF_MS = [1000, 2000, 4000, 8000];
export const RECONNECT_MAX_MS = 15000;
export const RECONNECT_MAX_ATTEMPTS = 10;

/**
 * Fila de escrita. Cada `run` so comeca quando o anterior terminou (com
 * sucesso ou erro). `reset()` marca a conexao como encerrada: o que ainda
 * estava esperando na fila desiste com o erro de desconexao, em vez de tentar
 * escrever numa sessao que nao existe mais.
 */
export class WriteQueue {
  private chain: Promise<void> = Promise.resolve();
  private generation = 0;

  reset(): void {
    this.generation++;
  }

  run(
    packets: Uint8Array[],
    writeOne: (packet: Uint8Array) => Promise<void>,
    deadError: () => Error,
  ): Promise<void> {
    const geracao = this.generation;
    const job = async () => {
      for (const packet of packets) {
        if (geracao !== this.generation) throw deadError();
        await writeOne(packet);
      }
    };
    // Encadeia mesmo que a escrita anterior tenha falhado.
    const resultado = this.chain.then(job, job);
    this.chain = resultado.catch(() => undefined);
    return resultado;
  }
}

export interface ReconnectorOptions {
  /** Uma tentativa; rejeitar = falhou. */
  attempt: () => Promise<void>;
  /** Ainda faz sentido tentar? (ex.: o usuario nao pediu para desconectar) */
  shouldTry: () => boolean;
  /** Limpeza depois de uma tentativa que falhou. */
  onFailedAttempt?: () => void;
  onGiveUp?: () => void;
  backoffMs?: number[];
  maxMs?: number;
  maxAttempts?: number;
}

/** Reconexao com espera crescente: 1, 2, 4, 8 s, depois 15 s, ate 10 tentativas. */
export class Reconnector {
  private timer: ReturnType<typeof setTimeout> | null = null;
  private attempts = 0;

  constructor(private readonly opts: ReconnectorOptions) {}

  get scheduled(): boolean {
    return this.timer !== null;
  }

  schedule(): void {
    if (this.timer || !this.opts.shouldTry()) return;
    const max = this.opts.maxAttempts ?? RECONNECT_MAX_ATTEMPTS;
    if (this.attempts >= max) {
      this.opts.onGiveUp?.();
      return;
    }
    const lista = this.opts.backoffMs ?? RECONNECT_BACKOFF_MS;
    const espera = lista[this.attempts] ?? this.opts.maxMs ?? RECONNECT_MAX_MS;
    this.attempts++;
    this.timer = setTimeout(() => {
      this.timer = null;
      void this.tryOnce();
    }, espera);
  }

  /** Para de tentar e zera a contagem (conexao manual, ou o usuario desconectou). */
  cancel(): void {
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
    this.attempts = 0;
  }

  private async tryOnce(): Promise<void> {
    if (!this.opts.shouldTry()) return;
    try {
      await this.opts.attempt();
      this.attempts = 0;
    } catch {
      this.opts.onFailedAttempt?.();
      this.schedule();
    }
  }
}
