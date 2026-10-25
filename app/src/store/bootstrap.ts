/** Carrega dados salvos e conecta ao comedouro quando o app abre. */

import { useEffect, useState } from 'react';
import { useCatsStore } from './catsStore';
import { onDeviceReady, simulator, useDeviceStore } from './deviceStore';
import { useEventsStore } from './eventsStore';

let started = false;

/**
 * Roda depois de TODA conexao preparada, qualquer que seja o caminho: o
 * auto-connect daqui, o botao Conectar de qualquer tela, ou a reconexao
 * automatica do Bluetooth.
 */
async function aposConectar() {
  // App recem-instalado (ou celular novo): tenta puxar o cadastro que o
  // comedouro guarda na flash antes de pedir tudo de novo ao usuario.
  if (!useCatsStore.getState().configured) {
    await useCatsStore.getState().recoverFromDevice();
  }
  await useCatsStore.getState().syncToDevice();
}

export function useBootstrap(): boolean {
  const [ready, setReady] = useState(false);

  useEffect(() => {
    let cancelado = false;

    async function boot() {
      if (!started) {
        started = true;
        useDeviceStore.getState().init();
        onDeviceReady(aposConectar);
      }
      await Promise.all([
        useCatsStore.getState().load(),
        useEventsStore.getState().load(),
        useDeviceStore.getState().loadSettings(),
      ]);
      if (cancelado) return;
      setReady(true);

      try {
        // No Bluetooth pelo navegador isto falha de proposito (o seletor exige
        // um toque); a conexao vem do botao Conectar.
        await useDeviceStore.getState().connect();

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
