/**
 * Comedouro simulado.
 *
 * Implementa o mesmo protocolo e as mesmas regras que a ESP32 vai rodar, para
 * dar para desenvolver e apresentar o app sem a maquete. Os metodos em "sim"
 * sao os controles do painel do simulador (aba Comedouro) e nao existem no
 * BleLink.
 */

import {
  AppMessage,
  CfgDumpMsg,
  DeviceCat,
  DeviceMessage,
  DeviceMode,
  DoorState,
  EventMsg,
  GrantState,
  StatusMsg,
} from './protocol';
import { ConnectionHandler, DeviceLink, DeviceMessageHandler, LinkError, Unsubscribe } from './DeviceLink';
import { Cat, CatId } from '../domain/cats';
import { PRESENCE_THRESHOLD_G, consumeGrant, decide, grantExpired, identify } from '../domain/rules';

const TICK_MS = 250;
const STATUS_EVERY_MS = 500;
const ACK_DELAY_MS = 120;
/** Tempo que a porta leva para ir de um fim de curso ao outro. */
const TRAVEL_MS = 1500;
/** Depois disso sem chegar ao fim de curso, o dispositivo entra em falha. */
const TRAVEL_TIMEOUT_MS = 4000;
const MAX_EVENTS = 300;

interface MockConfig {
  confMs: number;
  leaveMs: number;
  maxOpenS: number;
}

const DEFAULT_CONFIG: MockConfig = { confMs: 2000, leaveMs: 4000, maxOpenS: 300 };

/** Converte os gatos do app para o formato que o "dispositivo" guarda. */
function asDomainCats(cats: DeviceCat[]): Cat[] {
  return cats.map((c) => ({
    id: c.k as CatId,
    name: `gato ${c.k}`,
    emoji: '🐱',
    color: 'laranja' as const,
    // rangeOf(weight, tolerance) precisa devolver exatamente minG..maxG
    weightG: Math.round((c.minG + c.maxG) / 2),
    toleranceG: Math.round((c.maxG - c.minG) / 2),
    rule: c.rule,
  }));
}

export class MockLink implements DeviceLink {
  readonly kind = 'mock' as const;
  readonly label = 'FELIS (simulador)';

  private connected = false;
  private timer: ReturnType<typeof setInterval> | null = null;
  private msgHandlers = new Set<DeviceMessageHandler>();
  private connHandlers = new Set<ConnectionHandler>();

  // estado do "dispositivo"
  private cats: DeviceCat[] = [];
  private config: MockConfig = { ...DEFAULT_CONFIG };
  /** Texto opaco do app (nomes, emoji, cores) guardado na "flash". */
  private profile: string | undefined;
  /** Sobe a cada gravacao de configuracao, como o cfgV da ESP32. */
  private cfgV = 0;
  private grant: GrantState | null = null;
  private mode: DeviceMode = 'auto';
  private door: DoorState = 'closed';
  private targetWeightG = 0;
  private weightG = 0;
  private tareOffsetG = 0;
  private seq = 0;
  private events: EventMsg[] = [];

  // temporizadores internos
  private travelStartedAt = 0;
  private jammed = false;
  private refillUntilMs = 0;
  private aboveSinceMs = 0;
  private belowSinceMs = 0;
  private lastStatusAt = 0;

  // visita em andamento
  private visitStartMs = 0;
  private visitCat: 0 | 1 | 2 = 0;
  private visitOpened = false;
  private visitWhy: EventMsg['why'] = 'unknown';

  // ------------------------------------------------------------ DeviceLink

  isConnected(): boolean {
    return this.connected;
  }

  async connect(): Promise<void> {
    await wait(300);
    this.connected = true;
    this.ensureRunning();
    this.emitConnection(true);
    this.pushEvent({ kind: 'boot' });
  }

  async disconnect(): Promise<void> {
    this.connected = false;
    this.emitConnection(false);
    // o dispositivo continua funcionando sozinho, so o app deixa de ver
  }

  async send(msg: AppMessage): Promise<void> {
    if (!this.connected) throw new LinkError('Sem conexão com o comedouro.', 'disconnected');
    await wait(ACK_DELAY_MS);
    this.handleAppMessage(msg);
  }

  onMessage(handler: DeviceMessageHandler): Unsubscribe {
    this.msgHandlers.add(handler);
    return () => this.msgHandlers.delete(handler);
  }

  onConnectionChange(handler: ConnectionHandler): Unsubscribe {
    this.connHandlers.add(handler);
    return () => this.connHandlers.delete(handler);
  }

  // ------------------------------------------------- controles do simulador

