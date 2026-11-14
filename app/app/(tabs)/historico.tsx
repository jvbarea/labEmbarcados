import { useMemo, useState } from 'react';
import { Pressable, ScrollView, Text, View } from 'react-native';
import { Card, EmptyState, SectionTitle, StatusPill } from '../../src/ui/components';
import { screen } from '../../src/ui/tokens';
import { catLabel, useCatsStore } from '../../src/store/catsStore';
import { useDeviceStore } from '../../src/store/deviceStore';
import { groupByDay, useEventsStore, visitsByHour } from '../../src/store/eventsStore';
import { formatDayLabel, formatDuration, formatKg, formatTime, nowS, startOfDay } from '../../src/domain/format';
import { EventMsg } from '../../src/device/protocol';

type Filtro = 'todos' | 1 | 2 | 0;

export default function HistoricoScreen() {
  const events = useEventsStore((s) => s.events);
  const cats = useCatsStore((s) => s.cats);
  const [filtro, setFiltro] = useState<Filtro>('todos');
  const noSimulador = useDeviceStore((s) => s.linkKind === 'mock');

  // Filtro por gato vale so para visitas: reabastecer, falha e boot nao sao "de" ninguem.
  const filtrados = useMemo(
    () =>
      filtro === 'todos'
        ? events
        : events.filter((e) => e.kind === 'visit' && (e.cat ?? 0) === filtro),
    [events, filtro],
  );
  const dias = useMemo(() => groupByDay(filtrados), [filtrados]);
  // O grafico acompanha o filtro.
  const horas = useMemo(() => visitsByHour(filtrados, startOfDay(nowS())), [filtrados]);
  const maxHora = Math.max(1, ...horas);

  const opcoes: { key: Filtro; label: string }[] = [
    { key: 'todos', label: 'Todos' },
    ...cats.map((c) => ({ key: c.id as Filtro, label: catLabel(c) })),
    { key: 0 as Filtro, label: 'Desconhecido' },
  ];

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      <Card className="gap-3">
        <SectionTitle hint="Visitas por hora hoje">Atividade de hoje</SectionTitle>
        <View className="h-24 flex-row items-end gap-0.5">
          {horas.map((qtd, hora) => (
            <View key={hora} className="flex-1 items-center gap-1">
              <View
                className={`w-full rounded-t ${qtd > 0 ? 'bg-marca-500' : 'bg-slate-200 dark:bg-slate-800'}`}
                style={{ height: Math.max(3, (qtd / maxHora) * 70) }}
              />
              {hora % 6 === 0 ? <Text className="text-[9px] text-slate-400">{hora}h</Text> : null}
            </View>
          ))}
        </View>
      </Card>

      <View className="flex-row flex-wrap gap-2">
        {opcoes.map((op) => (
          <Pressable
            key={String(op.key)}
            accessibilityRole="button"
            onPress={() => setFiltro(op.key)}
            className={`rounded-full border px-4 py-2 ${
              filtro === op.key
                ? 'border-marca-500 bg-marca-100 dark:bg-marca-900'
                : 'border-slate-200 dark:border-slate-700'
            }`}
          >
            <Text className={screen.label}>{op.label}</Text>
          </Pressable>
        ))}
      </View>

      {dias.length === 0 ? (
        <Card>
          <EmptyState
            icon="time-outline"
            title="Nenhuma visita registrada"
            description={
              noSimulador
                ? 'Use o painel do simulador na aba Comedouro para gerar visitas.'
                : 'As visitas aparecem aqui quando um gato sobe na balança do comedouro.'
            }
          />
        </Card>
      ) : (
        dias.map((dia) => (
          <Card key={dia.day} className="gap-2">
            <SectionTitle>{formatDayLabel(dia.day)}</SectionTitle>
            {dia.events.map((ev) => (
              <EventoLinha key={`${ev.seq}-${ev.ts}`} ev={ev} nome={nomeDoEvento(ev, cats)} />
            ))}
          </Card>
        ))
      )}
    </ScrollView>
  );
}

function nomeDoEvento(ev: EventMsg, cats: ReturnType<typeof useCatsStore.getState>['cats']): string {
  if (ev.kind === 'refill') return 'Reabastecimento';
  if (ev.kind === 'fault') return 'Falha na porta';
  if (ev.kind === 'boot') return 'Comedouro ligado';
  const cat = cats.find((c) => c.id === ev.cat);
  if (ev.learn === 1) return cat ? `${catLabel(cat)} · adaptação` : 'Amostra da adaptação';
  return cat ? catLabel(cat) : 'Visitante desconhecido';
}

function EventoLinha({ ev, nome }: { ev: EventMsg; nome: string }) {
  const abriu = ev.door === 'opened';
  const icone =
    ev.kind === 'visit' ? (abriu ? '🍽️' : '🚫') : ev.kind === 'refill' ? '🥣' : ev.kind === 'fault' ? '⚠️' : '🔌';

  return (
    <View className="flex-row items-center gap-3 border-b border-slate-100 py-2 last:border-b-0 dark:border-slate-800">
      <Text className="w-12 text-sm text-slate-500">{formatTime(ev.ts)}</Text>
      <Text className="text-lg">{icone}</Text>
      <View className="flex-1">
        <Text className="text-base font-medium text-slate-900 dark:text-slate-50">{nome}</Text>
        <Text className={screen.hint}>
          {[
            ev.wG !== undefined && ev.kind === 'visit' ? formatKg(ev.wG) : null,
            ev.durS !== undefined ? formatDuration(ev.durS) : null,
            ev.why === 'grant' ? 'liberação do app' : null,
            ev.err ? ev.err : null,
          ]
            .filter(Boolean)
            .join(' · ')}
        </Text>
      </View>
      {ev.kind === 'visit' ? (
        ev.learn === 1 ? (
          <StatusPill label="Medido" tone="info" />
        ) : (
          <StatusPill label={abriu ? 'Comeu' : 'Negado'} tone={abriu ? 'success' : 'warning'} />
        )
      ) : null}
    </View>
  );
}
