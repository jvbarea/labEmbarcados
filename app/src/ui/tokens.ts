/**
 * Classes Tailwind com significado fixo.
 *
 * Tailwind so enxerga classes escritas por extenso, entao elas ficam aqui em
 * strings completas (nunca montadas com template) e as telas escolhem pela
 * chave.
 */

import { DoorState } from '../device/protocol';
import { CatColorKey } from '../domain/cats';

export const catColor: Record<
  CatColorKey,
  { bg: string; text: string; ring: string; dot: string; soft: string }
> = {
  laranja: {
    bg: 'bg-amber-500',
    text: 'text-amber-700 dark:text-amber-300',
    ring: 'border-amber-300 dark:border-amber-700',
    dot: 'bg-amber-500',
    soft: 'bg-amber-100 dark:bg-amber-950',
  },
  azul: {
    bg: 'bg-sky-500',
    text: 'text-sky-700 dark:text-sky-300',
    ring: 'border-sky-300 dark:border-sky-700',
    dot: 'bg-sky-500',
    soft: 'bg-sky-100 dark:bg-sky-950',
  },
  roxo: {
    bg: 'bg-violet-500',
    text: 'text-violet-700 dark:text-violet-300',
    ring: 'border-violet-300 dark:border-violet-700',
    dot: 'bg-violet-500',
    soft: 'bg-violet-100 dark:bg-violet-950',
  },
  rosa: {
    bg: 'bg-rose-500',
    text: 'text-rose-700 dark:text-rose-300',
    ring: 'border-rose-300 dark:border-rose-700',
    dot: 'bg-rose-500',
    soft: 'bg-rose-100 dark:bg-rose-950',
  },
  verde: {
    bg: 'bg-emerald-500',
    text: 'text-emerald-700 dark:text-emerald-300',
    ring: 'border-emerald-300 dark:border-emerald-700',
    dot: 'bg-emerald-500',
    soft: 'bg-emerald-100 dark:bg-emerald-950',
  },
  cinza: {
    bg: 'bg-slate-500',
    text: 'text-slate-700 dark:text-slate-300',
    ring: 'border-slate-300 dark:border-slate-700',
    dot: 'bg-slate-500',
    soft: 'bg-slate-100 dark:bg-slate-800',
  },
};

export const doorLook: Record<DoorState, { label: string; tone: PillTone; icon: string }> = {
  closed: { label: 'Fechada', tone: 'neutral', icon: 'lock-closed' },
  opening: { label: 'Abrindo…', tone: 'info', icon: 'arrow-up-circle' },
  open: { label: 'Aberta', tone: 'success', icon: 'lock-open' },
  closing: { label: 'Fechando…', tone: 'info', icon: 'arrow-down-circle' },
  fault: { label: 'Falha', tone: 'danger', icon: 'warning' },
};

/** Porta quando o app nao sabe o estado: sem conexao, nao da para afirmar "fechada". */
export const doorLookOffline: { label: string; tone: PillTone; icon: string } = {
  label: 'Sem conexão',
  tone: 'neutral',
  icon: 'cloud-offline',
};

export type PillTone = 'neutral' | 'success' | 'warning' | 'danger' | 'info';

export const pillBox: Record<PillTone, string> = {
  neutral: 'bg-slate-200 dark:bg-slate-700',
  success: 'bg-emerald-100 dark:bg-emerald-900',
  warning: 'bg-amber-100 dark:bg-amber-900',
  danger: 'bg-rose-100 dark:bg-rose-900',
  info: 'bg-sky-100 dark:bg-sky-900',
};

export const pillText: Record<PillTone, string> = {
  neutral: 'text-slate-700 dark:text-slate-200',
  success: 'text-emerald-800 dark:text-emerald-200',
  warning: 'text-amber-800 dark:text-amber-200',
  danger: 'text-rose-800 dark:text-rose-200',
  info: 'text-sky-800 dark:text-sky-200',
};

/** Cor literal para icones (Ionicons nao aceita className). */
export const pillIconColor: Record<PillTone, string> = {
  neutral: '#475569',
  success: '#047857',
  warning: '#b45309',
  danger: '#be123c',
  info: '#0369a1',
};

export const screen = {
  page: 'flex-1 bg-slate-50 dark:bg-slate-950',
  content: 'px-4 pb-10 pt-2 gap-4',
  title: 'text-2xl font-bold text-slate-900 dark:text-slate-50',
  subtitle: 'text-base text-slate-500 dark:text-slate-400',
  label: 'text-sm font-medium text-slate-600 dark:text-slate-300',
  body: 'text-base text-slate-700 dark:text-slate-200',
  hint: 'text-xs text-slate-500 dark:text-slate-400',
  error: 'text-sm text-rose-600 dark:text-rose-400',
  input:
    'h-12 rounded-xl border border-slate-300 bg-white px-3 text-base text-slate-900 dark:border-slate-700 dark:bg-slate-900 dark:text-slate-50',
};
