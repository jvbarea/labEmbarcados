/**
 * Protocolo Feed Cat v2 (app <-> ESP32).
 *
 * Especificacao: ../../../plano/01-app.md secao 7.
 * Regras gerais:
 *  - pesos sempre em GRAMAS inteiras;
 *  - tempos em segundos (S) ou milissegundos (Ms) inteiros;
 *  - toda mensagem do app tem "id" e recebe um "ack" com o mesmo id;
 *  - o transporte (BLE agora, Wi-Fi depois) so carrega strings JSON.
 */

export const PROTOCOL_VERSION = 2;

/** Id do gato no dispositivo. 0 = visitante desconhecido. */
export type CatKey = 0 | 1 | 2;

export type Rule = 'allow' | 'deny';

export type DoorState = 'closed' | 'opening' | 'open' | 'closing' | 'fault';

export type DoorOutcome = 'opened' | 'denied';

export type DecisionReason = 'rule' | 'grant' | 'unknown' | 'manual' | 'learn';

/**
 * Modo de operacao do comedouro.
 *  - 'auto'  : regra normal (abre so para quem pode)
 *  - 'learn' : modo adaptacao/identificacao: ninguem e bloqueado e cada visita
 *              vira uma amostra de peso. O simulador deixa a porta aberta o
 *              tempo todo; o comedouro real, cujo servo precisa segurar a
 *              tampa, abre a cada visita e fecha quando o gato sai.
 */
export type DeviceMode = 'auto' | 'learn';

// ---------------------------------------------------------------- app -> ESP

export interface DeviceCat {
  /** Id curto do gato (1 ou 2). */
  k: Exclude<CatKey, 0>;
  minG: number;
  maxG: number;
  rule: Rule;
}

/** Liberacao temporaria: por prazo (untilS) ou por numero de visitas. */
export interface GrantSpec {
  cat: Exclude<CatKey, 0>;
  untilS?: number;
  visits?: number;
}

export type AppMessage =
  | { t: 'hello'; id: number; v: number; now: number }
  /**
   * Dados que so o app entende (nomes, emoji, cores), guardados no comedouro
   * como texto opaco. Servem para um celular novo recuperar o cadastro.
   */
  | { t: 'profile'; id: number; blob: string }
  /** Pede ao comedouro a configuracao que ele tem guardada. */
  | { t: 'cfgGet'; id: number }
  | { t: 'cats'; id: number; cats: DeviceCat[] }
  | ({ t: 'grant'; id: number } & GrantSpec)
  | { t: 'revoke'; id: number; cat: Exclude<CatKey, 0> }
  | { t: 'cmd'; id: number; op: 'refill'; timeoutS: number }
  | { t: 'cmd'; id: number; op: 'close' | 'tare' }
  | { t: 'cmd'; id: number; op: 'calibrate'; refG: number }
  | { t: 'cfg'; id: number; confMs: number; leaveMs: number; maxOpenS: number }
  | { t: 'mode'; id: number; mode: DeviceMode }
  | { t: 'sync'; id: number; since: number };

// ---------------------------------------------------------------- ESP -> app

export interface GrantState {
  cat: Exclude<CatKey, 0>;
  untilS?: number;
  visits?: number;
}

export interface StatusMsg {
  t: 'st';
  /** Peso atual na balanca, em gramas. */
  wG: number;
  /** Gato reconhecido pelo peso (0 = ninguem/desconhecido). */
  cat: CatKey;
  door: DoorState;
  /** Fim de curso FECHADO / ABERTO. */
  fcC: 0 | 1;
  fcO: 0 | 1;
  grant: GrantState | null;
  mode: DeviceMode;
  /** Versao da configuracao guardada no comedouro; muda a cada gravacao. */
  cfgV: number;
}

/** Resposta ao cfgGet: tudo o que o comedouro tem na flash. */
export interface CfgDumpMsg {
  t: 'cfgDump';
  cats: DeviceCat[];
  confMs: number;
  leaveMs: number;
  maxOpenS: number;
  mode: DeviceMode;
  cfgV: number;
  profile?: string;
}

