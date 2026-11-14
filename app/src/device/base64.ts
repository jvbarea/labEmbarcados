/**
 * Base64 <-> bytes, sem depender de Buffer (que nao existe no React Native).
 *
 * O react-native-ble-plx troca o valor das caracteristicas BLE como texto
 * base64; o protocolo trabalha com os bytes UTF-8 (ver framing.ts).
 */

const ALFABETO = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

const VALOR = new Int16Array(128).fill(-1);
for (let i = 0; i < ALFABETO.length; i++) VALOR[ALFABETO.charCodeAt(i)] = i;
// variante "URL-safe", por garantia
VALOR['-'.charCodeAt(0)] = 62;
VALOR['_'.charCodeAt(0)] = 63;

export function bytesToBase64(bytes: Uint8Array): string {
  let out = '';
  let i = 0;
  for (; i + 2 < bytes.length; i += 3) {
    const n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out += ALFABETO[(n >> 18) & 63] + ALFABETO[(n >> 12) & 63] + ALFABETO[(n >> 6) & 63] + ALFABETO[n & 63];
  }
  const resto = bytes.length - i;
  if (resto === 1) {
    const n = bytes[i] << 16;
    out += ALFABETO[(n >> 18) & 63] + ALFABETO[(n >> 12) & 63] + '==';
  } else if (resto === 2) {
    const n = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out += ALFABETO[(n >> 18) & 63] + ALFABETO[(n >> 12) & 63] + ALFABETO[(n >> 6) & 63] + '=';
  }
  return out;
}

/** Ignora espacos e quebras de linha; caractere fora do alfabeto lanca erro. */
export function base64ToBytes(texto: string): Uint8Array {
  const limpo = texto.replace(/[\s=]/g, '');
  const out = new Uint8Array(Math.floor((limpo.length * 3) / 4));
  let buffer = 0;
  let bits = 0;
  let pos = 0;
  for (let i = 0; i < limpo.length; i++) {
    const c = limpo.charCodeAt(i);
    const v = c < 128 ? VALOR[c] : -1;
    if (v < 0) throw new Error(`base64 invalido na posicao ${i}`);
    buffer = (buffer << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[pos++] = (buffer >> bits) & 0xff;
    }
  }
  return out.subarray(0, pos);
}
