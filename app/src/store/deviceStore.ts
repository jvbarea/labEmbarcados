/**
 * Conexao com o comedouro, status ao vivo e envio de comandos com confirmacao.
 *
 * As telas usam este store; ele fala com o DeviceLink (simulador ou BLE). Todo
 * comando espera o "ack" com o mesmo id e tem timeout, para a tela nunca ficar
 * pendurada.
 *
 * Toda conexao que da certo - botao Conectar de qualquer tela, o auto-connect
 * do bootstrap ou a reconexao automatica do transporte - passa pelo mesmo
 * preparo: hello -> cfg -> sync, e depois os ganchos registrados com
 * onDeviceReady (o bootstrap registra "recuperar cadastro e reenviar os gatos").
 */

import { create } from 'zustand';
import {
  AckMsg,
  AppMessage,
  CfgDumpMsg,
  DeviceMessage,
  DeviceMode,
  GrantState,
  PROTOCOL_VERSION,
  StatusMsg,
  describeError,
} from '../device/protocol';
import { DeviceLink } from '../device/DeviceLink';
import { MockLink } from '../device/MockLink';
import { LinkKind, createLink, defaultLinkKind, urlLinkOverride } from '../device/createLink';
import { Cat, toDeviceCat } from '../domain/cats';
import { STORAGE_KEYS, loadJson, saveJson } from '../data/storage';
import { nowS } from '../domain/format';
import { useEventsStore } from './eventsStore';

const ACK_TIMEOUT_MS = 4000;

export interface DeviceSettings {
  confMs: number;
  leaveMs: number;
  maxOpenS: number;
}

export const DEFAULT_SETTINGS: DeviceSettings = { confMs: 2000, leaveMs: 4000, maxOpenS: 300 };

export type ConnectionState = 'idle' | 'connecting' | 'connected' | 'error';

/** Omit que percorre cada membro da uniao de mensagens (Omit normal achata a uniao). */
type DistributiveOmit<T, K extends PropertyKey> = T extends unknown ? Omit<T, K> : never;

/** Mensagem pronta para enviar: o "id" e atribuido pelo store. */
export type OutgoingMessage = DistributiveOmit<AppMessage, 'id'> & { id?: number };

