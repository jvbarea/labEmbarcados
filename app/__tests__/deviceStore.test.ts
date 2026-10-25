/**
 * Preparo da conexao no deviceStore, contra o simulador: o mesmo caminho vale
 * para o Bluetooth real (WebBleLink), que implementa a mesma interface.
 */

import { AppMessage } from '../src/device/protocol';
import { onDeviceReady, simulator, useDeviceStore } from '../src/store/deviceStore';
import { useEventsStore } from '../src/store/eventsStore';

jest.mock('@react-native-async-storage/async-storage', () =>
  require('@react-native-async-storage/async-storage/jest/async-storage-mock'),
);

/** Anota tudo o que o app manda ao comedouro. */
function espionaEnvios(): AppMessage[] {
  const enviadas: AppMessage[] = [];
  const link = useDeviceStore.getState().link;
  const original = link.send.bind(link);
  jest.spyOn(link, 'send').mockImplementation(async (msg) => {
    enviadas.push(msg);
    return original(msg);
  });
  return enviadas;
}

async function conectar() {
  const p = useDeviceStore.getState().connect();
  await jest.advanceTimersByTimeAsync(3000);
  await p;
}

describe('deviceStore: preparo da conexao', () => {
  beforeAll(() => {
    useDeviceStore.getState().init();
  });

  beforeEach(() => {
    jest.useFakeTimers();
  });

  afterEach(async () => {
    await useDeviceStore.getState().disconnect();
    jest.restoreAllMocks();
    jest.useRealTimers();
  });

  it('conectar manda hello, cfg e sync, nessa ordem, e depois roda os ganchos', async () => {
    const enviadas = espionaEnvios();
    const gancho = jest.fn(async () => {});
    const cancelar = onDeviceReady(gancho);

    await conectar();
    cancelar();

    expect(useDeviceStore.getState().connection).toBe('connected');
    expect(enviadas.map((m) => m.t).slice(0, 3)).toEqual(['hello', 'cfg', 'sync']);
    expect(gancho).toHaveBeenCalledTimes(1);
  });

  it('erro num gancho nao derruba a conexao', async () => {
    const aviso = jest.spyOn(console, 'warn').mockImplementation(() => {});
    const cancelar = onDeviceReady(async () => {
      throw new Error('falhou de proposito');
    });

    await conectar();
    cancelar();

    expect(useDeviceStore.getState().connection).toBe('connected');
    expect(aviso).toHaveBeenCalled();
  });

  it('reconexao feita pelo transporte tambem prepara a sessao e roda os ganchos', async () => {
    await conectar();

    const gancho = jest.fn(async () => {});
    const cancelar = onDeviceReady(gancho);
    const enviadas = espionaEnvios();

    // queda e volta sem passar pelo connect() do store, como o BLE faz sozinho
    simulator()!.dropConnection();
    expect(useDeviceStore.getState().connection).toBe('idle');

    const voltando = useDeviceStore.getState().link.connect();
    await jest.advanceTimersByTimeAsync(3000);
    await voltando;
    cancelar();

    expect(useDeviceStore.getState().connection).toBe('connected');
    expect(enviadas.map((m) => m.t).slice(0, 3)).toEqual(['hello', 'cfg', 'sync']);
    expect(gancho).toHaveBeenCalledTimes(1);
  });

  it('comedouro com contagem menor que a do app: rebaseia e pede o historico desde o zero', async () => {
    // o app ja viu eventos ate o seq 5000 (de outro comedouro, ou do simulador)
    useEventsStore.setState({ lastSeq: 5000 });
    const enviadas = espionaEnvios();

    await conectar();
    await jest.advanceTimersByTimeAsync(1000);

    const syncs = enviadas.filter(
      (m): m is Extract<AppMessage, { t: 'sync' }> => m.t === 'sync',
    );
    expect(syncs.map((s) => s.since)).toEqual([5000, 0]);
    expect(useEventsStore.getState().lastSeq).toBeLessThan(5000);
  });

  it('calibrate manda a massa de referencia em gramas inteiras', async () => {
    await conectar();
    const enviadas = espionaEnvios();

    const p = useDeviceStore.getState().calibrate(696.4);
    await jest.advanceTimersByTimeAsync(500);
    await p;

    expect(enviadas.at(-1)).toMatchObject({ t: 'cmd', op: 'calibrate', refG: 696 });
  });
});
