/**
 * Compatibilidade com o firmware real (teste_componentes/teste_esp/firmware_ble).
 *
 * As mensagens de fixtures/firmware-messages.jsonl estao no formato que a ESP32
 * envia. Se o firmware mudar um campo e o app deixar de entender, e aqui que
 * aparece - antes de aparecer na demonstracao.
 */

import { readFileSync } from 'fs';
import { join } from 'path';

import { APP_CHUNK_BYTES, FrameAssembler, encodeFrames } from '../src/device/framing';
import {
  CfgDumpMsg,
  DeviceMessage,
  EventMsg,
  StatusMsg,
  decodeDeviceMessage,
  describeError,
  encodeAppMessage,
} from '../src/device/protocol';
import { decodeProfile, encodeProfile } from '../src/store/catsStore';
import { Cat, makeDefaultCat } from '../src/domain/cats';

jest.mock('@react-native-async-storage/async-storage', () =>
  require('@react-native-async-storage/async-storage/jest/async-storage-mock'),
);

/** Maior mensagem que o firmware remonta (BLE_MSG_MAX). */
const FIRMWARE_MAX_MESSAGE_BYTES = 512;

const LINHAS = readFileSync(join(__dirname, 'fixtures', 'firmware-messages.jsonl'), 'utf8')
  .split(/\r?\n/)
  .map((l) => l.trim())
  .filter((l) => l.length > 0 && !l.startsWith('//'));

const decoded = (): DeviceMessage[] =>
  LINHAS.map((raw) => decodeDeviceMessage(raw)).filter((m): m is DeviceMessage => m !== null);

function remonta(frames: Uint8Array[]): string[] {
  const a = new FrameAssembler();
  return frames.map((f) => a.push(f)).filter((m): m is string => m !== null);
}

describe('mensagens do firmware', () => {
  it('a fixture tem exemplos', () => {
    expect(LINHAS.length).toBeGreaterThan(5);
  });

  it.each(LINHAS)('o app entende %s', (raw) => {
    const msg = decodeDeviceMessage(raw);
    expect(msg).not.toBeNull();
    expect(msg!.t).toBe(JSON.parse(raw).t);
  });

  it('cobre todos os tipos que o comedouro manda', () => {
    const tipos = new Set(decoded().map((m) => m.t));
    expect([...tipos].sort()).toEqual(['ack', 'cfgDump', 'ev', 'st', 'syncEnd']);
  });

  it('le a liberacao dentro do status (por visitas e por prazo)', () => {
    const st = decoded().filter((m): m is StatusMsg => m.t === 'st');
    expect(st.map((s) => s.grant)).toEqual(
      expect.arrayContaining([null, { cat: 2, visits: 1 }, { cat: 2, untilS: 1761400000 }]),
    );
    expect(st.some((s) => s.mode === 'learn')).toBe(true);
  });

  it('cfgDump traz o profile e o app recupera nomes e emoji dele', () => {
    const dump = decoded().find((m): m is CfgDumpMsg => m.t === 'cfgDump');
    expect(dump?.profile).toBeDefined();
    const perfis = decodeProfile(dump!.profile);
    expect(perfis.map((p) => [p.id, p.name, p.emoji])).toEqual([
      [1, 'Mimi', '🐱'],
      [2, 'Bolota', '🐈'],
    ]);
  });

  it('visita do modo adaptacao vem marcada como amostra', () => {
    const ev = decoded().filter((m): m is EventMsg => m.t === 'ev');
    expect(ev.find((e) => e.why === 'learn')).toMatchObject({ kind: 'visit', learn: 1, door: 'opened' });
    expect(ev.find((e) => e.kind === 'fault')?.err).toBe('door_timeout');
  });

  it('os codigos de erro do firmware tem texto proprio na tela', () => {
    const codigos = [
      'door_fault',
      'door_timeout',
      'sensor_fault',
      'unknown_cat',
      'no_clock',
      'bad_range',
      'bad_cats',
      'bad_json',
      'bad_type',
      'bad_op',
      'bad_cmd',
      'door_moving',
      'scale_mute',
      'calib_failed',
      'no_scale',
      'missing_ref',
      'too_big',
      'real_scale',
    ];
    const generico = describeError('codigo_que_nao_existe');
    codigos.forEach((c) => {
      const texto = describeError(c);
      expect(texto).not.toBe(generico);
      expect(texto).not.toContain(c);
    });
    expect(generico).toContain('codigo_que_nao_existe');
  });

  it.each([20, 64, 180])(
    'toda mensagem chega igual depois de fragmentada pelo firmware (pacote de %i bytes)',
    (chunk) => {
      LINHAS.forEach((raw) => {
        expect(remonta(encodeFrames(raw, chunk))).toEqual([raw]);
      });
    },
  );
});

describe('mensagens do app para o firmware', () => {
  const gatos: Cat[] = [
    { ...makeDefaultCat(1), name: 'Mimi Açúcar da Silva', emoji: '🐈‍⬛', color: 'roxo' },
    { ...makeDefaultCat(2), name: 'Bolota Pança Grande', emoji: '😸', color: 'verde' },
  ];

  it('o profile de dois gatos com emoji vai em pedacos e chega identico', () => {
    const json = encodeAppMessage({ t: 'profile', id: 9, blob: encodeProfile(gatos) });
    const bytes = new TextEncoder().encode(json).length;
    expect(bytes).toBeLessThanOrEqual(FIRMWARE_MAX_MESSAGE_BYTES);

    const frames = encodeFrames(json, APP_CHUNK_BYTES);
    expect(frames.length).toBeGreaterThan(1);
    frames.forEach((f) => expect(f.length).toBeLessThanOrEqual(APP_CHUNK_BYTES));

    const [volta] = remonta(frames);
    expect(volta).toBe(json);
    const perfis = decodeProfile(JSON.parse(volta).blob);
    expect(perfis.map((p) => p.name)).toEqual(['Mimi Açúcar da Silva', 'Bolota Pança Grande']);
  });

  it('o cadastro dos gatos cabe no limite do firmware', () => {
    const json = encodeAppMessage({
      t: 'cats',
      id: 10,
      cats: [
        { k: 1, minG: 2700, maxG: 3700, rule: 'allow' },
        { k: 2, minG: 6000, maxG: 7000, rule: 'deny' },
      ],
    });
    expect(new TextEncoder().encode(json).length).toBeLessThanOrEqual(FIRMWARE_MAX_MESSAGE_BYTES);
  });
});
