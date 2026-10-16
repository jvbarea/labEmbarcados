import { useLocalSearchParams, useRouter } from 'expo-router';
import { useMemo, useState } from 'react';
import { Pressable, ScrollView, Text, TextInput, View } from 'react-native';
import { BigButton, Card, CatAvatar, Row, SectionTitle } from '../../src/ui/components';
import { FeedbackBanner } from '../../src/ui/deviceViews';
import { useAction } from '../../src/ui/hooks';
import { catColor, screen } from '../../src/ui/tokens';
import { useCatsStore } from '../../src/store/catsStore';
import { useDeviceStore } from '../../src/store/deviceStore';
import {
  CAT_COLOR_KEYS,
  CAT_EMOJIS,
  Cat,
  CatColorKey,
  CatId,
  FIXED_TOLERANCE_G,
  hasErrors,
  rangeOf,
  validateCat,
} from '../../src/domain/cats';
import { formatKg, gramsToKgInput, kgInputToGrams } from '../../src/domain/format';
import { PRESENCE_THRESHOLD_G } from '../../src/domain/rules';

export default function EditarGatoScreen() {
  const { id } = useLocalSearchParams<{ id: string }>();
  const catId = (Number(id) === 2 ? 2 : 1) as CatId;
  const router = useRouter();

  const cats = useCatsStore((s) => s.cats);
  const saveCat = useCatsStore((s) => s.saveCat);
  const pesoNaBalanca = useDeviceStore((s) => s.status?.wG ?? 0);
  const { run, feedback, isPending } = useAction();

  const original = cats.find((c) => c.id === catId);
  const [draft, setDraft] = useState<Cat | undefined>(original);
  const [pesoTexto, setPesoTexto] = useState(() => (original ? gramsToKgInput(original.weightG) : ''));

  const outros = useMemo(() => cats.filter((c) => c.id !== catId), [cats, catId]);
  const errors = useMemo(() => (draft ? validateCat(draft, outros) : {}), [draft, outros]);

  if (!draft) {
    return (
      <View className={screen.page}>
        <Text className={screen.body}>Gato não encontrado.</Text>
      </View>
    );
  }

  const faixa = rangeOf(draft);
  const invalido = hasErrors(errors);

  function update(patch: Partial<Cat>) {
    setDraft((atual) => (atual ? { ...atual, ...patch } : atual));
  }

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      <FeedbackBanner feedback={feedback} />

      <Card className="gap-4">
        <Row>
          <CatAvatar emoji={draft.emoji} color={draft.color} size="lg" />
          <View className="flex-1 gap-1">
            <Text className={screen.label}>Nome</Text>
            <TextInput
              className={screen.input}
              value={draft.name}
              onChangeText={(t) => update({ name: t })}
              placeholder="Ex.: Mimi"
              placeholderTextColor="#94a3b8"
              maxLength={20}
            />
            {errors.name ? <Text className={screen.error}>{errors.name}</Text> : null}
          </View>
        </Row>

        <View className="gap-2">
          <Text className={screen.label}>Emoji</Text>
          <View className="flex-row flex-wrap gap-2">
            {CAT_EMOJIS.map((emoji) => (
              <Pressable
                key={emoji}
                accessibilityRole="button"
                onPress={() => update({ emoji })}
                className={`h-12 w-12 items-center justify-center rounded-xl border ${
                  draft.emoji === emoji
                    ? 'border-marca-500 bg-marca-100 dark:bg-marca-900'
                    : 'border-slate-200 dark:border-slate-700'
                }`}
              >
                <Text className="text-2xl">{emoji}</Text>
              </Pressable>
            ))}
          </View>
        </View>

        <View className="gap-2">
          <Text className={screen.label}>Cor</Text>
          <View className="flex-row flex-wrap gap-2">
            {CAT_COLOR_KEYS.map((key: CatColorKey) => (
              <Pressable
                key={key}
                accessibilityRole="button"
                accessibilityLabel={`cor ${key}`}
                onPress={() => update({ color: key })}
                className={`h-10 w-10 rounded-full border-2 ${catColor[key].bg} ${
                  draft.color === key ? 'border-slate-900 dark:border-white' : 'border-transparent'
                }`}
              />
            ))}
          </View>
        </View>
      </Card>

      <Card className="gap-4">
        <SectionTitle hint="O comedouro usa a faixa para reconhecer o gato">Peso</SectionTitle>

        <View className="gap-1">
          <Text className={screen.label}>Peso típico (kg)</Text>
          <TextInput
            className={screen.input}
            value={pesoTexto}
            onChangeText={(t) => {
              setPesoTexto(t);
              const gramas = kgInputToGrams(t);
              update({ weightG: Number.isNaN(gramas) ? NaN : gramas });
            }}
            keyboardType="decimal-pad"
            placeholder="3,2"
            placeholderTextColor="#94a3b8"
          />
          {errors.weightG ? <Text className={screen.error}>{errors.weightG}</Text> : null}

          {pesoNaBalanca >= PRESENCE_THRESHOLD_G ? (
            <Pressable
              accessibilityRole="button"
              onPress={() => {
                setPesoTexto(gramsToKgInput(pesoNaBalanca));
                update({ weightG: pesoNaBalanca });
              }}
              className="self-start rounded-lg bg-slate-100 px-3 py-2 dark:bg-slate-800"
            >
              <Text className={screen.label}>Usar peso da balança ({formatKg(pesoNaBalanca)})</Text>
            </Pressable>
          ) : null}
        </View>

        <View className="rounded-xl bg-slate-100 p-3 dark:bg-slate-800">
          <Text className={screen.label}>Margem fixa</Text>
          <Text className={screen.body}>
            O comedouro aceita o peso informado ± {FIXED_TOLERANCE_G} g. É fixo porque a balança tem
            variação normal (gato se mexe, apoia a pata na borda) e porque os dois gatos precisam
            ter pesos bem diferentes para serem distinguidos.
          </Text>
        </View>

        <View className="rounded-xl bg-slate-100 p-3 dark:bg-slate-800">
          <Text className={screen.label}>Faixa resultante</Text>
          <Text className="text-base font-semibold text-slate-900 dark:text-slate-50">
            {Number.isFinite(draft.weightG)
              ? `${formatKg(faixa.minG)} — ${formatKg(faixa.maxG)}`
              : '—'}
          </Text>
        </View>
        {errors.range ? <Text className={screen.error}>{errors.range}</Text> : null}
      </Card>

      <Card className="gap-3">
        <SectionTitle hint="Quem está em dieta só come com liberação pelo app">Acesso</SectionTitle>
        <Row>
          <Pressable
            accessibilityRole="button"
            onPress={() => update({ rule: 'allow' })}
            className={`flex-1 items-center rounded-xl border py-4 ${
              draft.rule === 'allow'
                ? 'border-emerald-500 bg-emerald-50 dark:bg-emerald-950'
                : 'border-slate-200 dark:border-slate-700'
            }`}
          >
            <Text className={screen.body}>Liberado</Text>
          </Pressable>
          <Pressable
            accessibilityRole="button"
            onPress={() => update({ rule: 'deny' })}
            className={`flex-1 items-center rounded-xl border py-4 ${
              draft.rule === 'deny'
                ? 'border-amber-500 bg-amber-50 dark:bg-amber-950'
                : 'border-slate-200 dark:border-slate-700'
            }`}
          >
            <Text className={screen.body}>Em dieta</Text>
          </Pressable>
        </Row>
      </Card>

      <BigButton
        title="Salvar"
        icon="save"
        disabled={invalido}
        loading={isPending('salvar')}
        onPress={() =>
          run('salvar', async () => {
            await saveCat(draft);
            router.back();
          })
        }
      />
    </ScrollView>
  );
}
