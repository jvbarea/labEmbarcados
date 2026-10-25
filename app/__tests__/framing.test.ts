/**
 * Fragmentacao BLE: o mesmo contrato que o firmware implementa em
 * teste_componentes/teste_esp/firmware_ble (BleControl).
 */

import {
  APP_CHUNK_BYTES,
  FrameAssembler,
  FrameTooBigError,
  MAX_HEADER_BYTES,
  encodeFrames,
} from '../src/device/framing';

const encoder = new TextEncoder();
const ascii = (s: string) => encoder.encode(s);

/** Junta pacote a pacote, como as notificacoes chegam. */
function assemble(frames: Uint8Array[], assembler = new FrameAssembler()): string[] {
  const out: string[] = [];
  for (const f of frames) {
    const msg = assembler.push(f);
    if (msg !== null) out.push(msg);
  }
  return out;
}

const PERFIL = JSON.stringify([
  { id: 1, name: 'Mimi Açúcar', emoji: '🐱', color: 'laranja' },
  { id: 2, name: 'Bolota', emoji: '🐈‍⬛', color: 'azul' },
]);

const LONGA = JSON.stringify({
  t: 'cfgDump',
  cats: [
    { k: 1, minG: 2700, maxG: 3700, rule: 'allow' },
    { k: 2, minG: 6000, maxG: 7000, rule: 'deny' },
  ],
  confMs: 2000,
  leaveMs: 4000,
  maxOpenS: 300,
  mode: 'auto',
  cfgV: 12,
  profile: PERFIL,
});

describe('encodeFrames', () => {
  it('manda inteira, sem cabecalho, a mensagem que cabe no pacote', () => {
    const msg = '{"t":"cmd","id":1,"op":"close"}';
    const frames = encodeFrames(msg, APP_CHUNK_BYTES);
    expect(frames).toHaveLength(1);
    expect(frames[0][0]).toBe(0x7b); // '{'
    expect(new TextDecoder().decode(frames[0])).toBe(msg);
  });

  it('nao fragmenta quando o tamanho e exatamente o do pacote', () => {
    const msg = `{"x":"${'a'.repeat(20 - 8)}"}`;
    expect(encoder.encode(msg).length).toBe(20);
    expect(encodeFrames(msg, 20)).toHaveLength(1);
  });

  it.each([20, 64, 180])('fragmenta e remonta com pacote de %i bytes', (chunk) => {
    const frames = encodeFrames(LONGA, chunk);
    expect(frames.length).toBeGreaterThan(1);

    frames.forEach((f, i) => {
      expect(f.length).toBeLessThanOrEqual(chunk);
      const header = new TextDecoder().decode(f.subarray(0, MAX_HEADER_BYTES));
      expect(header.startsWith(`#${i + 1}/${frames.length}:`)).toBe(true);
    });

    expect(assemble(frames)).toEqual([LONGA]);
  });

  it('corta no meio de um emoji e mesmo assim remonta certo', () => {
    // pacote pequeno o bastante para algum corte cair dentro de um caractere de 4 bytes
    const msg = JSON.stringify({ t: 'profile', id: 3, blob: '🐱🐈‍⬛😺😸🙀🐯🦁'.repeat(3) });
    for (let chunk = 9; chunk <= 30; chunk++) {
      expect(assemble(encodeFrames(msg, chunk))).toEqual([msg]);
    }
  });

  it('recusa mensagem que precisaria de mais de 99 pacotes', () => {
    const enorme = JSON.stringify({ t: 'profile', id: 1, blob: 'x'.repeat(99 * 13 + 50) });
    expect(() => encodeFrames(enorme, 20)).toThrow(FrameTooBigError);
  });
});

describe('FrameAssembler', () => {
  it('passa direto uma mensagem inteira', () => {
    const a = new FrameAssembler();
    expect(a.push(ascii('{"t":"ack","id":1,"ok":true}'))).toBe('{"t":"ack","id":1,"ok":true}');
    expect(a.pending).toBe(false);
  });

  it('descarta a mensagem com pedaco perdido e a seguinte chega normal', () => {
    const frames = encodeFrames(LONGA, 64);
    expect(frames.length).toBeGreaterThan(3);
    const a = new FrameAssembler();

    const semUm = [...frames.slice(0, 1), ...frames.slice(2)]; // perdeu o #2
    expect(assemble(semUm, a)).toEqual([]);
    expect(assemble(frames, a)).toEqual([LONGA]);
  });

  it('descarta pedacos fora de ordem', () => {
    const frames = encodeFrames(LONGA, 64);
    const trocados = [frames[0], frames[2], frames[1], ...frames.slice(3)];
    const a = new FrameAssembler();
    expect(assemble(trocados, a)).toEqual([]);
    expect(assemble(frames, a)).toEqual([LONGA]);
  });

  it('pedaco do meio sem o comeco e ignorado', () => {
    const frames = encodeFrames(LONGA, 64);
    const a = new FrameAssembler();
    expect(a.push(frames[1])).toBeNull();
    expect(a.pending).toBe(false);
    expect(assemble(frames, a)).toEqual([LONGA]);
  });

  it('um "#1/n:" novo recomeca a montagem', () => {
    const frames = encodeFrames(LONGA, 64);
    const a = new FrameAssembler();
    a.push(frames[0]);
    a.push(frames[1]);
    expect(assemble(frames, a)).toEqual([LONGA]);
  });

  it('mensagem inteira no meio da montagem cancela a montagem e e entregue', () => {
    const frames = encodeFrames(LONGA, 64);
    const a = new FrameAssembler();
    a.push(frames[0]);
    a.push(frames[1]);

    const st = '{"t":"st","wG":0,"cat":0,"door":"closed","fcC":1,"fcO":0,"grant":null,"mode":"auto","cfgV":1}';
    expect(a.push(ascii(st))).toBe(st);
    expect(a.pending).toBe(false);

    // o resto da mensagem antiga nao forma nada
    expect(assemble(frames.slice(2), a)).toEqual([]);
  });

  it.each(['#x/2:abc', '#1-2:abc', '#3/2:abc', '#0/2:abc', '#1/2abcdefghij', '#/2:abc', '#1/:abc', '#1/2/3:a'])(
    'ignora cabecalho malformado %s',
    (lixo) => {
      const a = new FrameAssembler();
      expect(a.push(ascii(lixo))).toBeNull();
      expect(a.pending).toBe(false);
    },
  );

  it('total diferente no meio da montagem descarta', () => {
    const a = new FrameAssembler();
    expect(a.push(ascii('#1/3:{"a":'))).toBeNull();
    expect(a.push(ascii('#2/4:1'))).toBeNull();
    expect(a.pending).toBe(false);
  });

  it('abandona montagem que passa do limite de tamanho', () => {
    const a = new FrameAssembler(32);
    expect(a.push(ascii(`#1/3:${'a'.repeat(20)}`))).toBeNull();
    expect(a.push(ascii(`#2/3:${'b'.repeat(20)}`))).toBeNull();
    expect(a.pending).toBe(false);
    expect(a.push(ascii(`#3/3:${'c'.repeat(5)}`))).toBeNull();
  });

  it('ignora pacote vazio', () => {
    expect(new FrameAssembler().push(new Uint8Array(0))).toBeNull();
  });
});
