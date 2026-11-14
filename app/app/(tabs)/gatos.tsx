import { Link } from 'expo-router';
import { Pressable, ScrollView, Text, View } from 'react-native';
import { Card, CatAvatar, Row, StatusPill } from '../../src/ui/components';
import { screen } from '../../src/ui/tokens';
import { catLabel, useCatsStore } from '../../src/store/catsStore';
import { formatKg } from '../../src/domain/format';
import { rangeOf } from '../../src/domain/cats';

export default function GatosScreen() {
  const cats = useCatsStore((s) => s.cats);

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      <Text className={screen.subtitle}>
        O comedouro identifica cada gato pela faixa de peso. As faixas não podem se encostar.
      </Text>

      {cats.map((cat) => {
        const faixa = rangeOf(cat);
        const liberado = cat.rule === 'allow';
        return (
          <Link key={cat.id} href={`/gato/${cat.id}`} asChild>
            <Pressable accessibilityRole="button">
              <Card className="gap-3">
                <Row className="justify-between">
                  <Row>
                    <CatAvatar emoji={cat.emoji} color={cat.color} />
                    <View>
                      <Text className="text-lg font-semibold text-slate-900 dark:text-slate-50">
                        {catLabel(cat)}
                      </Text>
                      <Text className={screen.hint}>{formatKg(cat.weightG, 1)} típico</Text>
                    </View>
                  </Row>
                  <StatusPill
                    label={liberado ? 'Liberado' : 'Em dieta'}
                    tone={liberado ? 'success' : 'warning'}
                    icon={liberado ? 'checkmark-circle' : 'lock-closed'}
                  />
                </Row>

                <View className="rounded-xl bg-slate-100 p-3 dark:bg-slate-800">
                  <Text className={screen.label}>Faixa aceita</Text>
                  <Text className="text-base font-semibold text-slate-900 dark:text-slate-50">
                    {formatKg(faixa.minG, 2)} — {formatKg(faixa.maxG, 2)}
                  </Text>
                  <Text className={screen.hint}>tolerância de ± {cat.toleranceG} g</Text>
                </View>

                <Text className={screen.hint}>Toque para editar</Text>
              </Card>
            </Pressable>
          </Link>
        );
      })}
    </ScrollView>
  );
}
