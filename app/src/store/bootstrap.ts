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

  // No simulador, se o historico dele esta vazio, semeia alguns dias de visitas
  // (incluindo hoje) para as telas nao nascerem vazias. Uma vez so: depois o
  // historico do simulador ja esta salvo. O do comedouro real nunca e semeado.
  const temVisitas = useEventsStore.getState().events.some((e) => e.kind === 'visit');
  if (!temVisitas) simulator()?.seedHistory(2);
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
        useDeviceStore.getState().loadSettings(),
        // simulador ou comedouro real, e o historico daquele modo
        useDeviceStore.getState().restoreLink(),
      ]);
      if (cancelado) return;
      setReady(true);

      try {
        // No celular conecta sozinho (pede a permissao de Bluetooth na primeira
        // vez). No navegador falha de proposito: o seletor exige um toque, e a
        // conexao vem do botao Conectar.
        await useDeviceStore.getState().connect();
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
