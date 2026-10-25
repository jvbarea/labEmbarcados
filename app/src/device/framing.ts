/**
 * Fragmentacao das mensagens no BLE.
 *
 * Uma escrita ou notificacao BLE carrega no maximo MTU - 3 bytes, e algumas
 * mensagens do protocolo passam disso (o cfgDump com o "profile" dos gatos tem
 * uns 400 bytes). O contrato, igual nos dois sentidos e no firmware
 * (teste_componentes/teste_esp/firmware_ble):
 *
 *   - mensagem que cabe num pacote vai inteira: o JSON cru, comecando com "{";
 *   - mensagem maior vai em pedacos, cada um com o cabecalho ASCII "#i/n:"
 *     (i = 1..n, n ate 99) seguido de uma fatia dos BYTES UTF-8 da mensagem.
 *
 * As fatias sao cortadas em bytes, entao um emoji pode ficar dividido entre
 * dois pedacos: quem recebe junta os bytes e so decodifica no fim.
 *
 * Este arquivo nao depende de React Native, para os testes rodarem direto.
 */

/** Tamanho dos pacotes que o app escreve. O firmware pede MTU 185 (182 uteis). */
export const APP_CHUNK_BYTES = 180;

/** Maior cabecalho possivel: "#99/99:". */
export const MAX_HEADER_BYTES = 7;

export const MAX_FRAGMENTS = 99;

/** Acima disso a montagem e abandonada: nenhuma mensagem do protocolo chega perto. */
export const MAX_ASSEMBLED_BYTES = 16 * 1024;

const HASH = 0x23; // '#'
const COLON = 0x3a; // ':'
const SLASH = 0x2f; // '/'

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/** A mensagem nao cabe em 99 pedacos. */
export class FrameTooBigError extends Error {
  constructor(bytes: number, chunk: number) {
    super(`Mensagem de ${bytes} bytes nao cabe em ${MAX_FRAGMENTS} pacotes de ${chunk} bytes.`);
    this.name = 'FrameTooBigError';
  }
}

/**
 * Divide uma mensagem em pacotes de no maximo `chunk` bytes, ja com os
 * cabecalhos. Devolve um pacote so (a mensagem crua) quando ela cabe.
 */
export function encodeFrames(
  text: string,
  chunk: number = APP_CHUNK_BYTES,
): Uint8Array<ArrayBuffer>[] {
  const bytes = encoder.encode(text);
  if (bytes.length <= chunk) return [bytes];

  const slice = chunk - MAX_HEADER_BYTES;
  if (slice <= 0) {
    throw new RangeError(`Pacote de ${chunk} bytes nao comporta o cabecalho de fragmento.`);
  }

  const total = Math.ceil(bytes.length / slice);
  if (total > MAX_FRAGMENTS) throw new FrameTooBigError(bytes.length, chunk);

  const frames: Uint8Array<ArrayBuffer>[] = [];
  for (let i = 0; i < total; i++) {
    const header = encoder.encode(`#${i + 1}/${total}:`);
    const part = bytes.subarray(i * slice, Math.min(bytes.length, (i + 1) * slice));
    const frame = new Uint8Array(header.length + part.length);
    frame.set(header, 0);
    frame.set(part, header.length);
    frames.push(frame);
  }
  return frames;
}

interface FrameHeader {
  index: number;
  total: number;
  /** Onde comecam os dados, logo depois do ":". */
  bodyStart: number;
}

/** Le "#i/n:". Devolve null para qualquer cabecalho fora do formato. */
function parseHeader(packet: Uint8Array): FrameHeader | null {
  // '#' ja conferido; procura o ':' dentro dos primeiros bytes
  const limite = Math.min(packet.length, MAX_HEADER_BYTES + 1);
  let colon = -1;
  for (let k = 1; k < limite; k++) {
    if (packet[k] === COLON) {
      colon = k;
      break;
    }
  }
  if (colon < 0) return null;

  let index = 0;
  let total = 0;
  let digitosIndex = 0;
  let digitosTotal = 0;
  let depoisDaBarra = false;

  for (let k = 1; k < colon; k++) {
    const b = packet[k];
    if (b === SLASH) {
      if (depoisDaBarra) return null;
      depoisDaBarra = true;
      continue;
    }
    if (b < 0x30 || b > 0x39) return null;
    const digito = b - 0x30;
    if (depoisDaBarra) {
      total = total * 10 + digito;
      digitosTotal++;
    } else {
      index = index * 10 + digito;
      digitosIndex++;
    }
  }

  if (!depoisDaBarra || digitosIndex === 0 || digitosTotal === 0) return null;
  if (total < 1 || total > MAX_FRAGMENTS || index < 1 || index > total) return null;
  return { index, total, bodyStart: colon + 1 };
}

/**
 * Remonta as mensagens que chegam em pacotes.
 *
 * Os pedacos de uma mensagem chegam sempre em ordem e seguidos (o firmware
 * tem uma fila unica de envio). Qualquer coisa fora disso - pedaco perdido,
 * fora de ordem, uma mensagem inteira no meio - descarta a montagem em curso
 * e o receptor espera o proximo "#1/n:".
 */
export class FrameAssembler {
  private parts: Uint8Array[] = [];
  private size = 0;
  private expected = 0; // proximo indice esperado; 0 = nenhuma montagem em curso
  private total = 0;

  constructor(private readonly maxBytes: number = MAX_ASSEMBLED_BYTES) {}

  reset(): void {
    this.parts = [];
    this.size = 0;
    this.expected = 0;
    this.total = 0;
  }

  /** true enquanto ha uma mensagem pela metade. */
  get pending(): boolean {
    return this.expected > 0;
  }

  /** Recebe um pacote. Devolve o texto da mensagem quando ela fica completa. */
  push(packet: Uint8Array): string | null {
    if (packet.length === 0) return null;

    if (packet[0] !== HASH) {
      // Mensagem inteira (normalmente "{..."). Se havia montagem, ela se perdeu.
      this.reset();
      return decoder.decode(packet);
    }

    const header = parseHeader(packet);
    if (!header) {
      this.reset();
      return null;
    }

    if (header.index === 1) {
      this.reset();
      this.total = header.total;
      this.expected = 1;
    }

    if (this.expected === 0 || header.index !== this.expected || header.total !== this.total) {
      this.reset();
      return null;
    }

    const body = packet.slice(header.bodyStart); // copia: o navegador pode reaproveitar o buffer
    this.size += body.length;
    if (this.size > this.maxBytes) {
      this.reset();
      return null;
    }
    this.parts.push(body);
    this.expected++;

    if (header.index < header.total) return null;

    const inteira = new Uint8Array(this.size);
    let pos = 0;
    for (const parte of this.parts) {
      inteira.set(parte, pos);
      pos += parte.length;
    }
    this.reset();
    return decoder.decode(inteira);
  }
}
