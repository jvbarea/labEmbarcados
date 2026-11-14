/**
 * Regra de acesso do comedouro.
 *
 * Esta mesma logica roda na ESP32: o app usa para prever o resultado na tela
 * e o simulador usa para se comportar como o dispositivo real.
 */

import { CatKey, DecisionReason, GrantState } from '../device/protocol';
import { Cat, rangeOf } from './cats';

export interface Decision {
  /** Gato reconhecido pelo peso (0 = ninguem reconhecido). */
  cat: CatKey;
  allowed: boolean;
  why: DecisionReason;
}

/** Peso minimo para considerar que tem alguem na balanca. */
export const PRESENCE_THRESHOLD_G = 300;

/** Identifica o gato pelo peso. Retorna 0 quando nenhuma faixa bate. */
export function identify(weightG: number, cats: Cat[]): CatKey {
  if (weightG < PRESENCE_THRESHOLD_G) return 0;
  const match = cats.find((cat) => {
    const { minG, maxG } = rangeOf(cat);
    return weightG >= minG && weightG <= maxG;
  });
  return match ? match.id : 0;
}

/** A liberacao vale para este gato agora? */
export function grantApplies(grant: GrantState | null, cat: CatKey, nowS: number): boolean {
  if (!grant || cat === 0 || grant.cat !== cat) return false;
  if (grant.untilS !== undefined && nowS >= grant.untilS) return false;
  if (grant.visits !== undefined && grant.visits <= 0) return false;
  return true;
}

/** Decide se a porta deve abrir para quem esta na balanca. */
export function decide(
  weightG: number,
  cats: Cat[],
  grant: GrantState | null,
  nowS: number,
): Decision {
  const cat = identify(weightG, cats);

  if (cat === 0) {
    return { cat: 0, allowed: false, why: 'unknown' };
  }
  if (grantApplies(grant, cat, nowS)) {
    return { cat, allowed: true, why: 'grant' };
  }
  const registrado = cats.find((c) => c.id === cat);
  return { cat, allowed: registrado?.rule === 'allow', why: 'rule' };
}

/** Consome uma visita da liberacao por visitas; devolve o novo estado (ou null se acabou). */
export function consumeGrant(grant: GrantState | null): GrantState | null {
  if (!grant || grant.visits === undefined) return grant;
  const restantes = grant.visits - 1;
  return restantes > 0 ? { ...grant, visits: restantes } : null;
}

/** A liberacao expirou por tempo? */
export function grantExpired(grant: GrantState | null, nowS: number): boolean {
  if (!grant) return false;
  if (grant.untilS !== undefined && nowS >= grant.untilS) return true;
  if (grant.visits !== undefined && grant.visits <= 0) return true;
  return false;
}
