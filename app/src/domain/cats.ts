/** Modelo dos gatos no app (nome, emoji e cor ficam so aqui; o dispositivo recebe faixas). */

import { DeviceCat, LIMITS, Rule } from '../device/protocol';

export type CatId = 1 | 2;

/** Chaves de cor; as classes Tailwind correspondentes estao em src/ui/catColors.ts. */
export type CatColorKey = 'laranja' | 'azul' | 'roxo' | 'rosa' | 'verde' | 'cinza';

export interface Cat {
  id: CatId;
  name: string;
  emoji: string;
  color: CatColorKey;
  /** Peso tipico do gato, em gramas. */
  weightG: number;
  /** Margem para cima e para baixo, em gramas. */
  toleranceG: number;
  rule: Rule;
}

export interface WeightRange {
  minG: number;
  maxG: number;
}

/**
 * Tolerancia fixa: a faixa de cada gato e o peso dele +/- 500 g.
 * Fica fixa de proposito — um numero a menos para o usuario decidir errado.
 */
export const FIXED_TOLERANCE_G = 500;

/** @deprecated use FIXED_TOLERANCE_G */
export const DEFAULT_TOLERANCE_G = FIXED_TOLERANCE_G;

export const CAT_EMOJIS = ['🐱', '🐈', '🐈‍⬛', '😺', '😸', '🙀', '🐯', '🦁'];

export const CAT_COLOR_KEYS: CatColorKey[] = ['laranja', 'azul', 'roxo', 'rosa', 'verde', 'cinza'];

export function makeDefaultCat(id: CatId): Cat {
  return id === 1
    ? {
        id: 1,
        name: '',
        emoji: '🐱',
        color: 'laranja',
        weightG: 3200,
        toleranceG: FIXED_TOLERANCE_G,
        rule: 'allow',
      }
    : {
        id: 2,
        name: '',
        emoji: '🐈',
        color: 'azul',
        weightG: 6500,
        toleranceG: FIXED_TOLERANCE_G,
        rule: 'deny',
      };
}

/** Faixa de peso aceita para identificar o gato (peso +/- tolerancia fixa). */
export function rangeOf(cat: Pick<Cat, 'weightG' | 'toleranceG'>): WeightRange {
  const tol = cat.toleranceG || FIXED_TOLERANCE_G;
  return {
    minG: Math.max(0, cat.weightG - tol),
    maxG: cat.weightG + tol,
  };
}

export function rangesOverlap(a: WeightRange, b: WeightRange): boolean {
  return a.minG <= b.maxG && b.minG <= a.maxG;
}

/** Converte para o formato enviado ao dispositivo. */
export function toDeviceCat(cat: Cat): DeviceCat {
  const range = rangeOf(cat);
  return { k: cat.id, minG: range.minG, maxG: range.maxG, rule: cat.rule };
}

export interface CatErrors {
  name?: string;
  weightG?: string;
  toleranceG?: string;
  range?: string;
}

/**
 * Valida um gato contra os limites e contra os outros gatos ja cadastrados.
 * Faixas sobrepostas sao barradas: o comedouro nao teria como distinguir os dois.
 */
export function validateCat(cat: Cat, others: Cat[]): CatErrors {
  const errors: CatErrors = {};

  if (!cat.name.trim()) {
    errors.name = 'Dê um nome ao gato.';
  } else if (cat.name.trim().length > 20) {
    errors.name = 'No máximo 20 caracteres.';
  }

  if (!Number.isFinite(cat.weightG)) {
    errors.weightG = 'Informe o peso.';
  } else if (cat.weightG < LIMITS.catWeightG.min || cat.weightG > LIMITS.catWeightG.max) {
    errors.weightG = `O peso deve ficar entre ${LIMITS.catWeightG.min / 1000} kg e ${
      LIMITS.catWeightG.max / 1000
    } kg.`;
  }

  if (!errors.weightG) {
    const range = rangeOf(cat);
    const conflito = others.find((o) => o.id !== cat.id && rangesOverlap(range, rangeOf(o)));
    if (conflito) {
      errors.range = `A faixa encosta na do ${
        conflito.name.trim() || 'outro gato'
      }. Com a faixa fixa de ± ${FIXED_TOLERANCE_G} g, os gatos precisam ter pelo menos ${
        (FIXED_TOLERANCE_G * 2) / 1000
      } kg de diferença.`;
    }
  }

  return errors;
}

export function hasErrors(errors: CatErrors): boolean {
  return Object.keys(errors).length > 0;
}
