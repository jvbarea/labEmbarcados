import { Link, useRouter } from 'expo-router';
import { useState } from 'react';
import { Pressable, ScrollView, Text, TextInput, View } from 'react-native';
import { BigButton, Card, KeyValue, Row, SectionTitle, StatusPill } from '../../src/ui/components';
import { ConnectionPill, FeedbackBanner } from '../../src/ui/deviceViews';
import { useAction } from '../../src/ui/hooks';
import { doorLook, doorLookOffline, screen } from '../../src/ui/tokens';
import { DeviceSettings, simulator, useDeviceStore } from '../../src/store/deviceStore';
import { useCatsStore } from '../../src/store/catsStore';
import { useEventsStore } from '../../src/store/eventsStore';
import { formatDuration, formatKg } from '../../src/domain/format';
import { LIMITS } from '../../src/device/protocol';
import { LinkKind, realLinkAvailable } from '../../src/device/createLink';

/** Faixa aceita para a massa de referencia da calibracao, em gramas. */
const MASSA_REF_MIN_G = 50;
const MASSA_REF_MAX_G = 20000;

/** Le o campo da massa de referencia (gramas inteiras); null quando nao vale. */
function lerMassaRef(texto: string): number | null {
  const limpo = texto.trim();
  if (!/^\d+$/.test(limpo)) return null;
  const gramas = Number(limpo);
  if (gramas < MASSA_REF_MIN_G || gramas > MASSA_REF_MAX_G) return null;
  return gramas;
}

