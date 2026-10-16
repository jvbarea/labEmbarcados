"""
Console BLE do Alimentador — conecta pelo PC, sem precisar do celular.

    pip install bleak
    python tools/ble_console.py           # interativo
    python tools/ble_console.py --demo    # roteiro automatico, para conferir que tudo responde

Atalhos no modo interativo (ou mande JSON cru, que vai direto):

    w 4200        coloca 4200 g na balanca mockada
    open / close  abre / fecha a porta
    reset         rearma depois de falha
    status        pede o status agora
    events        historico
    cfg 3000 8000        janela de peso, em gramas
    door 0 90            posicoes da porta, em graus
    sensor off|on        simula sensor mudo
    quiet         liga/desliga a impressao do status de 1 em 1 s
    q             sai
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "Alimentador"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # app -> ESP (write)
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # ESP -> app (notify)

quiet_status = False
msg_id = 0


def next_id():
    global msg_id
    msg_id += 1
    return msg_id


def on_notify(_, data: bytearray):
    txt = data.decode("utf-8", "replace")
    if quiet_status and '"t":"st"' in txt:
        return
    print("   <-", txt)


def to_json(line: str):
    """Traduz um atalho em JSON do protocolo. None = linha vazia."""
    line = line.strip()
    if not line:
        return None
    if line.startswith("{"):
        return line  # JSON cru passa direto

    parts = line.split()
    cmd, args = parts[0].lower(), parts[1:]
    i = next_id()

    if cmd in ("open", "close", "reset", "status"):
        return '{"t":"cmd","id":%d,"c":"%s"}' % (i, cmd)
    if cmd == "w" and args:
        return '{"t":"cmd","id":%d,"c":"mock","wG":%s}' % (i, args[0])
    if cmd == "events":
        return '{"t":"get","id":%d,"w":"events"}' % i
    if cmd == "sensor" and args:
        ok = "false" if args[0].lower() in ("off", "0", "false") else "true"
        return '{"t":"cmd","id":%d,"c":"sensor","ok":%s}' % (i, ok)
    if cmd == "cfg" and len(args) >= 2:
        return '{"t":"cfg","id":%d,"minG":%s,"maxG":%s}' % (i, args[0], args[1])
    if cmd == "door" and len(args) >= 2:
        return '{"t":"cfg","id":%d,"closedDeg":%s,"openDeg":%s}' % (i, args[0], args[1])

    print("   [?] atalho desconhecido:", cmd)
    return None


async def connect():
    print(f"procurando '{DEVICE_NAME}'...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=12.0)
    if dev is None:
        print(f"nao encontrei '{DEVICE_NAME}'. A ESP32 esta ligada e rodando este firmware?")
        return None
    print(f"achei em {dev.address}, conectando...")
    return dev


async def run_demo(client):
    async def send(js, wait=3.5):
        print("   ->", js)
        await client.write_gatt_char(RX, js.encode(), response=True)
        await asyncio.sleep(wait)

    print("\n--- restaura a janela padrao ---")
    await send('{"t":"cfg","id":1,"minG":3000,"maxG":8000}', 2.0)
    print("\n--- gato de 4200 g sobe (dentro da janela) ---")
    await send('{"t":"cmd","id":2,"c":"mock","wG":4200}', 4.0)
    print("\n--- gato desce ---")
    await send('{"t":"cmd","id":3,"c":"mock","wG":0}', 4.0)
    print("\n--- peso fora da janela ---")
    await send('{"t":"cmd","id":4,"c":"mock","wG":12000}', 3.5)
    await send('{"t":"cmd","id":5,"c":"mock","wG":0}', 4.0)
    print("\n--- historico ---")
    await send('{"t":"get","id":6,"w":"events"}', 3.0)


async def run_interactive(client):
    global quiet_status
    loop = asyncio.get_running_loop()
    print("\nconectado. digite '?' para os atalhos, 'q' para sair.\n")

    while True:
        line = await loop.run_in_executor(None, lambda: input("> "))
        line = line.strip()

        if line.lower() in ("q", "quit", "exit"):
            break
        if line == "?":
            print(__doc__)
            continue
        if line.lower() == "quiet":
            quiet_status = not quiet_status
            print(f"   [i] status de 1 em 1 s {'oculto' if quiet_status else 'visivel'}")
            continue

        js = to_json(line)
        if js is None:
            continue
        print("   ->", js)
        await client.write_gatt_char(RX, js.encode(), response=True)
        await asyncio.sleep(0.4)


async def main():
    demo = "--demo" in sys.argv

    dev = await connect()
    if dev is None:
        return

    async with BleakClient(dev) as client:
        print("conectado:", client.is_connected)
        await client.start_notify(TX, on_notify)
        try:
            if demo:
                await run_demo(client)
            else:
                await run_interactive(client)
        finally:
            await client.stop_notify(TX)
    print("desconectado.")


if __name__ == "__main__":
    asyncio.run(main())
