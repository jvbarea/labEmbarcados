/**
 * Teste de integracao do comedouro simulado: percorre o ciclo completo de uma
 * visita, como as telas fazem em tempo real.
 */

import { MockLink } from '../src/device/MockLink';
import { CfgDumpMsg, DeviceCat, DeviceMessage, EventMsg, StatusMsg } from '../src/device/protocol';

const CATS: DeviceCat[] = [
  { k: 1, minG: 2900, maxG: 3500, rule: 'allow' }, // Mimi, liberada
  { k: 2, minG: 6200, maxG: 6800, rule: 'deny' }, // Bolota, em dieta
];

let id = 1;

async function setup() {
  const link = new MockLink();
  const recebidas: DeviceMessage[] = [];
  link.onMessage((m) => recebidas.push(m));

  const conectando = link.connect();
  await jest.advanceTimersByTimeAsync(400);
  await conectando;

  await send(link, { t: 'cats', id: id++, cats: CATS });
  return { link, recebidas };
}

async function send(link: MockLink, msg: Parameters<MockLink['send']>[0]) {
  const p = link.send(msg);
  await jest.advanceTimersByTimeAsync(200);
  return p;
}

function ultimoStatus(msgs: DeviceMessage[]): StatusMsg | undefined {
  return [...msgs].reverse().find((m): m is StatusMsg => m.t === 'st');
}

function visitas(msgs: DeviceMessage[]): EventMsg[] {
  return msgs.filter((m): m is EventMsg => m.t === 'ev' && m.kind === 'visit');
}

