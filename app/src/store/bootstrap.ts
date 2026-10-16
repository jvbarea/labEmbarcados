/** Carrega dados salvos e conecta ao comedouro quando o app abre. */

import { useEffect, useState } from 'react';
import { useCatsStore } from './catsStore';
import { simulator, useDeviceStore } from './deviceStore';
import { useEventsStore } from './eventsStore';

let started = false;

export function useBootstrap(): boolean {
  const [ready, setReady] = useState(false);

  useEffect(() => {
    let cancelado = false;

    async function boot() {
      if (!started) {
        started = true;
        useDeviceStore.getState().init();
      }
      await Promise.all([
        useCatsStore.getState().load(),
        useEventsStore.getState().load(),
        useDeviceStore.getState().loadSettings(),
      ]);
      if (cancelado) return;
      setReady(true);

      try {
        await useDeviceStore.getState().connect();

        // App recem-instalado (ou celular novo): tenta puxar o cadastro que
        // o comedouro guarda na flash antes de pedir tudo de novo ao usuario.
        if (!useCatsStore.getState().configured) {
          await useCatsStore.getState().recoverFromDevice();
        }
        await useCatsStore.getState().syncToDevice();

        // No simulador, se nao ha nada guardado ainda, semeia alguns dias de
        // visitas (incluindo hoje) para as telas nao nascerem vazias.
        // Roda uma vez so: na recarga seguinte o historico ja esta salvo.
        const temVisitas = useEventsStore
          .getState()
          .events.some((e) => e.kind === 'visit');
        if (!temVisitas) simulator()?.seedHistory(2);
      } catch {
        // sem conexao: o app funciona mostrando o ultimo estado conhecido
      }
    }

    void boot();
    return () => {
      cancelado = true;
    };
  }, []);

  return ready;
}