  readonly sim = {
    /** Coloca um peso na balanca (em gramas). */
    setWeight: (grams: number) => {
      this.targetWeightG = Math.max(0, Math.round(grams));
    },
    /** Tira tudo da balanca. */
    clear: () => {
      this.targetWeightG = 0;
    },
    /** Trava a porta: ela nao chega mais ao fim de curso e o comedouro vai para falha. */
    jam: (value = true) => {
      this.jammed = value;
    },
    isJammed: () => this.jammed,
    /** Simula queda do Bluetooth. */
    dropConnection: () => {
      if (!this.connected) return;
      this.connected = false;
      this.emitConnection(false);
    },
    /** Muda o modo direto no "dispositivo" (o app normalmente manda a mensagem). */
    setMode: (mode: DeviceMode) => this.setMode(mode),
    getMode: () => this.mode,
    /**
     * Gera historico de exemplo: os dias anteriores completos e o dia de hoje
     * ate a hora atual, para o grafico ja nascer com dados.
     */
    seedHistory: (days = 2) => {
      const agora = new Date();
      for (let d = days; d >= 0; d--) {
        const horarios = [7, 10, 13, 16, 19, 22].filter(
          (h) => d > 0 || h <= agora.getHours(),
        );
        for (const hora of horarios) {
          const dia = new Date(agora.getTime() - d * 86400_000);
          dia.setHours(hora, 5 + Math.floor(Math.random() * 50), 0, 0);
          if (dia.getTime() > agora.getTime()) continue;

          const ts = Math.floor(dia.getTime() / 1000);
          const magro = Math.random() > 0.35;
          this.pushEvent({
            kind: 'visit',
            ts,
            cat: magro ? 1 : 2,
            wG: magro ? 3180 + Math.round(Math.random() * 80) : 6440 + Math.round(Math.random() * 90),
            durS: magro ? 120 + Math.round(Math.random() * 240) : 15 + Math.round(Math.random() * 40),
            door: magro ? 'opened' : 'denied',
            why: 'rule',
          });
        }
      }
    },
    /**
     * Gera medicoes do modo adaptacao (dois gatos de pesos diferentes),
     * para nao ser preciso esperar dias para testar a identificacao.
     */
    seedLearning: (visits = 12) => {
      const agora = Date.now();
      for (let i = visits; i > 0; i--) {
        const magro = i % 2 === 0;
        const ts = Math.floor((agora - i * 3600_000) / 1000);
        this.pushEvent({
          kind: 'visit',
          ts,
          cat: 0,
          wG: magro
            ? 3150 + Math.round(Math.random() * 140)
            : 6400 + Math.round(Math.random() * 180),
          durS: 60 + Math.round(Math.random() * 200),
          door: 'opened',
          why: 'learn',
          learn: 1,
        });
      }
    },
    /** Esquece o historico guardado no "dispositivo" (usado ao apagar os dados do app). */
    clearEvents: () => {
      this.events = [];
    },
    /** Apaga tambem a configuracao, como uma ESP32 recem-gravada. */
    factoryReset: () => {
      this.events = [];
      this.cats = [];
      this.profile = undefined;
      this.grant = null;
      this.cfgV = 0;
      this.config = { ...DEFAULT_CONFIG };
      this.setMode('auto');
    },
    /** Estado interno, para a tela do simulador mostrar. */
    snapshot: () => ({
      weightG: this.weightG,
      targetWeightG: this.targetWeightG,
      door: this.door,
      mode: this.mode,
      jammed: this.jammed,
      grant: this.grant,
      events: this.events.length,
    }),
  };

  // ------------------------------------------------------------- internos

  private ensureRunning() {
    if (this.timer) return;
    this.timer = setInterval(() => this.tick(), TICK_MS);
  }

