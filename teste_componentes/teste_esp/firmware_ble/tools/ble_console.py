"""
Console BLE do Alimentador: conecta pelo PC, sem precisar do celular, e fala o
protocolo Feed Cat v2 (mensagens grandes em pedacos, como o app).

    pip install bleak
    python tools/ble_console.py

Atalhos (ou mande JSON cru, que vai direto; o "id" e posto sozinho):

    hello                 manda a hora do PC (faca primeiro: sem isso os eventos ficam sem hora)
    w 4200                coloca 4200 g na balanca simulada (so no 'pio run -e mock')
    refill [s]            abre para reabastecer (padrao 60 s)
    close                 fecha (com a porta em falha, rearma)
    reset                 rearme de bancada da maquina de estados
    status                pede o status agora
    sync [desde]          historico a partir de um seq (padrao 0)
    cfgget                o que esta gravado: gatos, tempos, modo, perfil
    grant <gato> <min>    libera o gato por <min> minutos
    grantv <gato> <n>     libera o gato por <n> visitas
    revoke <gato>         cancela a liberacao
    learn / auto          modo adaptacao / automatico
    times <conf> <leave> <maxOpenS>   tempos (ms, ms, s)
    bench <min> <max>     janela de bancada em gramas (so vale sem gatos cadastrados)
    door <fechada> <aberta>           posicoes da porta, em graus
    sensor off|on         simula sensor mudo (balanca simulada)
    evtrim <seq>          apaga do historico os eventos depois deste seq
    quiet                 liga/desliga a impressao do status (sai a cada 500 ms)
    q                     sai
"""
import asyncio
import json
import time

from bleak import BleakClient

from feedcat_ble import Feeder, find_device

quiet_status = True


def mostra(texto, msg):
    if quiet_status and isinstance(msg, dict) and msg.get("t") == "st":
        return
    print("   <-", texto)


def traduz(linha: str):
    """Atalho -> dict do protocolo. None = nada a mandar."""
    linha = linha.strip()
    if not linha:
        return None
    if linha.startswith("{"):
        return json.loads(linha)

    partes = linha.split()
    cmd, args = partes[0].lower(), partes[1:]

    def n(i, padrao=None):
        return int(args[i]) if len(args) > i else padrao

    if cmd == "hello":
        return {"t": "hello", "v": 2, "now": int(time.time())}
    if cmd == "w" and args:
        return {"t": "cmd", "c": "mock", "wG": float(args[0])}
    if cmd == "refill":
        return {"t": "cmd", "op": "refill", "timeoutS": n(0, 60)}
    if cmd == "close":
        return {"t": "cmd", "op": "close"}
    if cmd in ("reset", "status"):
        return {"t": "cmd", "c": cmd}
    if cmd == "sync":
        return {"t": "sync", "since": n(0, 0)}
    if cmd == "cfgget":
        return {"t": "cfgGet"}
    if cmd == "grant" and len(args) >= 2:
        return {"t": "grant", "cat": n(0), "untilS": int(time.time()) + 60 * n(1)}
    if cmd == "grantv" and len(args) >= 2:
        return {"t": "grant", "cat": n(0), "visits": n(1)}
    if cmd == "revoke" and args:
        return {"t": "revoke", "cat": n(0)}
    if cmd in ("learn", "auto"):
        return {"t": "mode", "mode": cmd}
    if cmd == "times" and len(args) >= 3:
        return {"t": "cfg", "confMs": n(0), "leaveMs": n(1), "maxOpenS": n(2)}
    if cmd == "bench" and len(args) >= 2:
        return {"t": "cfg", "minG": n(0), "maxG": n(1)}
    if cmd == "door" and len(args) >= 2:
        return {"t": "cfg", "closedDeg": n(0), "openDeg": n(1)}
    if cmd == "sensor" and args:
        return {"t": "cmd", "c": "sensor", "ok": args[0].lower() not in ("off", "0", "false")}
    if cmd == "evtrim" and args:
        return {"t": "cmd", "c": "evtrim", "after": n(0)}

    print("   [?] atalho desconhecido:", cmd, "- digite ? para a lista")
    return None


async def main():
    global quiet_status
    dev = await find_device()
    if dev is None:
        return
    print(f"conectando em {dev.address}...")

    async with BleakClient(dev) as client:
        f = Feeder(client)
        f.on_message = mostra
        await f.start()
        print(f"conectado (MTU {f.mtu}). '?' mostra os atalhos, 'q' sai. Comece com 'hello'.\n")
        loop = asyncio.get_running_loop()
        try:
            while True:
                linha = (await loop.run_in_executor(None, lambda: input("> "))).strip()
                if linha.lower() in ("q", "quit", "exit"):
                    break
                if linha == "?":
                    print(__doc__)
                    continue
                if linha.lower() == "quiet":
                    quiet_status = not quiet_status
                    print(f"   [i] status {'oculto' if quiet_status else 'visivel'}")
                    continue
                try:
                    msg = traduz(linha)
                except (ValueError, json.JSONDecodeError) as erro:
                    print("   [erro]", erro)
                    continue
                if msg is None:
                    continue
                mid = await f.send(msg)
                print(f"   -> {json.dumps({**msg, 'id': mid}, ensure_ascii=False)}")
                await asyncio.sleep(0.4)
        finally:
            await f.stop()
    print("desconectado.")


if __name__ == "__main__":
    asyncio.run(main())
