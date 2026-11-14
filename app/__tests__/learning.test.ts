import { clusterWeights, histogram, MIN_SAMPLES } from '../src/domain/learning';

function amostras(pesos: number[]) {
  return pesos.map((wG, i) => ({ wG, ts: 1789574400 + i * 3600 }));
}

describe('identificação pelo modo adaptação', () => {
  it('separa dois gatos de pesos bem diferentes', () => {
    const r = clusterWeights(
      amostras([3180, 3240, 3200, 6450, 6510, 6480, 3220, 6490]),
    );

    expect(r.usable).toBe(true);
    expect(r.clusters).toHaveLength(2);
    expect(r.clusters[0].centerG).toBeGreaterThan(3100);
    expect(r.clusters[0].centerG).toBeLessThan(3300);
    expect(r.clusters[1].centerG).toBeGreaterThan(6400);
    expect(r.clusters[0].count).toBe(4);
    expect(r.clusters[1].count).toBe(4);
    expect(r.separationG).toBeGreaterThan(3000);
  });

  it('avisa quando ainda há poucas medições', () => {
    const r = clusterWeights(amostras([3200, 6500, 3210]));
    expect(r.usable).toBe(false);
    expect(r.problem).toContain(String(MIN_SAMPLES));
  });

  it('avisa quando os dois pesos estão próximos demais para a faixa fixa', () => {
    const r = clusterWeights(amostras([3200, 3250, 3180, 3600, 3650, 3620]));
    expect(r.usable).toBe(false);
    expect(r.problem).toMatch(/próximos/);
  });

  it('avisa quando um grupo tem visitas de menos', () => {
    const r = clusterWeights(amostras([3200, 3210, 3190, 3205, 3195, 6500]));
    expect(r.usable).toBe(false);
    expect(r.problem).toMatch(/poucas visitas/);
  });

  it('não quebra sem medições', () => {
    const r = clusterWeights([]);
    expect(r.clusters).toHaveLength(0);
    expect(r.usable).toBe(false);
    expect(histogram([])).toEqual([]);
  });

  it('monta o histograma em faixas de 250 g', () => {
    const bins = histogram(amostras([3200, 3300, 6500]));
    expect(bins[0].fromG).toBe(3000);
    expect(bins.reduce((soma, b) => soma + b.count, 0)).toBe(3);
  });
});