  private handleAppMessage(msg: AppMessage) {
    switch (msg.t) {
      case 'hello':
        this.ack(msg.id, true);
        this.emitStatus();
        break;

      case 'cats':
        this.cats = msg.cats;
        this.cfgV += 1;
        this.ack(msg.id, true);
        this.emitStatus();
        break;

      case 'profile':
        this.profile = msg.blob;
        this.cfgV += 1;
        this.ack(msg.id, true);
        break;

      case 'cfgGet': {
        const dump: CfgDumpMsg = {
          t: 'cfgDump',
          cats: this.cats,
          confMs: this.config.confMs,
          leaveMs: this.config.leaveMs,
          maxOpenS: this.config.maxOpenS,
          mode: this.mode,
          cfgV: this.cfgV,
          ...(this.profile !== undefined ? { profile: this.profile } : {}),
        };
        this.emit(dump);
        this.ack(msg.id, true);
        break;
      }

      case 'grant': {
        if (!this.cats.some((c) => c.k === msg.cat)) {
          this.ack(msg.id, false, 'unknown_cat');
          return;
        }
        const grant: GrantState = { cat: msg.cat };
        if (msg.untilS !== undefined) grant.untilS = msg.untilS;
        if (msg.visits !== undefined) grant.visits = msg.visits;
        this.grant = grant;
        this.ack(msg.id, true);
        this.emitStatus();
        break;
      }

      case 'revoke':
        if (this.grant?.cat === msg.cat) this.grant = null;
        this.ack(msg.id, true);
        this.emitStatus();
        break;

      case 'cmd':
        this.handleCommand(msg);
        break;

      case 'cfg':
        this.config = { confMs: msg.confMs, leaveMs: msg.leaveMs, maxOpenS: msg.maxOpenS };
        this.cfgV += 1;
        this.ack(msg.id, true);
        break;

      case 'mode':
        this.setMode(msg.mode);
        this.ack(msg.id, true);
        break;

      case 'sync': {
        const pendentes = this.events.filter((e) => e.seq > msg.since);
        this.ack(msg.id, true);
        pendentes.forEach((e) => this.emit(e));
        this.emit({ t: 'syncEnd', id: msg.id, last: this.seq });
        break;
      }
    }
  }

  private handleCommand(msg: Extract<AppMessage, { t: 'cmd' }>) {
    switch (msg.op) {
      case 'refill':
        if (this.door === 'fault') {
          this.ack(msg.id, false, 'door_fault');
          return;
        }
        this.refillUntilMs = Date.now() + msg.timeoutS * 1000;
        this.startMoving('opening');
        this.ack(msg.id, true);
        break;

      case 'close':
        this.refillUntilMs = 0;
        if (this.door === 'fault') {
          // fechar tambem serve de rearme
          this.jammed = false;
          this.door = 'closed';
          this.emitStatus();
          this.ack(msg.id, true);
          return;
        }
        if (this.door !== 'closed') this.startMoving('closing');
        this.ack(msg.id, true);
        break;

      case 'tare':
        this.tareOffsetG = this.targetWeightG;
        this.ack(msg.id, true);
        break;

      case 'calibrate':
        this.ack(msg.id, true);
        break;
    }
  }

  /**
   * No simulador, o modo adaptacao deixa a porta aberta o tempo todo: os gatos
   * aprendem que ali tem comida e a balanca so registra quanto cada um pesa.
   * O comedouro real segura a tampa com o servo, entao la a porta abre a cada
   * visita (para qualquer gato) e fecha quando ele sai. Por isso os textos das
   * telas falam em "abre para qualquer gato", que vale para os dois.
   */
  private setMode(mode: DeviceMode) {
    if (this.mode === mode) return;
    this.mode = mode;
    this.refillUntilMs = 0;
    if (this.door === 'fault') {
      this.emitStatus();
      return;
    }
    this.startMoving(mode === 'learn' ? 'opening' : 'closing');
    this.emitStatus();
  }

  private startMoving(state: 'opening' | 'closing') {
    if (this.door === state) return;
    this.door = state;
    this.travelStartedAt = Date.now();
    this.emitStatus();
  }

  private tick() {
    const now = Date.now();

    // 1) peso: aproxima do alvo com um pouco de ruido
    const alvo = Math.max(0, this.targetWeightG - this.tareOffsetG);
    const delta = alvo - this.weightG;
    this.weightG = Math.abs(delta) < 15 ? alvo : Math.round(this.weightG + delta * 0.45);
    if (this.weightG > 0) this.weightG += Math.round((Math.random() - 0.5) * 6);

    // 2) porta em movimento
    if (this.door === 'opening' || this.door === 'closing') {
      const decorrido = now - this.travelStartedAt;
      if (this.jammed) {
        if (decorrido >= TRAVEL_TIMEOUT_MS) {
          this.door = 'fault';
          this.pushEvent({ kind: 'fault', err: 'door_timeout' });
          this.emitStatus();
        }
      } else if (decorrido >= TRAVEL_MS) {
        this.door = this.door === 'opening' ? 'open' : 'closed';
        this.emitStatus();
      }
    }

    // 3) liberacao expirada
    if (grantExpired(this.grant, Math.floor(now / 1000))) {
      this.grant = null;
      this.emitStatus();
    }

    // 4) reabastecer com tempo esgotado
    if (this.refillUntilMs > 0 && now >= this.refillUntilMs) {
      this.refillUntilMs = 0;
      this.pushEvent({ kind: 'refill', durS: Math.round(this.config.maxOpenS) });
      if (this.door !== 'fault') this.startMoving('closing');
    }

    this.updateVisit(now);

    if (now - this.lastStatusAt >= STATUS_EVERY_MS) {
      this.lastStatusAt = now;
      this.emitStatus();
    }
  }

