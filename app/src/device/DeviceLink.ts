/**
 * Transporte entre o app e o comedouro.
 *
 * As telas nunca falam com o transporte direto: elas usam os stores, que usam
 * esta interface. Hoje existe o MockLink (simulador); na Etapa 2 entra o
 * BleLink, sem mexer em tela nenhuma.
 */

import { AppMessage, DeviceMessage } from './protocol';

export type DeviceMessageHandler = (msg: DeviceMessage) => void;
export type ConnectionHandler = (connected: boolean) => void;
export type Unsubscribe = () => void;

export interface DeviceLink {
  readonly kind: 'mock' | 'ble';
  /** Nome mostrado na tela de conexao. */
  readonly label: string;
  isConnected(): boolean;
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  /** Envia sem esperar resposta; o "ack" chega por onMessage. */
  send(msg: AppMessage): Promise<void>;
  onMessage(handler: DeviceMessageHandler): Unsubscribe;
  onConnectionChange(handler: ConnectionHandler): Unsubscribe;
}

/** Erro de transporte (sem conexao, escrita falhou). */
export class LinkError extends Error {
  constructor(
    message: string,
    readonly code: string = 'link_error',
  ) {
    super(message);
    this.name = 'LinkError';
  }
}