describe('MockLink', () => {
  beforeEach(() => {
    jest.useFakeTimers();
    id = 1;
  });

  afterEach(() => {
    jest.useRealTimers();
  });

  it('abre a porta para o gato liberado e registra a visita', async () => {
    const { link, recebidas } = await setup();

    link.sim.setWeight(3200);
    await jest.advanceTimersByTimeAsync(3000); // passa do tempo de confirmacao

    const durante = ultimoStatus(recebidas);
    expect(durante?.cat).toBe(1);
    expect(['opening', 'open']).toContain(durante?.door);

    await jest.advanceTimersByTimeAsync(2000);
    expect(ultimoStatus(recebidas)?.door).toBe('open');
    expect(ultimoStatus(recebidas)?.fcO).toBe(1);

    link.sim.clear();
    await jest.advanceTimersByTimeAsync(6000); // sai da balanca e espera fechar

    const visita = visitas(recebidas).at(-1);
    expect(visita).toMatchObject({ cat: 1, door: 'opened', why: 'rule' });
    expect(visita!.durS).toBeGreaterThan(0);

    await jest.advanceTimersByTimeAsync(2000);
    expect(ultimoStatus(recebidas)?.door).toBe('closed');
  });

  it('mantem a porta fechada para o gato em dieta', async () => {
    const { link, recebidas } = await setup();

    link.sim.setWeight(6500);
    await jest.advanceTimersByTimeAsync(4000);
    expect(ultimoStatus(recebidas)?.door).toBe('closed');

    link.sim.clear();
    await jest.advanceTimersByTimeAsync(6000);

    expect(visitas(recebidas).at(-1)).toMatchObject({ cat: 2, door: 'denied' });
  });

  it('libera o gato em dieta por uma visita e volta a negar na seguinte', async () => {
    const { link, recebidas } = await setup();

    await send(link, { t: 'grant', id: id++, cat: 2, visits: 1 });

    link.sim.setWeight(6500);
    await jest.advanceTimersByTimeAsync(4000);
    expect(ultimoStatus(recebidas)?.door).toBe('open');

    link.sim.clear();
    await jest.advanceTimersByTimeAsync(8000);
    expect(visitas(recebidas).at(-1)).toMatchObject({ cat: 2, door: 'opened', why: 'grant' });
    expect(ultimoStatus(recebidas)?.grant).toBeNull();

    link.sim.setWeight(6500);
    await jest.advanceTimersByTimeAsync(4000);
    expect(ultimoStatus(recebidas)?.door).toBe('closed');
  });

  it('entra em falha quando a porta trava e rearma com o comando fechar', async () => {
    const { link, recebidas } = await setup();

    link.sim.jam(true);
    link.sim.setWeight(3200);
    await jest.advanceTimersByTimeAsync(8000);

    expect(ultimoStatus(recebidas)?.door).toBe('fault');
    expect(recebidas.some((m) => m.t === 'ev' && m.kind === 'fault')).toBe(true);

    link.sim.jam(false);
    await send(link, { t: 'cmd', id: id++, op: 'close' });
    expect(ultimoStatus(recebidas)?.door).toBe('closed');
  });

  it('guarda eventos enquanto desconectado e sincroniza na volta', async () => {
    const { link, recebidas } = await setup();

    link.sim.dropConnection();
    expect(link.isConnected()).toBe(false);

    link.sim.setWeight(3200);
    await jest.advanceTimersByTimeAsync(4000);
    link.sim.clear();
    await jest.advanceTimersByTimeAsync(6000);

    const durante = recebidas.length;

    const reconectando = link.connect();
    await jest.advanceTimersByTimeAsync(400);
    await reconectando;

    const syncId = id++;
    await send(link, { t: 'sync', id: syncId, since: 0 });
    await jest.advanceTimersByTimeAsync(200);

    expect(recebidas.length).toBeGreaterThan(durante);
    expect(visitas(recebidas).some((v) => v.cat === 1)).toBe(true);
    expect(recebidas.some((m) => m.t === 'syncEnd' && m.id === syncId)).toBe(true);
  });

  it('modo adaptacao: porta aberta e visitas viram amostras de peso', async () => {
    const { link, recebidas } = await setup();

    await send(link, { t: 'mode', id: id++, mode: 'learn' });
    await jest.advanceTimersByTimeAsync(2000);
    expect(ultimoStatus(recebidas)?.mode).toBe('learn');
    expect(ultimoStatus(recebidas)?.door).toBe('open');

    // o gato em dieta tambem pode comer no modo adaptacao
    link.sim.setWeight(6500);
    await jest.advanceTimersByTimeAsync(4000);
    link.sim.clear();
    await jest.advanceTimersByTimeAsync(6000);

    const amostra = visitas(recebidas).at(-1);
    expect(amostra).toMatchObject({ cat: 2, door: 'opened', why: 'learn', learn: 1 });
    expect(ultimoStatus(recebidas)?.door).toBe('open'); // continua aberta

    await send(link, { t: 'mode', id: id++, mode: 'auto' });
    await jest.advanceTimersByTimeAsync(2000);
    expect(ultimoStatus(recebidas)?.mode).toBe('auto');
    expect(ultimoStatus(recebidas)?.door).toBe('closed');
  });

  it('guarda o cadastro na "flash" e devolve no cfgGet (app reinstalado)', async () => {
    const { link, recebidas } = await setup();

    const perfil = '[{"id":1,"name":"Mimi","emoji":"🐱","color":"laranja"}]';
    await send(link, { t: 'profile', id: id++, blob: perfil });
    await send(link, { t: 'cfgGet', id: id++ });

    const dump = [...recebidas].reverse().find((m): m is CfgDumpMsg => m.t === 'cfgDump');
    expect(dump).toBeDefined();
    expect(dump!.cats).toEqual(CATS);
    expect(dump!.profile).toBe(perfil);
    expect(dump!.cfgV).toBeGreaterThan(0);
  });

  it('cfgV muda a cada gravacao, para o app saber que precisa reenviar', async () => {
    const { link, recebidas } = await setup();

    const antes = ultimoStatus(recebidas)?.cfgV ?? 0;
    await send(link, { t: 'cfg', id: id++, confMs: 3000, leaveMs: 5000, maxOpenS: 600 });
    await jest.advanceTimersByTimeAsync(600);

    expect(ultimoStatus(recebidas)?.cfgV).toBeGreaterThan(antes);
  });

  it('apos apagar tudo, o comedouro nao devolve mais cadastro nenhum', async () => {
    const { link, recebidas } = await setup();

    link.sim.factoryReset();
    await send(link, { t: 'cfgGet', id: id++ });

    const dump = [...recebidas].reverse().find((m): m is CfgDumpMsg => m.t === 'cfgDump');
    expect(dump!.cats).toEqual([]);
    expect(dump!.profile).toBeUndefined();
  });

  it('recusa comandos sem conexao', async () => {
    const { link } = await setup();
    link.sim.dropConnection();
    await expect(link.send({ t: 'cmd', id: 99, op: 'close' })).rejects.toThrow();
  });
});
