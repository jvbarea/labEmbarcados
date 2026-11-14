/** Pedacos de tela compartilhados que dependem do estado do comedouro. */

import { Text, View } from 'react-native';
import { ConnectionState, useDeviceStore } from '../store/deviceStore';
import { Feedback } from './hooks';
import { BigButton, StatusPill } from './components';
import { PillTone } from './tokens';

const CONNECTION_LOOK: Record<ConnectionState, { label: string; tone: PillTone }> = {
  idle: { label: 'Desconectado', tone: 'neutral' },
  connecting: { label: 'Conectando…', tone: 'info' },
  connected: { label: 'Conectado', tone: 'success' },
  error: { label: 'Falha na conexão', tone: 'danger' },
};

export function ConnectionPill() {
  const connection = useDeviceStore((s) => s.connection);
  const look = CONNECTION_LOOK[connection];
  return <StatusPill label={look.label} tone={look.tone} icon="bluetooth" />;
}

/**
 * Barra que aparece quando nao ha conexao, com botao para tentar de novo.
 * `idleText` troca a frase padrao (quando nao houve erro) por uma que faca
 * sentido na tela em que o aviso aparece.
 */
export function OfflineBanner({
  onConnect,
  loading,
  idleText = 'Sem conexão com o comedouro. Os dados abaixo são os últimos conhecidos.',
}: {
  onConnect: () => void;
  loading?: boolean;
  idleText?: string;
}) {
  const connection = useDeviceStore((s) => s.connection);
  const error = useDeviceStore((s) => s.connectionError);
  if (connection === 'connected' || connection === 'connecting') return null;

  return (
    <View className="gap-3 rounded-2xl border border-amber-300 bg-amber-50 p-4 dark:border-amber-800 dark:bg-amber-950">
      <Text className="text-sm text-amber-900 dark:text-amber-100">{error ?? idleText}</Text>
      <BigButton title="Conectar" icon="bluetooth" onPress={onConnect} loading={loading} />
    </View>
  );
}

export function FeedbackBanner({ feedback }: { feedback: Feedback | null }) {
  if (!feedback) return null;
  const ok = feedback.tone === 'success';
  return (
    <View
      className={`rounded-xl px-4 py-3 ${
        ok
          ? 'bg-emerald-100 dark:bg-emerald-900'
          : 'bg-rose-100 dark:bg-rose-900'
      }`}
    >
      <Text
        className={`text-sm font-medium ${
          ok ? 'text-emerald-900 dark:text-emerald-100' : 'text-rose-900 dark:text-rose-100'
        }`}
      >
        {feedback.text}
      </Text>
    </View>
  );
}
