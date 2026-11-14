"""
Teste de bancada do firmware pelo Bluetooth do PC: faz o papel do app e confere
cada resposta. Roda com a placa de verdade, sem celular.

    pip install bleak
    python tools/v2_handshake.py                       # teste completo
    python tools/v2_handshake.py --capturar msgs.jsonl # e grava tudo o que chegou
    python tools/v2_handshake.py --restaurar <retrato.json>

Com a balanca SIMULADA (pio run -e mock -t upload) o teste injeta peso e passa
por visitas, liberacoes, modo adaptacao, reabastecer e RNF04. Com a balanca REAL
esses passos sao pulados (dependem de injetar peso) e o resto continua.

ATENCAO: a porta abre e fecha varias vezes. Mantenha as maos longe do mecanismo.

O teste NAO tara nem calibra a balanca. Antes de comecar ele tira um retrato do
que esta gravado no comedouro (gatos, tempos, modo e perfil) e no fim devolve
tudo como estava, e apaga do historico so os eventos que ele mesmo criou. O
retrato fica salvo num arquivo; se o teste for interrompido no meio, o
--restaurar devolve a configuracao a partir dele.

O app nao pode estar conectado ao mesmo tempo: o comedouro aceita uma conexao.
"""
import argparse
import asyncio
import json
import os
import sys
import tempfile
import time

from bleak import BleakClient

from feedcat_ble import Feeder, find_device

GATOS_TESTE = [
    {"k": 1, "minG": 2700, "maxG": 3700, "rule": "allow"},
    {"k": 2, "minG": 6000, "maxG": 7000, "rule": "deny"},
]
# Tempos curtos para o teste andar rapido.
CFG_TESTE = {"confMs": 1000, "leaveMs": 1500, "maxOpenS": 60}
PERFIL_TESTE = json.dumps(
    [
        {"id": 1, "name": "Mimi Teste", "emoji": "\U0001F431", "color": "laranja"},
        {"id": 2, "name": "Bolota Teste", "emoji": "\U0001F408‍⬛", "color": "azul"},
    ],
    ensure_ascii=False,
    separators=(",", ":"),
)

falhas = 0


def checa(rotulo, cond, detalhe=""):
    global falhas
    if not cond:
        falhas += 1
    marca = "OK   " if cond else "FALHA"
    print(f"   [{marca}] {rotulo}{(' - ' + str(detalhe)) if detalhe and not cond else ''}")
    return cond


def etapa(titulo):
    print(f"\n=== {titulo}")


def porta(f):
    return (f.status() or {}).get("door")


async def espera_porta(f, estados, timeout=6.0):
    return await f.wait_for(lambda: porta(f) in estados, timeout)


async def peso(f, gramas):
    return await f.request({"t": "cmd", "c": "mock", "wG": gramas})


async def visita(f, gramas, em_cima=3.5):
    """Gato sobe, fica em_cima segundos e desce. Devolve (status em cima, evento)."""
    inicio = f.mark()
    await peso(f, gramas)
    await asyncio.sleep(em_cima)
    st = f.status()
    await peso(f, 0)
    await f.wait_for(lambda: f.last("ev", inicio, kind="visit") is not None, 6.0)
    return st, f.last("ev", inicio, kind="visit")


# ---------------------------------------------------------------- retrato

async def tira_retrato(f):
    await f.request({"t": "cfgGet"})
    dump = f.last("cfgDump")
    _, _, fim = await f.sync(2**31 - 1)  # nada a mandar: so devolve o ultimo seq
    retrato = {
        "quando": time.strftime("%Y-%m-%d %H:%M:%S"),
        "cfgDump": dump,
        "lastSeq": (fim or {}).get("last"),
    }
    caminho = os.path.join(tempfile.gettempdir(), f"alimentador_retrato_{int(time.time())}.json")
    with open(caminho, "w", encoding="utf-8") as arq:
        json.dump(retrato, arq, ensure_ascii=False, indent=2)
    return retrato, caminho