export default function ComedouroScreen() {
  const status = useDeviceStore((s) => s.status);
  const connection = useDeviceStore((s) => s.connection);
  const settings = useDeviceStore((s) => s.settings);
  // assinar o tipo de transporte faz a tela redesenhar quando ele troca
  const linkKind = useDeviceStore((s) => s.linkKind);
  const link = useDeviceStore((s) => s.link);
  const cats = useCatsStore((s) => s.cats);
  const eventos = useEventsStore((s) => s.events.length);
  const { run, feedback, isPending } = useAction();
  const [confirmandoReset, setConfirmandoReset] = useState(false);
  const [massaRef, setMassaRef] = useState('');
  const massaRefG = lerMassaRef(massaRef);
  const massaRefInvalida = massaRef.trim().length > 0 && massaRefG === null;
  const sim = simulator();
  const router = useRouter();
  const conectado = connection === 'connected';
  const porta = conectado && status ? doorLook[status.door] : doorLookOffline;

  function ajustar(campo: keyof DeviceSettings, valor: number) {
    void run('ajuste', () => useDeviceStore.getState().setSettings({ ...settings, [campo]: valor }));
  }

  function trocarModo(kind: LinkKind) {
    if (kind === linkKind) return;
    void run('modo-link', async () => {
      await useDeviceStore.getState().switchLink(kind);
      await useDeviceStore.getState().connect();
    });
  }
  const emAdaptacao = status?.mode === 'learn';

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      <FeedbackBanner feedback={feedback} />

      <Card className="gap-3">
        <Row className="justify-between">
          <SectionTitle className="flex-1 pr-2" hint={link.label}>
            Conexão
          </SectionTitle>
          <ConnectionPill />
        </Row>
        <Row>
          <Segmento
            titulo="Comedouro real"
            ativo={linkKind === 'ble'}
            desabilitado={!realLinkAvailable() || isPending('modo-link')}
            onPress={() => trocarModo('ble')}
          />
          <Segmento
            titulo="Simulador"
            ativo={linkKind === 'mock'}
            desabilitado={isPending('modo-link')}
            onPress={() => trocarModo('mock')}
          />
        </Row>
        <Text className={screen.hint}>
          {linkKind === 'ble'
            ? 'Fala com a maquete pelo Bluetooth. O histórico deste modo é só das visitas de verdade.'
            : 'Um comedouro de mentira, para testar e apresentar sem a maquete. Tem o próprio histórico, separado do real.'}
        </Text>
        {!realLinkAvailable() ? (
          <Text className={screen.hint}>
            Este navegador não tem Bluetooth: use o Chrome ou o Edge, ou o app instalado no Android.
          </Text>
        ) : null}
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
              // connect() ja reenvia o cadastro dos gatos (ver onDeviceReady no bootstrap)
              run('conectar', () => useDeviceStore.getState().connect())
            }
          />
        )}
        {link.forget ? (
          <BigButton
            title="Esquecer este comedouro"
            icon="trash-bin"
            variant="ghost"
            loading={isPending('esquecer')}
            onPress={() =>
              run(
                'esquecer',
                () => useDeviceStore.getState().forgetDevice(),
                'Comedouro esquecido. O próximo Conectar procura de novo.',
              )
            }
          />
        ) : null}
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
            ? 'A porta abre para qualquer gato e o peso de cada visita é registrado. Nenhum gato é bloqueado.'
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
        <KeyValue
          label="Fim de curso FECHADO"
          value={!conectado || !status ? '—' : status.fcC ? 'acionado' : 'livre'}
        />
        <KeyValue
          label="Fim de curso ABERTO"
          value={!conectado || !status ? '—' : status.fcO ? 'acionado' : 'livre'}
        />
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

      <Card className="gap-3">
        <SectionTitle hint="Zere com a balança vazia e calibre com uma massa conhecida">
          Balança
        </SectionTitle>
        <KeyValue label="Leitura atual" value={formatKg(status?.wG ?? 0)} />
        <BigButton
          title="Zerar (tara)"
          icon="speedometer"
          variant="secondary"
          loading={isPending('tara')}
          disabled={!conectado}
          onPress={() => run('tara', () => useDeviceStore.getState().tare(), 'Balança zerada.')}
        />

        <View className="h-px bg-slate-200 dark:bg-slate-800" />

        <Text className={screen.label}>Calibrar com uma massa conhecida</Text>
        <View className="gap-1">
          <Text className={screen.hint}>1. Com a plataforma vazia, toque em Zerar (tara).</Text>
          <Text className={screen.hint}>
            2. Coloque no centro da plataforma algo de peso conhecido (uma garrafa d’água cheia
            serve).
          </Text>
          <Text className={screen.hint}>3. Digite a massa em gramas e toque em Calibrar.</Text>
        </View>
        <Row>
          <TextInput
            className={`flex-1 ${screen.input}`}
            value={massaRef}
            onChangeText={setMassaRef}
            keyboardType="number-pad"
            inputMode="numeric"
            placeholder="Massa em gramas, ex.: 696"
            placeholderTextColor="#94a3b8"
            maxLength={6}
            accessibilityLabel="Massa de referência em gramas"
          />
          <BigButton
            title="Calibrar"
            icon="scale"
            loading={isPending('calibrar')}
            disabled={!conectado || massaRefG === null}
            onPress={() => {
              if (massaRefG === null) return;
              void run(
                'calibrar',
                async () => {
                  await useDeviceStore.getState().calibrate(massaRefG);
                  setMassaRef('');
                },
                `Balança calibrada com ${massaRefG} g. Confira a leitura com a massa ainda em cima.`,
              );
            }}
          />
        </Row>
        {massaRefInvalida ? (
          <Text className={screen.error}>
            {`Use gramas inteiras, entre ${MASSA_REF_MIN_G} g e ${MASSA_REF_MAX_G / 1000} kg.`}
          </Text>
        ) : null}
      </Card>

      <Card className="gap-3">
        <SectionTitle hint="Estes tempos são enviados ao comedouro e valem mesmo sem o celular por perto">
          Ajustes de comportamento
        </SectionTitle>

        <Ajuste
          titulo="Tempo de confirmação"
          valor={formatDuration(settings.confMs / 1000)}
          explicacao="Quanto tempo o peso precisa ficar estável antes da porta abrir. Evita que a porta abra quando o gato só passa por cima da balança."
          podeDiminuir={settings.confMs > LIMITS.confMs.min}
          podeAumentar={settings.confMs < LIMITS.confMs.max}
          onDiminuir={() => ajustar('confMs', Math.max(LIMITS.confMs.min, settings.confMs - 500))}
          onAumentar={() => ajustar('confMs', Math.min(LIMITS.confMs.max, settings.confMs + 500))}
        />
        <Ajuste
          titulo="Espera para fechar"
          valor={formatDuration(settings.leaveMs / 1000)}
          explicacao="Quanto tempo o comedouro espera, depois que a balança fica vazia, antes de fechar. Evita fechar quando o gato só deu um passo para trás."
          podeDiminuir={settings.leaveMs > LIMITS.leaveMs.min}
          podeAumentar={settings.leaveMs < LIMITS.leaveMs.max}
          onDiminuir={() => ajustar('leaveMs', Math.max(LIMITS.leaveMs.min, settings.leaveMs - 1000))}
          onAumentar={() => ajustar('leaveMs', Math.min(LIMITS.leaveMs.max, settings.leaveMs + 1000))}
        />
        <Ajuste
          titulo="Duração do reabastecimento"
          valor={formatDuration(settings.maxOpenS)}
          explicacao="Quando você abre a porta para colocar ração, ela fecha sozinha depois deste tempo — assim ela nunca fica aberta por esquecimento."
          podeDiminuir={settings.maxOpenS > LIMITS.refillS.min}
          podeAumentar={settings.maxOpenS < LIMITS.refillS.max}
          onDiminuir={() => ajustar('maxOpenS', Math.max(LIMITS.refillS.min, settings.maxOpenS - 60))}
          onAumentar={() => ajustar('maxOpenS', Math.min(LIMITS.refillS.max, settings.maxOpenS + 60))}
        />
        <Text className={screen.hint}>
          {conectado
            ? 'Cada toque já vale no comedouro.'
            : 'Sem conexão: os ajustes ficam guardados e vão para o comedouro na próxima conexão.'}
        </Text>

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
          <SectionTitle hint="Só aparece no modo Simulador">
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