export interface EventMsg {
  t: 'ev';
  /** Numero de sequencia crescente, usado para sincronizar o historico. */
  seq: number;
  /** Epoch em segundos. */
  ts: number;
  kind: 'visit' | 'refill' | 'fault' | 'boot';
  cat?: CatKey;
  wG?: number;
  durS?: number;
  door?: DoorOutcome;
  why?: DecisionReason;
  err?: string;
  /** 1 quando a visita foi registrada no modo adaptacao (amostra de peso). */
  learn?: 0 | 1;
}

export type DeviceMessage =
  | { t: 'ack'; id: number; ok: boolean; err?: string }
  | StatusMsg
  | CfgDumpMsg
  | EventMsg
  | { t: 'syncEnd'; id: number; last: number };

export type AckMsg = Extract<DeviceMessage, { t: 'ack' }>;

// ------------------------------------------------------------------- limites

export const LIMITS = {
  /** Faixa aceita para o peso de um gato. */
  catWeightG: { min: 500, max: 15000 },
  /** A tolerancia e fixa (ver FIXED_TOLERANCE_G em domain/cats.ts). */
  toleranceG: { min: 500, max: 500 },
  /** Tempo que o peso precisa ficar estavel para abrir. */
  confMs: { min: 500, max: 10000 },
  /** Tempo sem gato antes de fechar. */
  leaveMs: { min: 1000, max: 30000 },
  /** Tempo maximo com a porta aberta no modo reabastecer. */
  refillS: { min: 30, max: 1800 },
} as const;

// ------------------------------------------------------------------ helpers

const isInt = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v);
const isFlag = (v: unknown): v is 0 | 1 => v === 0 || v === 1;
const isCatKey = (v: unknown): v is CatKey => v === 0 || v === 1 || v === 2;

const DOOR_STATES: DoorState[] = ['closed', 'opening', 'open', 'closing', 'fault'];

function parseDeviceCats(v: unknown): DeviceCat[] | null {
  if (!Array.isArray(v)) return null;
  const out: DeviceCat[] = [];
  for (const item of v) {
    if (typeof item !== 'object' || item === null) return null;
    const c = item as Record<string, unknown>;
    if (c.k !== 1 && c.k !== 2) return null;
    if (!isInt(c.minG) || !isInt(c.maxG)) return null;
    if (c.rule !== 'allow' && c.rule !== 'deny') return null;
    out.push({ k: c.k, minG: c.minG, maxG: c.maxG, rule: c.rule });
  }
  return out;
}

function parseGrantState(v: unknown): GrantState | null {
  if (v === null || v === undefined) return null;
  if (typeof v !== 'object') return null;
  const g = v as Record<string, unknown>;
  if (g.cat !== 1 && g.cat !== 2) return null;
  const out: GrantState = { cat: g.cat };
  if (isInt(g.untilS)) out.untilS = g.untilS;
  if (isInt(g.visits)) out.visits = g.visits;
  return out;
}

/** Serializa uma mensagem do app. */
export function encodeAppMessage(msg: AppMessage): string {
  return JSON.stringify(msg);
}

/**
 * Le uma mensagem vinda do dispositivo.
 * Retorna null quando o texto nao e um JSON valido ou nao segue o protocolo,
 * para que ruido na linha nunca derrube o app.
 */