async def restaura(f, retrato):
    dump = retrato.get("cfgDump") or {}
    await peso(f, 0)  # so tem efeito na balanca simulada
    await f.request({"t": "cmd", "op": "close"})
    for k in (1, 2):
        await f.request({"t": "revoke", "cat": k})
    oks = []
    if "cats" in dump:
        oks.append((await f.request({"t": "cats", "cats": dump["cats"]})).get("ok"))
    if all(k in dump for k in ("confMs", "leaveMs", "maxOpenS")):
        oks.append((await f.request({"t": "cfg", "confMs": dump["confMs"],
                                     "leaveMs": dump["leaveMs"],
                                     "maxOpenS": dump["maxOpenS"]})).get("ok"))
    oks.append((await f.request({"t": "mode", "mode": dump.get("mode", "auto")})).get("ok"))
    oks.append((await f.request({"t": "profile", "blob": dump.get("profile", "")})).get("ok"))
    if isinstance(retrato.get("lastSeq"), int):
        oks.append((await f.request({"t": "cmd", "c": "evtrim",
                                     "after": retrato["lastSeq"]})).get("ok"))
    return all(oks)


async def confere_restauracao(f, retrato):
    dump = retrato.get("cfgDump") or {}
    await f.request({"t": "cfgGet"})
    agora = f.last("cfgDump") or {}
    iguais = all(agora.get(k) == dump.get(k)
                 for k in ("cats", "confMs", "leaveMs", "maxOpenS", "mode"))
    checa("configuracao igual a de antes do teste", iguais, f"antes={dump} agora={agora}")
    checa("perfil igual ao de antes", agora.get("profile", "") == dump.get("profile", ""))
    _, eventos, _ = await f.sync(retrato.get("lastSeq") or 0)
    checa("nenhum evento do teste ficou no historico", len(eventos) == 0,
          [e.get("kind") for e in eventos])


# ----------------------------------------------------------------- etapas

async def etapas_com_peso(f):
    etapa("gato 1 (3200 g) sobe: abre; desce: visita opened e porta fecha")
    st, ev = await visita(f, 3200)
    checa("gato 1 identificado no status", st and st.get("cat") == 1, st)
    checa("porta abrindo ou aberta com ele em cima", st and st.get("door") in ("opening", "open"), st)
    checa("visita cat=1 opened rule", ev and ev.get("cat") == 1 and ev.get("door") == "opened"
          and ev.get("why") == "rule", ev)
    checa("visita com hora (ts do hello)", ev and ev.get("ts", 0) > 1_700_000_000, ev)
    checa("porta fechou depois", await espera_porta(f, ("closed",)), porta(f))

    etapa("gato 2 (6500 g, em dieta): nao abre")
    st, ev = await visita(f, 6500)
    checa("porta continuou fechada", st and st.get("door") == "closed", st)
    checa("visita cat=2 denied", ev and ev.get("cat") == 2 and ev.get("door") == "denied", ev)

    etapa("peso desconhecido (1200 g): why=unknown")
    _, ev = await visita(f, 1200)
    checa("visita cat=0 unknown denied", ev and ev.get("cat") == 0 and ev.get("why") == "unknown", ev)

    etapa("liberacao do gato 2 por 1 visita")
    ack = await f.request({"t": "grant", "cat": 2, "visits": 1})
    checa("ack do grant", ack.get("ok"), ack)
    await asyncio.sleep(0.3)
    checa("st.grant = {cat 2, visits 1}", (f.status() or {}).get("grant") == {"cat": 2, "visits": 1},
          f.status())
    st, ev = await visita(f, 6500)
    checa("abriu para o gato 2", st and st.get("door") in ("opening", "open"), st)
    checa("visita why=grant opened", ev and ev.get("why") == "grant" and ev.get("door") == "opened", ev)
    checa("liberacao consumida", (f.status() or {}).get("grant") is None, f.status())
    await espera_porta(f, ("closed",))
    st, ev = await visita(f, 6500)
    checa("na seguinte volta a negar", ev and ev.get("door") == "denied", ev)

    etapa("liberacao por prazo e cancelamento")
    ack = await f.request({"t": "grant", "cat": 2, "untilS": int(time.time()) + 20})
    checa("ack do grant por prazo", ack.get("ok"), ack)
    await asyncio.sleep(0.3)
    checa("st.grant.untilS", "untilS" in ((f.status() or {}).get("grant") or {}), f.status())
    await f.request({"t": "revoke", "cat": 2})
    await asyncio.sleep(0.3)
    checa("revoke limpa", (f.status() or {}).get("grant") is None, f.status())

    etapa("modo adaptacao: abre para qualquer gato")
    checa("ack do mode learn", (await f.request({"t": "mode", "mode": "learn"})).get("ok"))
    st, ev = await visita(f, 6500)
    checa("abriu para o gato 2 em adaptacao", st and st.get("door") in ("opening", "open"), st)
    checa("visita why=learn learn=1", ev and ev.get("why") == "learn" and ev.get("learn") == 1, ev)
    checa("ack do mode auto", (await f.request({"t": "mode", "mode": "auto"})).get("ok"))
    checa("fechada de volta no automatico", await espera_porta(f, ("closed",)), porta(f))

    etapa("RNF04: 'fechar' com gato em cima espera ele sair")
    await f.request({"t": "cmd", "op": "refill", "timeoutS": 60})
    await espera_porta(f, ("open",))
    await peso(f, 6500)
    await asyncio.sleep(2.0)
    ack = await f.request({"t": "cmd", "op": "close"})
    checa("ack do close na hora", ack.get("ok"), ack)
    await asyncio.sleep(2.0)
    checa("porta continua aberta com o gato em cima", porta(f) == "open", porta(f))
    await peso(f, 0)
    checa("fechou depois que ele saiu", await espera_porta(f, ("closing", "closed"), 6.0), porta(f))
    await espera_porta(f, ("closed",))


