import { useRouter } from 'expo-router';
import { useMemo, useState } from 'react';
import { Pressable, ScrollView, Text, TextInput, View } from 'react-native';
import { BigButton, Card, CatAvatar, KeyValue, Row, SectionTitle, StatusPill } from '../src/ui/components';
import { FeedbackBanner, OfflineBanner } from '../src/ui/deviceViews';
import { useAction } from '../src/ui/hooks';
import { screen } from '../src/ui/tokens';
import { simulator, useDeviceStore } from '../src/store/deviceStore';
import { useCatsStore } from '../src/store/catsStore';
import { learningSamples, useEventsStore } from '../src/store/eventsStore';
import { Cluster, MIN_SAMPLES, clusterWeights, histogram } from '../src/domain/learning';
import { CAT_EMOJIS, Cat, CatId, FIXED_TOLERANCE_G, makeDefaultCat } from '../src/domain/cats';
import { formatKg } from '../src/domain/format';

export default function AdaptacaoScreen() {
  const router = useRouter();
  const status = useDeviceStore((s) => s.status);
  const connection = useDeviceStore((s) => s.connection);
  const events = useEventsStore((s) => s.events);
  const cats = useCatsStore((s) => s.cats);
  const { run, feedback, isPending } = useAction();
  const conexao = useAction();
  const sim = simulator();

  const amostras = useMemo(() => learningSamples(events), [events]);
  const resultado = useMemo(() => clusterWeights(amostras), [amostras]);
  const barras = useMemo(() => histogram(amostras), [amostras]);
  const maxBarra = Math.max(1, ...barras.map((b) => b.count));

  const emAdaptacao = status?.mode === 'learn';
  const conectado = connection === 'connected';

  // nomes escolhidos para cada grupo identificado
  const [nomes, setNomes] = useState<string[]>(['', '']);
  const [emojis, setEmojis] = useState<string[]>(['🐱', '🐈']);

  async function salvarIdentificacao() {
    const store = useCatsStore.getState();
    const clusters = resultado.clusters;

    for (let i = 0; i < 2; i++) {
      const id = (i + 1) as CatId;
      const base = cats.find((c) => c.id === id) ?? makeDefaultCat(id);
      const novo: Cat = {
        ...base,
        name: nomes[i].trim(),
        emoji: emojis[i],
        weightG: clusters[i].centerG,
        toleranceG: FIXED_TOLERANCE_G,
        // o mais leve costuma ser o que está em dieta; o usuário troca depois se quiser
        rule: i === 0 ? 'allow' : 'deny',
      };
      await store.saveCat(novo);
    }

    await useDeviceStore.getState().setMode('auto');
    router.replace('/gatos');
  }

  const podeSalvar = resultado.usable && nomes.every((n) => n.trim().length > 0);

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      {/* o erro de conexao aparece no proprio aviso, por isso usa outro useAction */}
      <OfflineBanner
        idleText="Conecte ao comedouro para ligar ou desligar o modo adaptação."
        loading={conexao.isPending('conectar')}
        onConnect={() => conexao.run('conectar', () => useDeviceStore.getState().connect())}
      />

      <FeedbackBanner feedback={feedback} />

      <Card className="gap-3">
        <Row className="justify-between">
          <SectionTitle
            className="flex-1 pr-2"
            hint="A porta abre para qualquer gato e a balança anota quem sobe"
          >
            Modo adaptação
          </SectionTitle>
          <StatusPill
            label={emAdaptacao ? 'Ligado' : 'Desligado'}
            tone={emAdaptacao ? 'info' : 'neutral'}
            icon={emAdaptacao ? 'radio' : 'pause'}
          />
        </Row>

        <Text className={screen.body}>
          Deixe o comedouro neste modo por alguns dias. A porta abre para qualquer gato, eles
          aprendem que ali tem comida e a balança registra o peso de cada visita. Depois o app separa
          as medições em dois grupos e você dá nome a cada um — sem precisar pesar os gatos na mão.
        </Text>

        {emAdaptacao ? (
          <BigButton
            title="Encerrar adaptação"
            icon="stop-circle"
            variant="secondary"
            loading={isPending('modo')}
            disabled={!conectado}
            onPress={() =>
              run(
                'modo',
                () => useDeviceStore.getState().setMode('auto'),
                'Modo adaptação encerrado. A porta volta a obedecer as regras.',
              )
            }
          />
        ) : (
          <BigButton
            title="Iniciar adaptação"
            icon="play"
            loading={isPending('modo')}
            disabled={!conectado}
            onPress={() =>
              run(
                'modo',
                () => useDeviceStore.getState().setMode('learn'),
                'Modo adaptação ligado: a porta abre para qualquer gato e as visitas são registradas.',
              )
            }
          />
        )}
      </Card>

      <Card className="gap-3">
        <SectionTitle hint={`Mínimo de ${MIN_SAMPLES} visitas para identificar`}>
          Medições coletadas: {amostras.length}
        </SectionTitle>

        {barras.length === 0 ? (
          <Text className={screen.hint}>
            Nenhuma medição ainda. Ligue o modo adaptação e espere os gatos subirem na balança.
          </Text>
        ) : (
          <View className="gap-1">
            <View className="h-28 flex-row items-end gap-1">
              {barras.map((b) => (
                <View key={b.fromG} className="flex-1 items-center">
                  <View
                    className={`w-full rounded-t ${
                      b.count > 0 ? 'bg-marca-500' : 'bg-slate-200 dark:bg-slate-800'
                    }`}
                    style={{ height: Math.max(3, (b.count / maxBarra) * 90) }}
                  />
                </View>
              ))}
            </View>
            <Row className="justify-between">
              <Text className={screen.hint}>{formatKg(barras[0].fromG, 1)}</Text>
              <Text className={screen.hint}>peso medido</Text>
              <Text className={screen.hint}>{formatKg(barras[barras.length - 1].fromG + 250, 1)}</Text>
            </Row>
          </View>
        )}
      </Card>

      {resultado.clusters.length === 2 ? (
        <Card className="gap-4">
          <SectionTitle
            hint={
              resultado.usable
                ? `Os dois grupos estão separados por ${formatKg(resultado.separationG, 2)}`
                : undefined
            }
          >
            Gatos identificados
          </SectionTitle>

          {resultado.problem ? (
            <View className="rounded-xl bg-amber-100 px-3 py-2 dark:bg-amber-900">
              <Text className="text-sm text-amber-900 dark:text-amber-100">{resultado.problem}</Text>
            </View>
          ) : null}

          {resultado.clusters.map((grupo: Cluster, i: number) => (
            <View key={i} className="gap-2 rounded-xl border border-slate-200 p-3 dark:border-slate-700">
              <Row>
                <CatAvatar emoji={emojis[i]} color={i === 0 ? 'laranja' : 'azul'} />
                <View className="flex-1">
                  <Text className="text-base font-semibold text-slate-900 dark:text-slate-50">
                    Grupo {i + 1} — {formatKg(grupo.centerG)}
                  </Text>
                  <Text className={screen.hint}>
                    {grupo.count} visitas · de {formatKg(grupo.minG)} a {formatKg(grupo.maxG)}
                  </Text>
                </View>
              </Row>

              <TextInput
                className={screen.input}
                value={nomes[i]}
                onChangeText={(t) => setNomes((atual) => atual.map((n, idx) => (idx === i ? t : n)))}
                placeholder={`Nome do gato ${i + 1}`}
                placeholderTextColor="#94a3b8"
                maxLength={20}
              />

              <View className="flex-row flex-wrap gap-2">
                {CAT_EMOJIS.slice(0, 6).map((emoji) => (
                  <Pressable
                    key={emoji}
                    accessibilityRole="button"
                    onPress={() => setEmojis((atual) => atual.map((e, idx) => (idx === i ? emoji : e)))}
                    className={`h-10 w-10 items-center justify-center rounded-lg border ${
                      emojis[i] === emoji
                        ? 'border-marca-500 bg-marca-100 dark:bg-marca-900'
                        : 'border-slate-200 dark:border-slate-700'
                    }`}
                  >
                    <Text className="text-xl">{emoji}</Text>
                  </Pressable>
                ))}
              </View>

              <KeyValue
                label="Faixa que será usada"
                value={`${formatKg(Math.max(0, grupo.centerG - FIXED_TOLERANCE_G))} — ${formatKg(
                  grupo.centerG + FIXED_TOLERANCE_G,
                )}`}
              />
            </View>
          ))}

          <BigButton
            title="Salvar cadastro dos dois gatos"
            icon="checkmark-circle"
            disabled={!podeSalvar}
            loading={isPending('salvar')}
            onPress={() => run('salvar', salvarIdentificacao)}
          />
          <Text className={screen.hint}>
            O mais leve entra como Liberado e o mais pesado como Em dieta. Dá para trocar depois na
            aba Gatos.
          </Text>
        </Card>
      ) : null}

      <Card className="gap-2">
        <SectionTitle>Prefere cadastrar na mão?</SectionTitle>
        <Text className={screen.hint}>
          O modo manual continua disponível: informe o peso de cada gato direto no cadastro.
        </Text>
        <BigButton
          title="Cadastrar manualmente"
          icon="create"
          variant="ghost"
          onPress={() => router.push('/gato/1')}
        />
      </Card>

      {sim ? (
        <Card className="gap-2 border-dashed border-marca-400">
          <SectionTitle hint="Atalho do simulador">Gerar medições de exemplo</SectionTitle>
          <BigButton
            title="Simular 12 visitas de adaptação"
            icon="sparkles"
            variant="secondary"
            onPress={() => sim.seedLearning(12)}
          />
        </Card>
      ) : null}
    </ScrollView>
  );
}
