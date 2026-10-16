import { Link, useRouter } from 'expo-router';
import { useState } from 'react';
import { Pressable, ScrollView, Text, View } from 'react-native';
import { BigButton, Card, KeyValue, Row, SectionTitle, StatusPill } from '../../src/ui/components';
import { ConnectionPill, FeedbackBanner } from '../../src/ui/deviceViews';
import { useAction } from '../../src/ui/hooks';
import { doorLook, screen } from '../../src/ui/tokens';
import { simulator, useDeviceStore } from '../../src/store/deviceStore';
import { useCatsStore } from '../../src/store/catsStore';
import { useEventsStore } from '../../src/store/eventsStore';
import { formatDuration, formatKg } from '../../src/domain/format';

export default function ComedouroScreen() {
  const status = useDeviceStore((s) => s.status);
  const connection = useDeviceStore((s) => s.connection);
  const settings = useDeviceStore((s) => s.settings);
  const cats = useCatsStore((s) => s.cats);
  const eventos = useEventsStore((s) => s.events.length);
  const { run, feedback, isPending } = useAction();
  const [confirmandoReset, setConfirmandoReset] = useState(false);
  const sim = simulator();
  const router = useRouter();
  const porta = doorLook[status?.door ?? 'closed'];
  const conectado = connection === 'connected';
  const emAdaptacao = status?.mode === 'learn';

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      <FeedbackBanner feedback={feedback} />

      <Card className="gap-3">
        <Row className="justify-between">
          <SectionTitle className="flex-1 pr-2" hint={useDeviceStore.getState().link.label}>
            Conexão
          </SectionTitle>
          <ConnectionPill />
        </Row>
        {conectado ? (
          <BigButton
            title="Desconectar"
            icon="close-circle"
            variant="ghost"
            loading={isPending('desconectar')}
            onPress={() => run('desconectar', () => useDeviceStore.getState().disconnect())}
          />
        ) : (
          <BigButton
            title="Conectar"
            icon="bluetooth"
            loading={isPending('conectar')}
            onPress={() =>
              run('conectar', async () => {
                await useDeviceStore.getState().connect();
                await useCatsStore.getState().syncToDevice();
              })
            }
          />
        )}
      </Card>

      <Card className="gap-3">
        <Row className="justify-between">
          <SectionTitle className="flex-1 pr-2" hint="Como o comedouro está se comportando agora">
            Modo
          </SectionTitle>
          <StatusPill
            label={emAdaptacao ? 'Adaptação' : 'Automático'}
            tone={emAdaptacao ? 'info' : 'success'}
            icon={emAdaptacao ? 'school' : 'flash'}
          />
        </Row>
        <Text className={screen.hint}>
          {emAdaptacao
            ? 'Porta aberta o tempo todo, registrando o peso de quem sobe na balança. Nenhum gato é bloqueado.'
            : 'A porta abre só para quem pode comer, seguindo o cadastro dos gatos e as liberações.'}
        </Text>
        <Link href="/adaptacao" asChild>
          <Pressable accessibilityRole="button" className="self-start">
            <Text className="text-sm font-semibold text-marca-600 dark:text-marca-300">
              {emAdaptacao ? 'Ver medições e identificar os gatos →' : 'Abrir modo adaptação →'}
            </Text>
          </Pressable>
        </Link>
      </Card>

      <Card className="gap-2">
        <Row className="justify-between">
          <SectionTitle className="flex-1 pr-2">Porta</SectionTitle>
          <StatusPill label={porta.label} tone={porta.tone} icon={porta.icon as never} />
        </Row>
        <KeyValue label="Fim de curso FECHADO" value={status?.fcC ? 'acionado' : 'livre'} />
        <KeyValue label="Fim de curso ABERTO" value={status?.fcO ? 'acionado' : 'livre'} />
        {status?.door === 'fault' ? (
          <BigButton
            title="Rearmar porta"
            icon="refresh"
            variant="danger"
            loading={isPending('rearmar')}
            disabled={!conectado}
            onPress={() =>
              run('rearmar', () => useDeviceStore.getState().closeDoor(), 'Porta rearmada.')
            }
          />
        ) : null}
      </Card>

      <Card className="gap-2">
        <SectionTitle hint="Zere com a balança vazia">Balança</SectionTitle>
        <KeyValue label="Leitura atual" value={formatKg(status?.wG ?? 0)} />
        <BigButton
          title="Zerar (tara)"
          icon="speedometer"
          variant="secondary"
          loading={isPending('tara')}
          disabled={!conectado}
          onPress={() => run('tara', () => useDeviceStore.getState().tare(), 'Balança zerada.')}
        />
      </Card>

      <Card className="gap-3">
        <SectionTitle hint="Estes tempos são enviados ao comedouro e valem mesmo sem o celular por perto">
          Ajustes de comportamento
        </SectionTitle>

        <Ajuste
          titulo="Tempo de confirmação"
          valor={formatDuration(settings.confMs / 1000)}
          explicacao="Quanto tempo o peso precisa ficar estável antes da porta abrir. Evita que a porta abra quando o gato só passa por cima da balança."
        />
        <Ajuste
          titulo="Espera para fechar"
          valor={formatDuration(settings.leaveMs / 1000)}
          explicacao="Quanto tempo o comedouro espera, depois que a balança fica vazia, antes de fechar. Evita fechar quando o gato só deu um passo para trás."
        />
        <Ajuste
          titulo="Duração do reabastecimento"
          valor={formatDuration(settings.maxOpenS)}
          explicacao="Quando você abre a porta para colocar ração, ela fecha sozinha depois deste tempo — assim ela nunca fica aberta por esquecimento."
        />

        <View className="h-px bg-slate-200 dark:bg-slate-800" />

        <KeyValue label="Gatos cadastrados" value={String(cats.filter((c) => c.name.trim()).length)} />
        <KeyValue label="Eventos no histórico" value={String(eventos)} />
        <BigButton
          title="Reenviar cadastro dos gatos"
          icon="cloud-upload"
          variant="secondary"
          loading={isPending('sync')}
          disabled={!conectado}
          onPress={() =>
            run('sync', () => useCatsStore.getState().syncToDevice(), 'Cadastro enviado ao comedouro.')
          }
        />
      </Card>

      <Card className="gap-2">
        <SectionTitle hint="Fica guardado no próprio aparelho">Dados do app</SectionTitle>
        <Text className={screen.hint}>
          Apaga os gatos cadastrados, o histórico de visitas e as medições da adaptação. O app volta
          à tela de boas-vindas, como se fosse a primeira vez.
        </Text>
        <BigButton
          title={confirmandoReset ? 'Tem certeza? Apagar tudo' : 'Apagar dados do app'}
          icon={confirmandoReset ? 'warning' : 'trash'}
          variant={confirmandoReset ? 'danger' : 'ghost'}
          loading={isPending('reset')}
          onPress={() => {
            if (!confirmandoReset) {
              setConfirmandoReset(true);
              return;
            }
            void run('reset', async () => {
              await useCatsStore.getState().resetAll();
              setConfirmandoReset(false);
              router.replace('/');
            });
          }}
        />
        {confirmandoReset ? (
          <BigButton
            title="Cancelar"
            variant="secondary"
            onPress={() => setConfirmandoReset(false)}
          />
        ) : null}
      </Card>

      {sim ? (
        <Card className="gap-3 border-dashed border-marca-400">
          <SectionTitle hint="Só existe no simulador; some quando o BLE real entrar">
            Painel do simulador
          </SectionTitle>

          <Text className={screen.label}>Colocar na balança</Text>
          <Row>
            {cats.map((cat) => (
              <BigButton
                key={cat.id}
                className="flex-1"
                title={cat.name.trim() || `Gato ${cat.id}`}
                variant="secondary"
                onPress={() => sim.setWeight(cat.weightG)}
              />
            ))}
          </Row>
          <Row>
            <BigButton
              className="flex-1"
              title="Desconhecido"
              variant="secondary"
              onPress={() => sim.setWeight(1100)}
            />
            <BigButton
              className="flex-1"
              title="Tirar da balança"
              variant="ghost"
              onPress={() => sim.clear()}
            />
          </Row>

          <View className="h-px bg-slate-200 dark:bg-slate-800" />

          <Row>
            <BigButton
              className="flex-1"
              title={sim.isJammed() ? 'Destravar porta' : 'Travar porta'}
              variant="danger"
              onPress={() => sim.jam(!sim.isJammed())}
            />
            <BigButton
              className="flex-1"
              title="Cair conexão"
              variant="ghost"
              onPress={() => sim.dropConnection()}
            />
          </Row>
          <BigButton
            title="Gerar histórico de exemplo (inclui hoje)"
            icon="sparkles"
            variant="secondary"
            onPress={() => sim.seedHistory(2)}
          />
          <BigButton
            title="Gerar medições do modo adaptação"
            icon="school"
            variant="secondary"
            onPress={() => {
              sim.seedLearning(12);
              router.push('/adaptacao');
            }}
          />
          <Text className={screen.hint}>
            O simulador aplica as mesmas regras que a ESP32 vai aplicar (identificação por peso,
            liberações e fins de curso).
          </Text>
        </Card>
      ) : null}
    </ScrollView>
  );
}

/** Uma linha de ajuste: nome, valor e explicação em português claro. */
function Ajuste({
  titulo,
  valor,
  explicacao,
}: {
  titulo: string;
  valor: string;
  explicacao: string;
}) {
  return (
    <View className="gap-0.5">
      <Row className="justify-between">
        <Text className={screen.label}>{titulo}</Text>
        <Text className="text-sm font-semibold text-slate-900 dark:text-slate-50">{valor}</Text>
      </Row>
      <Text className={screen.hint}>{explicacao}</Text>
    </View>
  );
}