async def teste(f):
    etapa("hello e status")
    ack = await f.request({"t": "hello", "v": 2, "now": int(time.time())})
    checa("ack do hello", ack.get("ok"), ack)
    await asyncio.sleep(0.6)
    st = f.status()
    checa("st com todos os campos do v2",
          st and all(k in st for k in ("wG", "cat", "door", "fcC", "fcO", "grant", "mode", "cfgV")), st)
    checa("wG inteiro", st and isinstance(st.get("wG"), int), st)

    etapa("retrato do que esta gravado (sera devolvido no fim)")
    retrato, caminho = await tira_retrato(f)
    print(f"   retrato salvo em {caminho}")
    checa("cfgDump recebido", retrato["cfgDump"] is not None)
    checa("ultimo seq conhecido", isinstance(retrato["lastSeq"], int), retrato["lastSeq"])

    try:
        etapa("cadastro, tempos e perfil (o perfil passa de um pacote: vai em pedacos)")
        checa("ack do cats", (await f.request({"t": "cats", "cats": GATOS_TESTE})).get("ok"))
        checa("ack do cfg", (await f.request({"t": "cfg", **CFG_TESTE})).get("ok"))
        checa("ack do profile", (await f.request({"t": "profile", "blob": PERFIL_TESTE})).get("ok"))
        await asyncio.sleep(0.6)
        v1 = (f.status() or {}).get("cfgV")
        await f.request({"t": "cats", "cats": GATOS_TESTE})
        await f.request({"t": "cfg", **CFG_TESTE})
        await f.request({"t": "profile", "blob": PERFIL_TESTE})
        await asyncio.sleep(0.6)
        checa("reenviar igual nao muda o cfgV (nada gravado na flash)",
              (f.status() or {}).get("cfgV") == v1, f"{v1} -> {(f.status() or {}).get('cfgV')}")
        await f.request({"t": "cfgGet"})
        dump = f.last("cfgDump") or {}
        checa("cfgDump devolve os gatos", dump.get("cats") == GATOS_TESTE, dump.get("cats"))
        checa("cfgDump devolve o perfil inteiro, com emoji", dump.get("profile") == PERFIL_TESTE,
              dump.get("profile"))

        sonda = await peso(f, 0)
        balanca_real = sonda.get("err") == "real_scale"
        if balanca_real:
            print("\n### balanca REAL: os passos que injetam peso foram pulados")
        else:
            await etapas_com_peso(f)

        etapa("reabastecer e fechar")
        ack = await f.request({"t": "cmd", "op": "refill", "timeoutS": 30})
        checa("ack do refill", ack.get("ok"), ack)
        checa("porta abriu", await espera_porta(f, ("opening", "open"), 2.0), porta(f))
        await espera_porta(f, ("open",))
        checa("ack do close", (await f.request({"t": "cmd", "op": "close"})).get("ok"))
        checa("porta fechou", await espera_porta(f, ("closed",)), porta(f))

        etapa("sincronizacao do historico")
        ack, eventos, fim = await f.sync(retrato["lastSeq"] or 0)
        checa("ack do sync", ack and ack.get("ok"), ack)
        checa("syncEnd no fim", fim is not None, fim)
        seqs = [e.get("seq") for e in eventos]
        checa("eventos em ordem, sem repetir", seqs == sorted(set(seqs)), seqs)
        if fim:
            checa("syncEnd.last = ultimo evento", not seqs or fim.get("last") == seqs[-1], (fim, seqs))
        print(f"   {len(eventos)} evento(s) criados pelo teste")

        etapa("entrada ruim")
        inicio = f.mark()
        await f.send_raw("{quebrado")
        await f.wait_for(lambda: f.last("ack", inicio, id=0) is not None, 2.0)
        checa("JSON quebrado: ack bad_json", (f.last("ack", inicio, id=0) or {}).get("err") == "bad_json")
        checa("tipo desconhecido: bad_type",
              (await f.request({"t": "voar"})).get("err") == "bad_type")

        etapa("tamanho dos pacotes")
        maior = max((len(p) for p in f.raw), default=0)
        checa(f"maior notificacao = {maior} bytes, cabe no MTU {f.mtu} (limite {f.mtu - 3})",
              maior <= f.mtu - 3)
        checa("nenhum pedaco perdido na remontagem", f.asm.dropped == 0, f.asm.dropped)
    finally:
        etapa("devolvendo a configuracao de antes do teste")
        ok = await restaura(f, retrato)
        checa("restauracao confirmada pelo comedouro", ok)
        await confere_restauracao(f, retrato)
        if ok:
            print(f"   (o retrato {caminho} pode ser apagado)")


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capturar", help="grava cada mensagem recebida (uma por linha)")
    ap.add_argument("--restaurar", help="so devolve a configuracao de um retrato salvo")
    args = ap.parse_args()

    dev = await find_device()
    if dev is None:
        sys.exit(2)
    print(f"conectando em {dev.address}...")

    async with BleakClient(dev) as client:
        f = Feeder(client)
        await f.start()
        print(f"conectado, MTU {f.mtu}")
        try:
            if args.restaurar:
                with open(args.restaurar, encoding="utf-8") as arq:
                    retrato = json.load(arq)
                await f.request({"t": "hello", "v": 2, "now": int(time.time())})
                ok = await restaura(f, retrato)
                checa("restauracao confirmada pelo comedouro", ok)
                await confere_restauracao(f, retrato)
            else:
                print("ATENCAO: a porta vai abrir e fechar varias vezes.")
                await teste(f)
        finally:
            if args.capturar:
                with open(args.capturar, "w", encoding="utf-8") as arq:
                    for texto, _ in f.msgs:
                        arq.write(texto + "\n")
                print(f"\n{len(f.msgs)} mensagens gravadas em {args.capturar}")
            await f.stop()

    print("\n" + "=" * 50)
    print("TUDO PASSOU" if falhas == 0 else f"{falhas} VERIFICACAO(OES) FALHARAM")
    sys.exit(0 if falhas == 0 else 1)


if __name__ == "__main__":
    asyncio.run(main())