  /** Detecta inicio/fim de visita e aplica a regra de acesso. */
  private updateVisit(now: number) {
    const presente = this.weightG >= PRESENCE_THRESHOLD_G;
    const emReabastecimento = this.refillUntilMs > 0;

    if (presente) {
      this.belowSinceMs = 0;
      if (this.aboveSinceMs === 0) this.aboveSinceMs = now;

      const estavel = now - this.aboveSinceMs >= this.config.confMs;
      if (estavel && this.visitStartMs === 0) {
        const domainCats = asDomainCats(this.cats);
        const decisao = decide(this.weightG, domainCats, this.grant, Math.floor(now / 1000));
        this.visitStartMs = this.aboveSinceMs;

        if (this.mode === 'learn') {
          // porta ja esta aberta: so registra quem subiu e quanto pesava
          this.visitCat = decisao.cat;
          this.visitOpened = true;
          this.visitWhy = 'learn';
        } else {
          this.visitCat = decisao.cat;
          this.visitOpened = decisao.allowed;
          this.visitWhy = decisao.why;

          if (decisao.allowed) {
            if (decisao.why === 'grant') this.grant = consumeGrant(this.grant);
            if (this.door !== 'fault') this.startMoving('opening');
          }
        }
        this.emitStatus();
      }
      return;
    }

    // ninguem na balanca
    if (this.aboveSinceMs === 0) return;
    if (this.belowSinceMs === 0) this.belowSinceMs = now;

    if (now - this.belowSinceMs >= this.config.leaveMs) {
      if (this.visitStartMs > 0) {
        this.pushEvent({
          kind: 'visit',
          cat: this.visitCat,
          wG: this.peakWeightOfVisit(),
          durS: Math.round((this.belowSinceMs - this.visitStartMs) / 1000),
          door: this.visitOpened ? 'opened' : 'denied',
          why: this.visitWhy,
          ...(this.mode === 'learn' ? { learn: 1 as const } : {}),
        });
      }
      if (
        this.mode === 'auto' &&
        !emReabastecimento &&
        this.door !== 'fault' &&
        this.door !== 'closed'
      ) {
        this.startMoving('closing');
      }
      this.aboveSinceMs = 0;
      this.belowSinceMs = 0;
      this.visitStartMs = 0;
      this.visitCat = 0;
      this.visitOpened = false;
      this.emitStatus();
    }
  }

  private lastPeak = 0;

  private peakWeightOfVisit(): number {
    const peso = this.lastPeak || this.weightG;
    this.lastPeak = 0;
    return peso;
  }

  private pushEvent(partial: Omit<EventMsg, 't' | 'seq' | 'ts'> & { ts?: number }) {
    this.seq += 1;
    const ev: EventMsg = {
      t: 'ev',
      seq: this.seq,
      ts: partial.ts ?? Math.floor(Date.now() / 1000),
      kind: partial.kind,
      ...(partial.cat !== undefined ? { cat: partial.cat } : {}),
      ...(partial.wG !== undefined ? { wG: partial.wG } : {}),
      ...(partial.durS !== undefined ? { durS: partial.durS } : {}),
      ...(partial.door !== undefined ? { door: partial.door } : {}),
      ...(partial.why !== undefined ? { why: partial.why } : {}),
      ...(partial.err !== undefined ? { err: partial.err } : {}),
      ...(partial.learn !== undefined ? { learn: partial.learn } : {}),
    };
    this.events.push(ev);
    if (this.events.length > MAX_EVENTS) this.events.shift();
    this.emit(ev);
  }

  private emitStatus() {
    const cat = identify(this.weightG, asDomainCats(this.cats));
    if (this.weightG > this.lastPeak) this.lastPeak = this.weightG;
    const st: StatusMsg = {
      t: 'st',
      wG: this.weightG,
      cat,
      door: this.door,
      fcC: this.door === 'closed' ? 1 : 0,
      fcO: this.door === 'open' ? 1 : 0,
      grant: this.grant,
      mode: this.mode,
      cfgV: this.cfgV,
    };
    if (this.refillUntilMs > 0) {
      st.refillS = Math.max(0, Math.ceil((this.refillUntilMs - Date.now()) / 1000));
    }
    this.emit(st);
  }

  private ack(id: number, ok: boolean, err?: string) {
    this.emit(err ? { t: 'ack', id, ok, err } : { t: 'ack', id, ok });
  }

  private emit(msg: DeviceMessage) {
    if (!this.connected) return;
    this.msgHandlers.forEach((h) => h(msg));
  }

  private emitConnection(connected: boolean) {
    this.connHandlers.forEach((h) => h(connected));
  }
}

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
