import '../global.css';

import { Stack } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import { ActivityIndicator, View } from 'react-native';
import { SafeAreaProvider } from 'react-native-safe-area-context';
import { useBootstrap } from '../src/store/bootstrap';

export default function RootLayout() {
  const ready = useBootstrap();

  return (
    <SafeAreaProvider>
      <StatusBar style="auto" />
      {ready ? (
        <Stack
          screenOptions={{
            headerStyle: { backgroundColor: '#f97316' },
            headerTintColor: '#fff',
            headerTitleStyle: { fontWeight: '700' },
          }}
        >
          <Stack.Screen name="(tabs)" options={{ headerShown: false }} />
          <Stack.Screen name="gato/[id]" options={{ title: 'Editar gato' }} />
          <Stack.Screen name="primeiro-uso" options={{ title: 'Bem-vindo' }} />
          <Stack.Screen name="adaptacao" options={{ title: 'Modo adaptação' }} />
        </Stack>
      ) : (
        <View className="flex-1 items-center justify-center bg-slate-50 dark:bg-slate-950">
          <ActivityIndicator size="large" color="#f97316" />
        </View>
      )}
    </SafeAreaProvider>
  );
}
