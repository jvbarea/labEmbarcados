/** Formatacao em portugues do Brasil. */

/** 3240 -> "3,24 kg" */
export function formatKg(grams: number, decimals = 2): string {
  const kg = grams / 1000;
  return `${kg.toFixed(decimals).replace('.', ',')} kg`;
}

/** 3240 -> "3,2" (para campos de formulario) */
export function gramsToKgInput(grams: number): string {
  return (grams / 1000).toFixed(2).replace(/0$/, '').replace('.', ',');
}

/** "3,2" -> 3200 ; devolve NaN quando o texto nao e um numero valido. */
export function kgInputToGrams(text: string): number {
  const normalizado = text.trim().replace(',', '.');
  if (!/^\d*\.?\d*$/.test(normalizado) || normalizado === '' || normalizado === '.') return NaN;
  const kg = Number.parseFloat(normalizado);
  if (!Number.isFinite(kg)) return NaN;
  return Math.round(kg * 1000);
}

/** 250 -> "4 min 10 s" ; 40 -> "40 s" */
export function formatDuration(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  if (s < 60) return `${s} s`;
  const min = Math.floor(s / 60);
  const rest = s % 60;
  if (min < 60) return rest === 0 ? `${min} min` : `${min} min ${rest} s`;
  const h = Math.floor(min / 60);
  const minRest = min % 60;
  return minRest === 0 ? `${h} h` : `${h} h ${minRest} min`;
}

/** Contagem regressiva curta: 125 -> "2:05" */
export function formatCountdown(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  const min = Math.floor(s / 60);
  const rest = s % 60;
  return `${min}:${rest.toString().padStart(2, '0')}`;
}

/** Epoch em segundos -> "14:32" */
export function formatTime(epochS: number): string {
  const d = new Date(epochS * 1000);
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`;
}

const DIAS = ['domingo', 'segunda', 'terça', 'quarta', 'quinta', 'sexta', 'sábado'];

/** Epoch em segundos -> "Hoje", "Ontem" ou "quarta, 16/09" */
export function formatDayLabel(epochS: number, nowS: number = Date.now() / 1000): string {
  const dia = startOfDay(epochS);
  const hoje = startOfDay(nowS);
  const umDia = 86400;
  if (dia === hoje) return 'Hoje';
  if (dia === hoje - umDia) return 'Ontem';
  const d = new Date(epochS * 1000);
  const dd = d.getDate().toString().padStart(2, '0');
  const mm = (d.getMonth() + 1).toString().padStart(2, '0');
  return `${DIAS[d.getDay()]}, ${dd}/${mm}`;
}

/** Epoch (s) do inicio do dia local. */
export function startOfDay(epochS: number): number {
  const d = new Date(epochS * 1000);
  d.setHours(0, 0, 0, 0);
  return Math.floor(d.getTime() / 1000);
}

/** Hora local (0-23) de um epoch em segundos. */
export function hourOf(epochS: number): number {
  return new Date(epochS * 1000).getHours();
}

export function nowS(): number {
  return Math.floor(Date.now() / 1000);
}
