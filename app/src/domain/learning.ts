/**
 * Modo adaptacao: separar as medicoes de peso em dois gatos.
 *
 * No modo adaptacao a porta abre para qualquer gato e o comedouro so anota
 * quanto pesava quem subiu na balanca. Depois de alguns dias, estas funcoes separam as
 * medicoes em dois grupos (os dois gatos) para o usuario dar nome a cada um.
 *
 * A separacao usa k-means de uma dimensao com k=2 — e a versao mais simples que
 * resolve, e o mesmo resultado poderia ser calculado na ESP32 se um dia fizer
 * sentido tirar isso do app.
 */

import { FIXED_TOLERANCE_G } from './cats';

export interface WeightSample {
  wG: number;
  ts: number;
}

export interface Cluster {
  /** Peso representativo do grupo (mediana, menos sensivel a leitura torta). */
  centerG: number;
  minG: number;
  maxG: number;
  count: number;
  samples: WeightSample[];
}

export interface ClusterResult {
  clusters: Cluster[];
  /** Distancia entre os centros dos dois grupos. */
  separationG: number;
  /** Da para usar este resultado para cadastrar os dois gatos? */
  usable: boolean;
  problem?: string;
}

/** Minimo de visitas para a separacao fazer sentido. */
export const MIN_SAMPLES = 6;
/** Minimo de visitas em cada grupo. */
export const MIN_PER_CLUSTER = 3;

function median(values: number[]): number {
  if (values.length === 0) return 0;
  const ord = [...values].sort((a, b) => a - b);
  const meio = Math.floor(ord.length / 2);
  return ord.length % 2 ? ord[meio] : Math.round((ord[meio - 1] + ord[meio]) / 2);
}

function buildCluster(samples: WeightSample[]): Cluster {
  const pesos = samples.map((s) => s.wG);
  return {
    centerG: median(pesos),
    minG: Math.min(...pesos),
    maxG: Math.max(...pesos),
    count: samples.length,
    samples: [...samples].sort((a, b) => a.wG - b.wG),
  };
}

/**
 * Separa as amostras em dois grupos.
 * Retorna tambem se o resultado da para usar: precisa de amostras suficientes,
 * dos dois grupos terem visitas e de estarem longe o bastante um do outro para
 * as faixas fixas (+/- 500 g) nao se sobreporem.
 */
export function clusterWeights(samples: WeightSample[]): ClusterResult {
  const validas = samples.filter((s) => Number.isFinite(s.wG) && s.wG > 0);

  if (validas.length < MIN_SAMPLES) {
    return {
      clusters: validas.length ? [buildCluster(validas)] : [],
      separationG: 0,
      usable: false,
      problem: `Ainda são poucas medições (${validas.length} de ${MIN_SAMPLES}).`,
    };
  }

  const ordenadas = [...validas].sort((a, b) => a.wG - b.wG);

  // chutes iniciais: as duas pontas
  let c1 = ordenadas[0].wG;
  let c2 = ordenadas[ordenadas.length - 1].wG;

  let grupo1: WeightSample[] = [];
  let grupo2: WeightSample[] = [];

  for (let iteracao = 0; iteracao < 30; iteracao++) {
    grupo1 = [];
    grupo2 = [];
    ordenadas.forEach((s) => {
      (Math.abs(s.wG - c1) <= Math.abs(s.wG - c2) ? grupo1 : grupo2).push(s);
    });
    if (grupo1.length === 0 || grupo2.length === 0) break;

    const n1 = median(grupo1.map((s) => s.wG));
    const n2 = median(grupo2.map((s) => s.wG));
    if (n1 === c1 && n2 === c2) break;
    c1 = n1;
    c2 = n2;
  }

  if (grupo1.length === 0 || grupo2.length === 0) {
    return {
      clusters: [buildCluster(ordenadas)],
      separationG: 0,
      usable: false,
      problem: 'Todas as medições ficaram parecidas — parece um gato só.',
    };
  }

  const clusters = [buildCluster(grupo1), buildCluster(grupo2)].sort(
    (a, b) => a.centerG - b.centerG,
  );
  const separationG = clusters[1].centerG - clusters[0].centerG;

  let problem: string | undefined;
  if (clusters.some((c) => c.count < MIN_PER_CLUSTER)) {
    problem = `Um dos gatos tem poucas visitas (mínimo ${MIN_PER_CLUSTER} em cada).`;
  } else if (separationG < FIXED_TOLERANCE_G * 2) {
    problem = `Os dois pesos estão muito próximos (${(separationG / 1000)
      .toFixed(2)
      .replace('.', ',')} kg). Com a faixa fixa de ± ${FIXED_TOLERANCE_G} g, o comedouro não conseguiria distinguir.`;
  }

  return { clusters, separationG, usable: !problem, problem };
}

/** Distribuicao das medicoes em faixas de 250 g, para o grafico da tela. */
export function histogram(samples: WeightSample[], binG = 250): { fromG: number; count: number }[] {
  if (samples.length === 0) return [];
  const pesos = samples.map((s) => s.wG);
  const min = Math.floor(Math.min(...pesos) / binG) * binG;
  const max = Math.ceil(Math.max(...pesos) / binG) * binG;
  const bins: { fromG: number; count: number }[] = [];
  for (let from = min; from <= max; from += binG) {
    bins.push({ fromG: from, count: pesos.filter((p) => p >= from && p < from + binG).length });
  }
  return bins;
}