interface Pending {
  resolve: (ack: AckMsg) => void;
  reject: (err: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

interface DeviceStore {
  link: DeviceLink;
  /** Simulador ou comedouro real (o que esta em uso de fato). */
  linkKind: LinkKind;
  connection: ConnectionState;
  connectionError: string | null;
  status: StatusMsg | null;
  settings: DeviceSettings;
  lastAckAt: number | null;
  /** Ultima configuracao lida do comedouro (resposta do cfgGet). */
  cfgDump: CfgDumpMsg | null;

  init: () => void;
  loadSettings: () => Promise<void>;
  /** Le a escolha salva (simulador ou real) e carrega o historico daquele modo. */
  restoreLink: () => Promise<void>;
  /** Troca entre simulador e comedouro real; desconecta o anterior. */
  switchLink: (kind: LinkKind) => Promise<void>;
  /** Bluetooth nativo: esquece o comedouro salvo (a proxima conexao procura de novo). */
  forgetDevice: () => Promise<void>;
  connect: () => Promise<void>;
  disconnect: () => Promise<void>;
  send: (msg: OutgoingMessage) => Promise<AckMsg>;
  pushCats: (cats: Cat[]) => Promise<void>;
  grant: (cat: 1 | 2, opts: { minutes?: number; visits?: number }) => Promise<void>;
  revoke: (cat: 1 | 2) => Promise<void>;
  refill: (seconds: number) => Promise<void>;
  closeDoor: () => Promise<void>;
  tare: () => Promise<void>;
  /** Calibra a balanca com uma massa conhecida (em gramas) no centro da plataforma. */
  calibrate: (refG: number) => Promise<void>;
  setSettings: (settings: DeviceSettings) => Promise<void>;
  setMode: (mode: DeviceMode) => Promise<void>;
  /** Le do comedouro o que ele tem guardado na flash. */
  fetchConfig: () => Promise<CfgDumpMsg | null>;
  /** Guarda no comedouro o texto opaco com nomes/emoji/cores. */
  pushProfile: (blob: string) => Promise<void>;
}

let nextId = 1;
const pending = new Map<number, Pending>();
let unsubscribers: (() => void)[] = [];

/**
 * Transporte em uso: simulador ou comedouro real (ver createLink.ts). Comeca com
 * o palpite sincrono (URL na web, real no celular) e o bootstrap troca pela
 * escolha salva antes de conectar. Cada tipo e criado uma vez so: voltar ao
 * simulador reencontra o mesmo "comedouro" de antes, com o estado dele.
 */
let link: DeviceLink = createLink(urlLinkOverride() ?? defaultLinkKind());
const links: Partial<Record<LinkKind, DeviceLink>> = { [link.kind]: link };

function linkFor(kind: LinkKind): DeviceLink {
  const existente = links[kind];
  if (existente) return existente;
  const novo = createLink(kind);
  // Navegador sem Web Bluetooth: pediu o real e veio o simulador.
  links[novo.kind] ??= novo;
  return links[novo.kind]!;
}

/** Acesso aos controles do simulador (undefined quando o transporte for BLE). */
export function simulator(): MockLink['sim'] | undefined {
  return link instanceof MockLink ? link.sim : undefined;
}

function rejectPending(err: Error) {
  pending.forEach((entry) => {
    clearTimeout(entry.timer);
    entry.reject(err);
  });
  pending.clear();
}

function settleAck(ack: AckMsg) {
  const entry = pending.get(ack.id);
  if (!entry) return;
  clearTimeout(entry.timer);
  pending.delete(ack.id);
  entry.resolve(ack);
}

// ------------------------------------------------------------ pos-conexao

export type DeviceReadyHook = () => Promise<void>;

const readyHooks = new Set<DeviceReadyHook>();

/**
 * Registra algo para rodar depois de cada conexao preparada (hello, cfg e
 * sync ja confirmados). Devolve a funcao que cancela o registro.
 */
export function onDeviceReady(hook: DeviceReadyHook): () => void {
  readyHooks.add(hook);
  return () => {
    readyHooks.delete(hook);
  };
}

/** Preparo em andamento; impede dois preparos ao mesmo tempo. */
let preparing: Promise<void> | null = null;

/**
 * hello -> cfg -> sync, e depois os ganchos. Erro num gancho nao derruba a
 * conexao: fica no console e os outros ganchos continuam.
 */
function prepareSession(): Promise<void> {
  if (preparing) return preparing;

  preparing = (async () => {
    const store = useDeviceStore.getState();
    await store.send({ t: 'hello', v: PROTOCOL_VERSION, now: nowS() });
    await store.setSettings(store.settings);
    await store.send({ t: 'sync', since: useEventsStore.getState().lastSeq });

    for (const hook of readyHooks) {
      try {
        await hook();
      } catch (err) {
        console.warn('[device] tarefa pos-conexao falhou:', err);
      }
    }
  })().finally(() => {
    preparing = null;
  });

  return preparing;
}

/**
 * O comedouro diz que o ultimo evento dele e "last", mas o app ja viu um seq
 * maior: a contagem voltou (ESP regravada, ou troca do simulador para o
 * aparelho real no mesmo navegador). Sem isso, o app pediria "since" um numero
 * que o comedouro so alcancaria daqui a muito tempo e nada mais sincronizaria.
 */
function handleSyncEnd(last: number) {
  const events = useEventsStore.getState();
  if (last >= events.lastSeq) return;
  events.rebase(last);
  void useDeviceStore
    .getState()
    .send({ t: 'sync', since: 0 })
    .catch(() => undefined);
}

export const useDeviceStore = create<DeviceStore>((set, get) => ({
  link,
  linkKind: link.kind,
  connection: 'idle',
  connectionError: null,
  status: null,
  settings: DEFAULT_SETTINGS,
  lastAckAt: null,
  cfgDump: null,

  loadSettings: async () => {
    const saved = await loadJson<DeviceSettings>(STORAGE_KEYS.settings, DEFAULT_SETTINGS);
    set({ settings: { ...DEFAULT_SETTINGS, ...saved } });
  },

  init: () => {
    unsubscribers.forEach((fn) => fn());
    unsubscribers = [];

    unsubscribers.push(
      link.onMessage((msg: DeviceMessage) => {
        switch (msg.t) {
          case 'st':
            set({ status: msg });
            break;
          case 'ack':
            set({ lastAckAt: Date.now() });
            settleAck(msg);
            break;
          case 'cfgDump':
            set({ cfgDump: msg });
            break;
          case 'ev':
            useEventsStore.getState().ingest(msg);
            break;
          case 'syncEnd':
            settleAck({ t: 'ack', id: msg.id, ok: true });
            handleSyncEnd(msg.last);
            break;
        }
      }),
    );

    unsubscribers.push(
      link.onConnectionChange((connected) => {
        if (connected) {
          // connect() esta no meio do caminho e faz o preparo ele mesmo.
          if (get().connection === 'connecting') return;

          // Reconexao feita pelo proprio transporte (queda e volta do BLE):
          // prepara a sessao como se o usuario tivesse tocado em Conectar.
          set({ connection: 'connected', connectionError: null });
          prepareSession().catch((err) => {
            console.warn('[device] preparo da reconexao falhou:', err);
          });
          return;
        }

        set({ connection: 'idle', status: null });
        rejectPending(new Error(describeError('disconnected')));
      }),
    );
  },

  restoreLink: async () => {
    const salvo = await loadJson<LinkKind | null>(STORAGE_KEYS.link, null);
    const desejado = urlLinkOverride() ?? salvo ?? defaultLinkKind();
    if (desejado !== link.kind) {
      await get().switchLink(desejado);
    } else {
      await useEventsStore.getState().load(link.kind);
    }
  },

  switchLink: async (kind) => {
    const novo = linkFor(kind);
    await saveJson(STORAGE_KEYS.link, novo.kind);
    if (novo === link) return;

    // Desconecta o anterior com as assinaturas ainda ligadas: a tela ve a queda.
    try {
      await link.disconnect();
    } catch {
      // ja estava desconectado
    }
    rejectPending(new Error(describeError('disconnected')));

    link = novo;
    set({
      link,
      linkKind: link.kind,
      connection: link.isConnected() ? 'connected' : 'idle',
      connectionError: null,
      status: null,
      cfgDump: null,
    });
    get().init(); // assina as mensagens do transporte novo
    await useEventsStore.getState().load(link.kind);
  },

  forgetDevice: async () => {
    await link.forget?.();
    set({ connection: 'idle', status: null, connectionError: null });
  },

  connect: async () => {
    if (get().connection === 'connecting' || get().connection === 'connected') return;
    set({ connection: 'connecting', connectionError: null });
    try {
      await link.connect();
      set({ connection: 'connected' });
      await prepareSession();
    } catch (err) {
      set({
        connection: 'error',
        connectionError: err instanceof Error ? err.message : describeError(undefined),
      });
      throw err;
    }
  },

  disconnect: async () => {
    await link.disconnect();
    set({ connection: 'idle', status: null });
  },

  send: (msg) => {
    const id = msg.id ?? nextId++;
    const full = { ...msg, id } as AppMessage;

    return new Promise<AckMsg>((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(describeError('timeout')));
      }, ACK_TIMEOUT_MS);

      pending.set(id, { resolve, reject, timer });

      link.send(full).catch((err) => {
        clearTimeout(timer);
        pending.delete(id);
        reject(err instanceof Error ? err : new Error(describeError(undefined)));
      });
    }).then((ack) => {
      if (!ack.ok) throw new Error(describeError(ack.err));
      return ack;
    });
  },

  pushCats: async (cats) => {
    await get().send({ t: 'cats', cats: cats.map(toDeviceCat) });
  },

  grant: async (cat, opts) => {
    if (opts.minutes !== undefined) {
      await get().send({ t: 'grant', cat, untilS: nowS() + Math.round(opts.minutes * 60) });
      return;
    }
    await get().send({ t: 'grant', cat, visits: opts.visits ?? 1 });
  },

  revoke: async (cat) => {
    await get().send({ t: 'revoke', cat });
  },

  refill: async (seconds) => {
    await get().send({ t: 'cmd', op: 'refill', timeoutS: Math.round(seconds) });
  },

  closeDoor: async () => {
    await get().send({ t: 'cmd', op: 'close' });
  },

  tare: async () => {
    await get().send({ t: 'cmd', op: 'tare' });
  },

  calibrate: async (refG) => {
    await get().send({ t: 'cmd', op: 'calibrate', refG: Math.round(refG) });
  },

  setSettings: async (settings) => {
    set({ settings });
    await saveJson(STORAGE_KEYS.settings, settings);
    // Desconectado fica so salvo: vai para o comedouro no preparo da proxima conexao.
    if (get().connection !== 'connected') return;
    await get().send({ t: 'cfg', ...settings });
  },

  setMode: async (mode) => {
    await get().send({ t: 'mode', mode });
  },

  fetchConfig: async () => {
    set({ cfgDump: null });
    await get().send({ t: 'cfgGet' });
    return get().cfgDump;
  },

  pushProfile: async (blob) => {
    await get().send({ t: 'profile', blob });
  },
}));

/** Liberacao ativa (ou null). */
export function activeGrant(status: StatusMsg | null): GrantState | null {
  return status?.grant ?? null;
}