/** Uma linha de ajuste: nome, valor com − / +, e explicação em português claro. */
function Ajuste({
  titulo,
  valor,
  explicacao,
  podeDiminuir,
  podeAumentar,
  onDiminuir,
  onAumentar,
}: {
  titulo: string;
  valor: string;
  explicacao: string;
  podeDiminuir: boolean;
  podeAumentar: boolean;
  onDiminuir: () => void;
  onAumentar: () => void;
}) {
  return (
    <View className="gap-1">
      <Row className="justify-between">
        <Text className={`flex-1 ${screen.label}`}>{titulo}</Text>
        <Row className="gap-2">
          <Passo simbolo="−" rotulo={`Diminuir ${titulo}`} ativo={podeDiminuir} onPress={onDiminuir} />
          <Text className="min-w-16 text-center text-sm font-semibold text-slate-900 dark:text-slate-50">
            {valor}
          </Text>
          <Passo simbolo="+" rotulo={`Aumentar ${titulo}`} ativo={podeAumentar} onPress={onAumentar} />
        </Row>
      </Row>
      <Text className={screen.hint}>{explicacao}</Text>
    </View>
  );
}

function Passo({
  simbolo,
  rotulo,
  ativo,
  onPress,
}: {
  simbolo: string;
  rotulo: string;
  ativo: boolean;
  onPress: () => void;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={rotulo}
      disabled={!ativo}
      onPress={onPress}
      className={`h-10 w-10 items-center justify-center rounded-full border ${
        ativo
          ? 'border-marca-500 bg-marca-100 dark:bg-marca-900'
          : 'border-slate-200 opacity-40 dark:border-slate-700'
      }`}
    >
      <Text className="text-lg font-bold text-slate-900 dark:text-slate-50">{simbolo}</Text>
    </Pressable>
  );
}

/** Metade de um seletor de duas opções. */
function Segmento({
  titulo,
  ativo,
  desabilitado,
  onPress,
}: {
  titulo: string;
  ativo: boolean;
  desabilitado?: boolean;
  onPress: () => void;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityState={{ selected: ativo, disabled: desabilitado }}
      disabled={desabilitado}
      onPress={onPress}
      className={`min-h-12 flex-1 items-center justify-center rounded-xl border px-3 py-2 ${
        ativo
          ? 'border-marca-500 bg-marca-100 dark:bg-marca-900'
          : 'border-slate-200 dark:border-slate-700'
      } ${desabilitado && !ativo ? 'opacity-40' : ''}`}
    >
      <Text className={screen.label}>{titulo}</Text>
    </Pressable>
  );
}
