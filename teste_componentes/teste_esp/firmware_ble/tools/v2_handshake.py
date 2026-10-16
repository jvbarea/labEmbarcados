"""
Imita o que o app Feed Cat faz ao conectar, para validar o firmware sem
precisar do celular nem do development build.

    python tools/v2_handshake.py

Segue a ordem do protocolo v2 (plano/01-app.md secao 7):
hello -> cfgGet -> cats -> cfg -> sync, e depois exercita uma visita.
"""
import asyncio
import json
import time

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "Alimentador"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

recebidas = []


def on_notify(_, data: bytearray):
    txt = data.decode("utf-8", "replace")
    recebidas.append(txt)
    # o status de 1 em 1 s so aparece quando pedido explicitamente
    if '"t":"st"' in txt and len(recebidas) > 3:
        return
    print("   <-", txt)


def checa(rotulo, cond, detalhe=""):
    print(f"   [{'OK ' if cond else 'FALHA'}] {rotulo}{(' - ' + detalhe) if detalhe else ''}")
    return cond


async def main():
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=12.0)
    if dev is None:
        print(f"nao encontrei '{DEVICE_NAME}'")
        return
    print(f"conectando em {dev.address}...")

    falhas = 0
    async with BleakClient(dev) as c:
        await c.start_notify(TX, on_notify)

        async def send(obj, wait=2.0):
            js = json.dumps(obj, separators=(",", ":"))
            print("\n   ->", js)
            await c.write_gatt_char(RX, js.encode(), response=True)
            await asyncio.sleep(wait)

        def ultimo(tipo):
            for txt in reversed(recebidas):
                try:
                    m = json.loads(txt)
                except Exception:
                    continue
                if m.get("t") == tipo:
                    return m
            return None

        print("\n=== 1. hello (o app manda a hora, a ESP32 nao tem RTC) ===")
        agora = int(time.time())
        await send({"t": "hello", "id": 1, "v": 2, "now": agora})
        ack = ultimo("ack")
        falhas += not checa("ack do hello", ack and ack.get("id") == 1 and ack.get("ok"))
        st = ultimo("st")
        falhas += not checa(
            "st tem todos os campos do v2",
            st and all(k in st for k in ("wG", "cat", "door", "fcC", "fcO", "grant", "mode", "cfgV")),
            str(st),
        )

        print("\n=== 2. cfgGet -> cfgDump ===")
        await send({"t": "cfgGet", "id": 2})
        dump = ultimo("cfgDump")
        falhas += not checa("cfgDump recebido", dump is not None, str(dump))

        print("\n=== 3. cats (dois gatos, um bloqueado) ===")
        await send({"t": "cats", "id": 3, "cats": [
            {"k": 1, "minG": 2900, "maxG": 3500, "rule": "allow"},
            {"k": 2, "minG": 6000, "maxG": 7000, "rule": "deny"},
        ]})
        ack = ultimo("ack")
        falhas += not checa("ack do cats", ack and ack.get("id") == 3 and ack.get("ok"))

        print("\n=== 4. cfg (tempos) ===")
        await send({"t": "cfg", "id": 4, "confMs": 1000, "leaveMs": 1500, "maxOpenS": 60})
        ack = ultimo("ack")
        falhas += not checa("ack do cfg", ack and ack.get("id") == 4 and ack.get("ok"))

        print("\n=== 5. gato 1 (3200 g) sobe: deve ABRIR ===")
        await send({"t": "cmd", "id": 5, "c": "mock", "wG": 3200}, 3.5)
        st = ultimo("st")
        falhas += not checa("cat identificado como 1", st and st.get("cat") == 1, str(st))
        falhas += not checa("porta abrindo/aberta", st and st.get("door") in ("opening", "open"), str(st))

        print("\n=== 6. gato 1 desce: visita fecha com duracao ===")
        await send({"t": "cmd", "id": 6, "c": "mock", "wG": 0}, 5.0)
        ev = ultimo("ev")
        falhas += not checa("evento de visita gerado", ev and ev.get("kind") == "visit", str(ev))
        falhas += not checa("visita com door=opened", ev and ev.get("door") == "opened", str(ev))
        falhas += not checa("ts em epoch (hello aplicado)", ev and ev.get("ts", 0) > 1700000000, str(ev))

        print("\n=== 7. gato 2 (6500 g) sobe: regra deny, NAO abre ===")
        await send({"t": "cmd", "id": 7, "c": "mock", "wG": 6500}, 3.5)
        st = ultimo("st")
        falhas += not checa("cat identificado como 2", st and st.get("cat") == 2, str(st))
        falhas += not checa("porta continua fechada", st and st.get("door") == "closed", str(st))
        await send({"t": "cmd", "id": 8, "c": "mock", "wG": 0}, 5.0)
        ev = ultimo("ev")
        falhas += not checa("visita com door=denied", ev and ev.get("door") == "denied", str(ev))

        print("\n=== 8. peso desconhecido (1200 g): why=unknown ===")
        await send({"t": "cmd", "id": 9, "c": "mock", "wG": 1200}, 3.5)
        await send({"t": "cmd", "id": 10, "c": "mock", "wG": 0}, 5.0)
        ev = ultimo("ev")
        falhas += not checa("why=unknown", ev and ev.get("why") == "unknown", str(ev))

        print("\n=== 9. reabastecer e fechar ===")
        await send({"t": "cmd", "id": 11, "op": "refill", "timeoutS": 300}, 3.0)
        st = ultimo("st")
        falhas += not checa("porta abriu no refill", st and st.get("door") in ("opening", "open"), str(st))
        await send({"t": "cmd", "id": 12, "op": "close"}, 4.0)

        print("\n=== 10. sync do historico ===")
        await send({"t": "sync", "id": 13, "since": 0}, 3.0)
        fim = ultimo("syncEnd")
        falhas += not checa("syncEnd recebido", fim and fim.get("id") == 13, str(fim))

        print("\n=== 11. grant ainda nao suportado (Etapa 3) ===")
        await send({"t": "grant", "id": 14, "cat": 2, "visits": 1}, 2.0)
        ack = ultimo("ack")
        falhas += not checa("ack honesto com not_supported",
                            ack and ack.get("ok") is False and ack.get("err") == "not_supported",
                            str(ack))

        print("\n=== 12. tamanho das mensagens (limite do MTU) ===")
        maior = max(recebidas, key=len)
        falhas += not checa(f"maior notificacao = {len(maior)} bytes", len(maior) <= 180, maior[:80])

        await c.stop_notify(TX)

    print(f"\n{'=' * 50}")
    print("TUDO PASSOU" if falhas == 0 else f"{falhas} VERIFICACAO(OES) FALHARAM")


asyncio.run(main())
