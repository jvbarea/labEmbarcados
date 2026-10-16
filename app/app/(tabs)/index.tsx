import { Redirect, useRouter } from 'expo-router';
import { useMemo, useState } from 'react';
import { Pressable, ScrollView, Text, View } from 'react-native';
import { BigButton, Card, CatAvatar, KeyValue, Row, SectionTitle, StatusPill, WeightDisplay } from '../../src/ui/components';
import { ConnectionPill, FeedbackBanner, OfflineBanner } from '../../src/ui/deviceViews';
import { useAction, useNow } from '../../src/ui/hooks';
import { doorLook, screen } from '../../src/ui/tokens';
import { useDeviceStore } from '../../src/store/deviceStore';
import { catLabel, useCatsStore } from '../../src/store/catsStore';
import { summarizeDay, useEventsStore } from '../../src/store/eventsStore';
import { PRESENCE_THRESHOLD_G } from '../../src/domain/rules';
import { formatCountdown, formatKg, startOfDay } from '../../src/domain/format';

export default function InicioScreen() {
  const configured = useCatsStore((s) => s.configured);
  const cats = useCatsStore((s) => s.cats);
  const status = useDeviceStore((s) => s.status);
  const connection = useDeviceStore((s) => s.connection);
  const settings = useDeviceStore((s) => s.settings);
  const events = useEventsStore((s) => s.events);
  const { run, feedback, isPending } = useAction();
  const now = useNow();
  const router = useRouter();
  const [refillStartedAt, setRefillStartedAt] = useState<number | null>(null);

  const hoje = startOfDay(now);
  const resumo = useMemo(() => summarizeDay(events, hoje), [events, hoje]);

  if (!configured) return <Redirect href="/primeiro-uso" />;

  const peso = status?.wG ?? 0;
  const naBalanca = peso >= PRESENCE_THRESHOLD_G;
  const gatoAtual = cats.find((c) => c.id === status?.cat);
  const porta = doorLook[status?.door ?? 'closed'];
  const grant = status?.grant ?? null;
  const gatoLiberado = cats.find((c) => c.id === grant?.cat);
  const bloqueado = cats.find((c) => c.rule === 'deny');
  const reabastecendo = refillStartedAt !== null && status?.door !== 'closed';
  const restanteReabastecer = refillStartedAt ? settings.maxOpenS - (now - refillStartedAt) : 0;

  const cartaoAoVivo = (
    <Card className="gap-3">
      <Row className="justify-between">
        <Text className={`flex-1 pr-2 ${screen.label}`}>Na balança agora</Text>
        <StatusPill label={porta.label} tone={porta.tone} icon={porta.icon as never} />
      </Row>

      <Row>
        {naBalanca && gatoAtual ? (
          <CatAvatar emoji={gatoAtual.emoji} color={gatoAtual.color} size="lg" />
        ) : (
          <View className="h-16 w-16 items-center justify-center rounded-full bg-slate-100 dark:bg-slate-800">
            <Text className="text-3xl">{naBalanca ? '❓' : '🍽️'}</Text>
          </View>
        )}
        <View className="flex-1">
          <WeightDisplay text={formatKg(peso)} muted={!naBalanca} />
          <Text className={screen.subtitle}>
            {!naBalanca ? 'Balança vazia' : gatoAtual ? catLabel(gatoAtual) : 'Visitante desconhecido'}
          </Text>
        </View>
      </Row>

      {grant && gatoLiberado ? (
        <View className="rounded-xl bg-amber-100 px-3 py-2 dark:bg-amber-900">
          <Text className="text-sm font-medium text-amber-900 dark:text-amber-100">
            {grant.untilS
              ? `${catLabel(gatoLiberado)} liberado por mais ${formatCountdown(grant.untilS - now)}`
              : `${catLabel(gatoLiberado)} liberado na próxima visita`}
          </Text>
        </View>
      ) : null}

      {reabastecendo ? (
        <View className="rounded-xl bg-sky-100 px-3 py-2 dark:bg-sky-900">
          <Text className="text-sm font-medium text-sky-900 dark:text-sky-100">
            Reabastecendo — fecha sozinha em {formatCountdown(Math.max(0, restanteReabastecer))}
          </Text>
        </View>
      ) : null}
    </Card>
  );

  const bannerAdaptacao =
    status?.mode === 'learn' ? (
      <Pressable accessibilityRole="button" onPress={() => router.push('/adaptacao')}>
        <View className="rounded-2xl bg-sky-100 p-4 dark:bg-sky-950">
          <Text className="text-base font-semibold text-sky-900 dark:text-sky-100">
            Modo adaptação ligado
          </Text>
          <Text className="text-sm text-sky-900 dark:text-sky-100">
            A porta fica aberta e cada visita é medida. Toque para ver as medições e identificar os
            gatos.
          </Text>
        </View>
      </Pressable>
    ) : null;

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      <Row className="justify-between">
        <Text className={screen.title}>Feed Cat</Text>
        <ConnectionPill />
      </Row>

      <OfflineBanner
        loading={isPending('conectar')}
        onConnect={() => run('conectar', () => useDeviceStore.getState().connect())}
      />

      <FeedbackBanner feedback={feedback} />

      {bannerAdaptacao}

      {cartaoAoVivo}

      <Card className="gap-2">
        <SectionTitle hint="Visitas registradas hoje">Resumo do dia</SectionTitle>
        {resumo.length === 0 ? (
          <Text className={screen.hint}>Nenhuma visita hoje ainda.</Text>
        ) : (
          resumo.map((linha) => {
            const gato = cats.find((c) => c.id === linha.cat);
            return (
              <View key={linha.cat} className="gap-1 py-1">
                <Row className="justify-between">
                  <Row>
                    <Text className="text-lg">{gato?.emoji ?? '❓'}</Text>
                    <Text className={screen.body}>{catLabel(gato, linha.cat)}</Text>
                  </Row>
                  <Text className={screen.hint}>
                    {linha.visits} {linha.visits === 1 ? 'visita' : 'visitas'}
                  </Text>
                </Row>
                <Row className="gap-1">
                  <View
                    className="h-2 rounded-full bg-emerald-400"
                    style={{ flex: Math.max(linha.opened, 0.001) }}
                  />
                  <View
                    className="h-2 rounded-full bg-rose-300"
                    style={{ flex: Math.max(linha.denied, 0.001) }}
                  />
                </Row>
                <Text className={screen.hint}>
                  {linha.opened} com porta aberta · {linha.denied} negadas
                </Text>
              </View>
            );
          })
        )}
      </Card>

      <Card className="gap-3">
        <SectionTitle hint="O comedouro executa e avisa o app">Ações</SectionTitle>

        {bloqueado ? (
          <View className="gap-2">
            <Text className={screen.label}>Liberar {catLabel(bloqueado)} (em dieta)</Text>
            <Row>
              <BigButton
                className="flex-1"
                title="Por 15 min"
                icon="time"
                variant="secondary"
                loading={isPending('grant-min')}
                disabled={connection !== 'connected'}
                onPress={() =>
                  run(
                    'grant-min',
                    () => useDeviceStore.getState().grant(bloqueado.id, { minutes: 15 }),
                    `${catLabel(bloqueado)} liberado por 15 minutos.`,
                  )
                }
              />
              <BigButton
                className="flex-1"
                title="1 visita"
                icon="repeat"
                variant="secondary"
                loading={isPending('grant-visit')}
                disabled={connection !== 'connected'}
                onPress={() =>
                  run(
                    'grant-visit',
                    () => useDeviceStore.getState().grant(bloqueado.id, { visits: 1 }),
                    `${catLabel(bloqueado)} liberado na próxima visita.`,
                  )
                }
              />
            </Row>
            {grant ? (
              <BigButton
                title="Cancelar liberação"
                variant="ghost"
                loading={isPending('revoke')}
                disabled={connection !== 'connected'}
                onPress={() =>
                  run('revoke', () => useDeviceStore.getState().revoke(grant.cat), 'Liberação cancelada.')
                }
              />
            ) : null}
          </View>
        ) : null}

        <View className="h-px bg-slate-200 dark:bg-slate-800" />

        {status?.door === 'open' || status?.door === 'opening' ? (
          <BigButton
            title="Fechar porta agora"
            icon="lock-closed"
            variant="danger"
            loading={isPending('fechar')}
            disabled={connection !== 'connected'}
            onPress={() =>
              run(
                'fechar',
                async () => {
                  await useDeviceStore.getState().closeDoor();
                  setRefillStartedAt(null);
                },
                'Fechando a porta.',
              )
            }
          />
        ) : (
          <BigButton
            title="Abrir para reabastecer"
            icon="restaurant"
            loading={isPending('reabastecer')}
            disabled={connection !== 'connected'}
            onPress={() =>
              run(
                'reabastecer',
                async () => {
                  await useDeviceStore.getState().refill(settings.maxOpenS);
                  setRefillStartedAt(Math.floor(Date.now() / 1000));
                },
                'Porta aberta para reabastecer.',
              )
            }
          />
        )}

        <KeyValue label="Fecha sozinha após" value={formatCountdown(settings.maxOpenS)} />
      </Card>
    </ScrollView>
  );
}
