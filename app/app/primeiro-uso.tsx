import { Link, useRouter } from 'expo-router';
import { Pressable, ScrollView, Text, View } from 'react-native';
import { BigButton, Card, CatAvatar, Row, SectionTitle, StatusPill } from '../src/ui/components';
import { screen } from '../src/ui/tokens';
import { useCatsStore } from '../src/store/catsStore';
import { useDeviceStore } from '../src/store/deviceStore';

export default function PrimeiroUsoScreen() {
  const cats = useCatsStore((s) => s.cats);
  const configured = useCatsStore((s) => s.configured);
  const emAdaptacao = useDeviceStore((s) => s.status?.mode === 'learn');
  const router = useRouter();

  return (
    <ScrollView className={screen.page} contentContainerClassName={screen.content}>
      <Card className="gap-2">
        <Text className="text-4xl">🐱🐈</Text>
        <SectionTitle hint="Cada gato come o que é dele">Bem-vindo ao Feed Cat</SectionTitle>
        <Text className={screen.body}>
          O comedouro pesa quem sobe na balança e só abre a porta para o gato certo. Há dois
          caminhos para começar.
        </Text>
      </Card>

      <Card className="gap-3 border-2 border-marca-400">
        <Row className="justify-between">
          <SectionTitle
            className="flex-1 pr-2"
            hint="Recomendado quando você não sabe o peso exato dos gatos"
          >
            1. Modo adaptação
          </SectionTitle>
          <StatusPill label="Recomendado" tone="info" icon="sparkles" />
        </Row>

        <Text className={screen.body}>
          A porta fica aberta por alguns dias: os gatos se acostumam com o comedouro e a balança
          registra o peso de cada visita. Depois o app separa as medições em dois grupos e você só dá
          nome a cada um.
        </Text>

        <View className="gap-1 rounded-xl bg-slate-100 p-3 dark:bg-slate-800">
          <Text className={screen.hint}>1. Ligue o modo adaptação e deixe o comedouro aberto</Text>
          <Text className={screen.hint}>2. Os gatos comem e a balança anota cada visita</Text>
          <Text className={screen.hint}>3. O app identifica os dois e você dá os nomes</Text>
        </View>

        {emAdaptacao ? (
          <BigButton
            title="Ver medições da adaptação"
            icon="school"
            onPress={() => router.push('/adaptacao')}
          />
        ) : (
          <BigButton
            title="Começar pelo modo adaptação"
            icon="school"
            onPress={() => router.push('/adaptacao')}
          />
        )}
      </Card>

      <SectionTitle hint="Se você já sabe quanto cada gato pesa">2. Cadastro manual</SectionTitle>

      {cats.map((cat, indice) => {
        const pronto = cat.name.trim().length > 0;
        return (
          <Link key={cat.id} href={`/gato/${cat.id}`} asChild>
            <Pressable accessibilityRole="button">
              <Card className="gap-2">
                <Row className="justify-between">
                  <Row>
                    <CatAvatar emoji={cat.emoji} color={cat.color} />
                    <View>
                      <Text className="text-base font-semibold text-slate-900 dark:text-slate-50">
                        {pronto ? cat.name : `${indice + 1}º gato`}
                      </Text>
                      <Text className={screen.hint}>
                        {pronto ? 'Cadastrado — toque para editar' : 'Toque para cadastrar'}
                      </Text>
                    </View>
                  </Row>
                  <Text className="text-2xl">{pronto ? '✅' : '➕'}</Text>
                </Row>
              </Card>
            </Pressable>
          </Link>
        );
      })}

      <BigButton
        title="Começar a usar"
        icon="arrow-forward"
        disabled={!configured}
        onPress={() => router.replace('/')}
      />
      {!configured ? (
        <Text className={screen.hint}>Cadastre os dois gatos para continuar.</Text>
      ) : null}
    </ScrollView>
  );
}