export function decodeDeviceMessage(raw: string): DeviceMessage | null {
  let data: unknown;
  try {
    data = JSON.parse(raw);
  } catch {
    return null;
  }
  if (typeof data !== 'object' || data === null) return null;
  const m = data as Record<string, unknown>;

  switch (m.t) {
    case 'ack': {
      if (!isInt(m.id) || typeof m.ok !== 'boolean') return null;
      const ack: AckMsg = { t: 'ack', id: m.id, ok: m.ok };
      if (typeof m.err === 'string') ack.err = m.err;
      return ack;
    }
    case 'st': {
      if (!isInt(m.wG) || !isCatKey(m.cat)) return null;
      if (typeof m.door !== 'string' || !DOOR_STATES.includes(m.door as DoorState)) return null;
      if (!isFlag(m.fcC) || !isFlag(m.fcO)) return null;
      return {
        t: 'st',
        wG: m.wG,
        cat: m.cat,
        door: m.door as DoorState,
        fcC: m.fcC,
        fcO: m.fcO,
        grant: parseGrantState(m.grant),
        mode: m.mode === 'learn' ? 'learn' : 'auto',
        cfgV: isInt(m.cfgV) ? m.cfgV : 0,
      };
    }
    case 'cfgDump': {
      const cats = parseDeviceCats(m.cats);
      if (!cats) return null;
      if (!isInt(m.confMs) || !isInt(m.leaveMs) || !isInt(m.maxOpenS)) return null;
      const dump: CfgDumpMsg = {
        t: 'cfgDump',
        cats,
        confMs: m.confMs,
        leaveMs: m.leaveMs,
        maxOpenS: m.maxOpenS,
        mode: m.mode === 'learn' ? 'learn' : 'auto',
        cfgV: isInt(m.cfgV) ? m.cfgV : 0,
      };
      if (typeof m.profile === 'string') dump.profile = m.profile;
      return dump;
    }
    case 'ev': {
      if (!isInt(m.seq) || !isInt(m.ts)) return null;
      if (m.kind !== 'visit' && m.kind !== 'refill' && m.kind !== 'fault' && m.kind !== 'boot') {
        return null;
      }
      const ev: EventMsg = { t: 'ev', seq: m.seq, ts: m.ts, kind: m.kind };
      if (isCatKey(m.cat)) ev.cat = m.cat;
      if (isInt(m.wG)) ev.wG = m.wG;
      if (isInt(m.durS)) ev.durS = m.durS;
      if (m.door === 'opened' || m.door === 'denied') ev.door = m.door;
      if (
        m.why === 'rule' ||
        m.why === 'grant' ||
        m.why === 'unknown' ||
        m.why === 'manual' ||
        m.why === 'learn'
      ) {
        ev.why = m.why;
      }
      if (isFlag(m.learn)) ev.learn = m.learn;
      if (typeof m.err === 'string') ev.err = m.err;
      return ev;
    }
    case 'syncEnd': {
      if (!isInt(m.id) || !isInt(m.last)) return null;
      return { t: 'syncEnd', id: m.id, last: m.last };
    }
    default:
      return null;
  }
}

/**
 * Mensagens de erro traduzidas para a tela. Cobre os codigos que o firmware
 * manda em ack.err e ev.err (teste_componentes/teste_esp/firmware_ble) e os do
 * proprio app (timeout, disconnected).
 */
const ERROR_TEXT: Record<string, string> = {
  // porta
  door_fault: 'A porta está em falha. Rearme pela aba Comedouro.',
  door_timeout: 'A porta não chegou ao fim do curso a tempo.',
  door_moving: 'Espere a porta parar e tente de novo.',
  // balança
  sensor_fault: 'A balança parou de responder. Confira os fios do HX711.',
  scale_mute: 'A balança não respondeu. Confira os fios do HX711.',
  calib_failed: 'A calibração falhou. A massa de referência está no centro da plataforma?',
  no_scale: 'Esta balança é simulada; não há o que calibrar.',
  missing_ref: 'Informe a massa de referência em gramas.',
  real_scale: 'Este comedouro usa a balança real; o peso não pode ser simulado.',
  // cadastro e configuração
  unknown_cat: 'O comedouro não conhece esse gato. Envie o cadastro de novo.',
  no_clock: 'O comedouro ainda está sem hora. Reconecte e tente de novo.',
  bad_range: 'Valor fora da faixa aceita pelo comedouro.',
  bad_cats: 'O cadastro dos gatos chegou incompleto. Envie de novo.',
  too_big: 'A mensagem ficou grande demais para o comedouro.',
  // protocolo
  bad_json: 'O comedouro recebeu uma mensagem corrompida. Tente de novo.',
  bad_type: 'O comedouro não reconheceu o comando. O firmware está atualizado?',
  bad_op: 'O comedouro não reconheceu o comando. O firmware está atualizado?',
  bad_cmd: 'O comedouro não reconheceu o comando. O firmware está atualizado?',
  // app
  busy: 'O comedouro está ocupado com outro comando.',
  timeout: 'O comedouro não respondeu.',
  disconnected: 'Sem conexão com o comedouro.',
};

export function describeError(err: string | undefined): string {
  if (!err) return 'Não foi possível completar a ação.';
  return ERROR_TEXT[err] ?? `Não foi possível completar a ação (${err}).`;
}
