"""
Conexao BLE com o Alimentador pelo PC, do mesmo jeito que o app faz:

  - mensagens maiores que um pacote vao em pedacos "#i/n:" nos dois sentidos
    (contrato em src/BleControl.h e app/src/device/framing.ts);
  - cada mensagem recebe um "id" e o firmware responde com um "ack" de mesmo id.

Usado por ble_console.py e v2_handshake.py. Precisa de: pip install bleak
"""
import asyncio
import json
import time

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "Alimentador"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # app -> ESP (write)
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # ESP -> app (notify)

APP_CHUNK = 180   # o mesmo tamanho de pacote do app
HEADER_MAX = 7    # "#99/99:"


def encode_frames(text: str, chunk: int = APP_CHUNK) -> list:
    """Divide em pacotes de no maximo `chunk` bytes, ja com os cabecalhos."""
    data = text.encode("utf-8")
    if len(data) <= chunk:
        return [data]
    body = chunk - HEADER_MAX
    n = (len(data) + body - 1) // body
    if n > 99:
        raise ValueError(f"mensagem de {len(data)} bytes nao cabe em 99 pedacos")
    return [f"#{i + 1}/{n}:".encode() + data[i * body:(i + 1) * body] for i in range(n)]


class FrameAssembler:
    """Remonta os pedacos; descarta a montagem quando algo chega fora de ordem."""

    def __init__(self):
        self.reset()
        self.dropped = 0

    def reset(self):
        self.parts = []
        self.expected = 0
        self.total = 0

    def push(self, packet: bytes):
        if not packet:
            return None
        if packet[:1] != b"#":
            self.reset()
            return packet.decode("utf-8", "replace")
        colon = packet.find(b":", 0, HEADER_MAX + 1)
        try:
            i_txt, n_txt = packet[1:colon].decode().split("/")
            i, n = int(i_txt), int(n_txt)
        except (ValueError, UnicodeDecodeError):
            self.reset()
            self.dropped += 1
            return None
        if i == 1:
            self.reset()
            self.total = n
            self.expected = 1
        if self.expected == 0 or i != self.expected or n != self.total:
            self.reset()
            self.dropped += 1
            return None
        self.parts.append(packet[colon + 1:])
        self.expected += 1
        if i < n:
            return None
        whole = b"".join(self.parts)
        self.reset()
        return whole.decode("utf-8", "replace")


class Feeder:
    """Uma conexao com o comedouro: manda, espera ack, guarda tudo o que chega."""

    def __init__(self, client: BleakClient):
        self.client = client
        self.asm = FrameAssembler()
        self.raw = []    # pacotes como chegaram (para conferir o tamanho)
        self.msgs = []   # mensagens remontadas: (texto, dict ou None)
        self.next_id = 1
        self.on_message = None  # callback(texto, dict) opcional

    @property
    def mtu(self) -> int:
        try:
            return int(self.client.mtu_size)
        except Exception:
            return 23

    async def start(self):
        await self.client.start_notify(TX, self._on_notify)

    async def stop(self):
        try:
            await self.client.stop_notify(TX)
        except Exception:
            pass

    def _on_notify(self, _, data: bytearray):
        pacote = bytes(data)
        self.raw.append(pacote)
        texto = self.asm.push(pacote)
        if texto is None:
            return
        try:
            msg = json.loads(texto)
        except ValueError:
            msg = None
        self.msgs.append((texto, msg))
        if self.on_message:
            self.on_message(texto, msg)

    async def send(self, obj: dict, chunk: int = APP_CHUNK) -> int:
        obj = dict(obj)
        if "id" not in obj:
            obj["id"] = self.next_id
            self.next_id += 1
        texto = json.dumps(obj, separators=(",", ":"), ensure_ascii=False)
        for pacote in encode_frames(texto, chunk):
            await self.client.write_gatt_char(RX, pacote, response=True)
        return obj["id"]

    async def send_raw(self, texto: str):
        await self.client.write_gatt_char(RX, texto.encode("utf-8"), response=True)

    def find(self, t=None, since=0, **campos):
        """Mensagens do tipo t a partir do indice since, filtradas por campos."""
        out = []
        for _, m in self.msgs[since:]:
            if not isinstance(m, dict):
                continue
            if t and m.get("t") != t:
                continue
            if all(m.get(k) == v for k, v in campos.items()):
                out.append(m)
        return out

    def last(self, t, since=0, **campos):
        achadas = self.find(t, since, **campos)
        return achadas[-1] if achadas else None

    def mark(self) -> int:
        return len(self.msgs)

    async def wait_for(self, cond, timeout: float) -> bool:
        fim = time.monotonic() + timeout
        while time.monotonic() < fim:
            if cond():
                return True
            await asyncio.sleep(0.05)
        return cond()

    async def request(self, obj: dict, timeout: float = 4.0, chunk: int = APP_CHUNK) -> dict:
        """Manda e espera o ack, como o deviceStore do app (timeout de 4 s)."""
        inicio = self.mark()
        mid = await self.send(obj, chunk)
        await self.wait_for(lambda: self.last("ack", inicio, id=mid) is not None, timeout)
        return self.last("ack", inicio, id=mid) or {"t": "ack", "id": mid, "ok": False, "err": "SEM_RESPOSTA"}

    async def sync(self, since: int, timeout: float = 30.0):
        """sync completo: devolve (ack, eventos, syncEnd)."""
        inicio = self.mark()
        mid = await self.send({"t": "sync", "since": since})
        await self.wait_for(lambda: self.last("syncEnd", inicio, id=mid) is not None, timeout)
        ack = self.last("ack", inicio, id=mid)
        fim = self.last("syncEnd", inicio, id=mid)
        eventos, vistos = [], set()
        for _, m in self.msgs[inicio:]:
            if isinstance(m, dict) and m.get("t") == "syncEnd" and m.get("id") == mid:
                break
            if isinstance(m, dict) and m.get("t") == "ev" and m.get("seq") not in vistos:
                vistos.add(m.get("seq"))
                eventos.append(m)
        return ack, eventos, fim

    def status(self):
        return self.last("st")


async def find_device(timeout: float = 12.0):
    print(f"procurando '{DEVICE_NAME}'...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=timeout)
    if dev is None:
        print(f"nao encontrei '{DEVICE_NAME}'. A ESP32 esta ligada, e o app (ou outro")
        print("programa) NAO esta conectado nela? O comedouro aceita uma conexao por vez.")
    return dev
