import { decodeDeviceMessage, encodeAppMessage } from '../src/device/protocol';
import { makeDefaultCat, rangeOf, rangesOverlap, validateCat, hasErrors, Cat } from '../src/domain/cats';
import { decide, identify, consumeGrant, grantExpired, PRESENCE_THRESHOLD_G } from '../src/domain/rules';
import { formatKg, formatDuration, kgInputToGrams, gramsToKgInput, formatCountdown } from '../src/domain/format';

const mimi: Cat = {
  id: 1,
  name: 'Mimi',
  emoji: '🐱',
  color: 'laranja',
  weightG: 3200,
  toleranceG: 300,
  rule: 'allow',
};

const bolota: Cat = {
  id: 2,
  name: 'Bolota',
  emoji: '🐈',
  color: 'azul',
  weightG: 6500,
  toleranceG: 300,
  rule: 'deny',
};

describe('protocolo', () => {
  it('ida e volta de uma mensagem de status', () => {
    const raw = JSON.stringify({
      t: 'st',
      wG: 3240,
      cat: 1,
      door: 'open',
      fcC: 0,
      fcO: 1,
      grant: null,
      mode: 'learn',
      cfgV: 4,
    });
    const msg = decodeDeviceMessage(raw);
    expect(msg).toEqual({
      t: 'st',
      wG: 3240,
      cat: 1,
      door: 'open',
      fcC: 0,
      fcO: 1,
      grant: null,
      mode: 'learn',
      cfgV: 4,
    });
  });

  it('assume modo automatico quando o campo nao vem', () => {
    const msg = decodeDeviceMessage(
      '{"t":"st","wG":0,"cat":0,"door":"closed","fcC":1,"fcO":0,"grant":null}',
    );
    expect(msg).toMatchObject({ mode: 'auto', cfgV: 0 });
  });

  it('rejeita lixo sem derrubar o app', () => {
    expect(decodeDeviceMessage('nao e json')).toBeNull();
    expect(decodeDeviceMessage('{"t":"st"}')).toBeNull();
    expect(decodeDeviceMessage('{"t":"st","wG":1,"cat":9,"door":"open","fcC":0,"fcO":0}')).toBeNull();
    expect(decodeDeviceMessage('{"t":"desconhecida"}')).toBeNull();
  });

  it('aceita evento de visita com liberacao', () => {
    const ev = decodeDeviceMessage(
      '{"t":"ev","seq":7,"ts":1789574460,"kind":"visit","cat":2,"wG":6480,"durS":40,"door":"opened","why":"grant"}',
    );
    expect(ev).toMatchObject({ seq: 7, kind: 'visit', cat: 2, door: 'opened', why: 'grant' });
  });

  it('le a configuracao guardada no comedouro (cfgDump)', () => {
    const dump = decodeDeviceMessage(
      JSON.stringify({
        t: 'cfgDump',
        cats: [{ k: 1, minG: 2700, maxG: 3700, rule: 'allow' }],
        confMs: 2000,
        leaveMs: 4000,
        maxOpenS: 300,
        mode: 'auto',
        cfgV: 3,
        profile: '[{"id":1,"name":"Mimi","emoji":"🐱","color":"laranja"}]',
      }),
    );
    expect(dump).toMatchObject({ t: 'cfgDump', cfgV: 3 });
    expect((dump as { cats: unknown[] }).cats).toHaveLength(1);
  });

  it('rejeita cfgDump com gato invalido', () => {
    expect(
      decodeDeviceMessage(
        '{"t":"cfgDump","cats":[{"k":9,"minG":1,"maxG":2,"rule":"allow"}],"confMs":1,"leaveMs":1,"maxOpenS":1,"mode":"auto"}',
      ),
    ).toBeNull();
  });

  it('serializa comando do app com id', () => {
    expect(encodeAppMessage({ t: 'cmd', id: 3, op: 'refill', timeoutS: 300 })).toBe(
      '{"t":"cmd","id":3,"op":"refill","timeoutS":300}',
    );
  });
});

describe('faixas de peso', () => {
  it('calcula a faixa a partir do peso e da tolerancia', () => {
    expect(rangeOf(mimi)).toEqual({ minG: 2900, maxG: 3500 });
  });

  it('detecta sobreposicao', () => {
    expect(rangesOverlap(rangeOf(mimi), rangeOf(bolota))).toBe(false);
    expect(rangesOverlap(rangeOf(mimi), rangeOf({ weightG: 3400, toleranceG: 300 }))).toBe(true);
  });

  it('barra cadastro com faixas encostadas', () => {
    const conflitante: Cat = { ...bolota, weightG: 3400 };
    const errors = validateCat(conflitante, [mimi]);
    expect(errors.range).toBeDefined();
    expect(hasErrors(errors)).toBe(true);
  });

  it('exige nome e peso dentro dos limites', () => {
    const semNome = validateCat({ ...makeDefaultCat(1) }, []);
    expect(semNome.name).toBeDefined();

    const pesado = validateCat({ ...mimi, weightG: 20000 }, []);
    expect(pesado.weightG).toBeDefined();
  });
});

describe('regra de acesso', () => {
  const cats = [mimi, bolota];

  it('ignora peso abaixo do limiar de presenca', () => {
    expect(identify(PRESENCE_THRESHOLD_G - 1, cats)).toBe(0);
  });

  it('abre para o gato liberado', () => {
    expect(decide(3240, cats, null, 1000)).toEqual({ cat: 1, allowed: true, why: 'rule' });
  });

  it('nega o gato em dieta', () => {
    expect(decide(6480, cats, null, 1000)).toEqual({ cat: 2, allowed: false, why: 'rule' });
  });

  it('abre para o gato em dieta com liberacao por tempo', () => {
    const grant = { cat: 2 as const, untilS: 2000 };
    expect(decide(6480, cats, grant, 1500)).toEqual({ cat: 2, allowed: true, why: 'grant' });
    expect(decide(6480, cats, grant, 2500)).toEqual({ cat: 2, allowed: false, why: 'rule' });
  });

  it('consome liberacao por visitas', () => {
    expect(consumeGrant({ cat: 2, visits: 2 })).toEqual({ cat: 2, visits: 1 });
    expect(consumeGrant({ cat: 2, visits: 1 })).toBeNull();
  });

  it('marca liberacao vencida', () => {
    expect(grantExpired({ cat: 2, untilS: 100 }, 101)).toBe(true);
    expect(grantExpired({ cat: 2, untilS: 100 }, 99)).toBe(false);
  });

  it('trata visitante desconhecido', () => {
    expect(decide(1100, cats, null, 1000)).toEqual({ cat: 0, allowed: false, why: 'unknown' });
  });
});

describe('formatacao', () => {
  it('mostra peso em kg no padrao brasileiro', () => {
    expect(formatKg(3240)).toBe('3,24 kg');
    expect(formatKg(3240, 1)).toBe('3,2 kg');
  });

  it('converte texto do formulario para gramas inteiras', () => {
    expect(kgInputToGrams('3,2')).toBe(3200);
    expect(kgInputToGrams('2.01')).toBe(2010); // sem erro de ponto flutuante
    expect(kgInputToGrams('abc')).toBeNaN();
    expect(kgInputToGrams('')).toBeNaN();
  });

  it('volta de gramas para o campo', () => {
    expect(gramsToKgInput(3200)).toBe('3,2');
  });

  it('formata duracao e contagem regressiva', () => {
    expect(formatDuration(40)).toBe('40 s');
    expect(formatDuration(250)).toBe('4 min 10 s');
    expect(formatCountdown(125)).toBe('2:05');
  });
});
