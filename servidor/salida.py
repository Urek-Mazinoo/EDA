r"""servidor/salida.py -- ★ (21-nov) LO QUE SALE MIENTRAS UN JUEGO CARGA LA STAGE.

Medido en la prueba entre dos casas del 21-nov (server de prueba, 05:35 UTC) y en el exe:
  · mientras carga, el cliente atiende el TCP desde el HILO DE CARGA (`0x5345C0`): por cada mensaje, si el
    comando está entre 0x1003 y 0x100B llama a `+0x10` de la escena de carga = «Thread DoEnd!!» (0x530980 →
    0x74E150: pone la bandera de fin del hilo). El 0x1005 (el chat, y los «[MSN]» del Messenger) cae ahí.
  · la largada (0x105E) la atiende SÓLO ese hilo (PROTOCOLO §5.10). Si el hilo ya terminó, el 0x105E no se
    procesa nunca y el hilo principal queda para siempre en `while (!this[0xE0]) Sleep(1)` (0x500643):
    «NOW LOADING», sin mandar ni el «estoy vivo».
  · Al anfitrión le llegó un 0x1005 (261 B) a las 05:35:52, ENTRE su 0x105F y el 0x105E de las 05:35:53 (el
    invitado tardó 1 s más en cargar). Al invitado, el 0x105E antes que el 0x1005: entró. En las partidas de
    dos del 18-nov que anduvieron, el 0x1005 llegó siempre DESPUÉS del 0x105E.
Entonces: desde el 0x104F (empieza a cargar) hasta su 0x105E, a ese jugador no se le manda nada de 0x1003 a
0x100B: se guarda y sale, en el mismo orden, justo después de su 0x105E (o si vuelve al lobby).
"""
import time

from servidor.estado import PARTIDAS, SALAS, partida

PELIGROSOS = range(0x1003, 0x100C)             # los que terminan el hilo de carga (0x5345C0)
TOPE = 200                                      # mensajes guardados como mucho por jugador
VENCE = 180.0                                   # s: si la carga no termina en esto, se sueltan igual

# 1054 (expulsión) y 1058 (estado/máster) también pertenecen a la sala de espera.
DE_SALA = {0x1054, 0x1058}
DE_STAGE = DE_SALA | {0x105D, 0x1060, 0x1062, 0x106E, 0x106F, 0x1071, 0x1072, 0x1073, 0x1075}


def registrar_sala(s, conexion):
    """Recupera la visita de un miembro que ya estaba en una sala o stage al recargar."""
    p = partida(conexion)
    if "visita_salida" not in p:
        en_stage = conexion in s.get("en_stage", ()) or p.get("cartas_hasta")
        p["visita_salida"] = ("stage" if en_stage else "sala", object())


# El estado vive entre recargas. Registrar las visitas existentes antes de que un manejador
# quite miembros o cierre la partida para preparar los paquetes de despedida.
for _sala in SALAS.values():
    for _conexion in _sala["miembros"]:
        registrar_sala(_sala, _conexion)


def vincular(n, resp):
    """Conserva la visita del destinatario cuando el envío queda pendiente."""
    pendientes = []
    for r in resp:
        d = r[3] if len(r) > 3 else n
        if r[0] in DE_STAGE and len(r) < 5:
            visita = PARTIDAS.get(d, {}).get("visita_salida")
            r = (*r[:3], d, visita)
        pendientes.append(r)
    return pendientes


def sin_stage(n, resp, registrar):
    """Si falla la guardia, conserva sólo las respuestas ajenas a la stage."""
    seguros = []
    for r in resp:
        if r[0] in DE_STAGE:
            registrar("#%s descarto %04x: falló la guardia" % (r[3] if len(r) > 3 else n, r[0]))
        else:
            seguros.append(r)
    return seguros


def ordenar(n_defecto, resp, ahora=None):
    """Ordena respuestas (comando, cuerpo, espera[, destino[, visita]]) para el transporte.

    Los paquetes diferidos de stage conservan la visita hasta que vence su espera.
    La escena sigue viva durante las cartas aunque la sala ya haya cerrado el juego.
    """
    ahora = time.monotonic() if ahora is None else ahora
    sale = []
    for r in resp:
        c, cuerpo, espera = r[0], r[1], r[2]
        d = r[3] if len(r) > 3 else n_defecto
        if c == 0x104F and not espera:
            partida(d)["visita_salida"] = ("stage", object())
        p = PARTIDAS.get(d)
        if c in DE_STAGE:
            visita = (p or {}).get("visita_salida")
            if visita is None or (len(r) > 4 and visita is not r[4]):
                motivo = "salió o cambió de partida"
            elif c not in DE_SALA and visita[0] != "stage":
                motivo = "fuera de la stage"
            else:
                motivo = None
            if motivo:
                print("#%s descarto %04x: %s" % (d, c, motivo), flush=True)
                continue
            if espera:
                sale.append((c, cuerpo, espera, d, visita))
                continue
        if espera:
            sale.append((c, cuerpo, espera, d))
            continue
        if p is None:
            sale.append((c, cuerpo, 0, d))
            continue
        if p.get("cargando") and ahora - p["cargando"] > VENCE:
            sale += _soltar(p, d, "la carga pasó de %d s" % VENCE)
        if p.get("cargando") and c in PELIGROSOS:
            g = p.setdefault("diferidos", [])
            if len(g) >= TOPE:
                g.pop(0)
            g.append((c, cuerpo))
            continue
        sale.append((c, cuerpo, 0, d))
        if (c == 0x1072 or (c in (0x101E, 0x1020, 0x104D) and cuerpo[:2] == b"\1\0")
                or (c == 0x102E and cuerpo[:3] == b"\1\0\x09")):
            p["visita_salida"] = ("sala", object())
        elif c == 0x1075 or (c in (0x1012, 0x1018, 0x1024, 0x1028, 0x1046, 0x1054)
                             and cuerpo[:2] == b"\1\0") or (c == 0x102E and cuerpo[:3] == b"\1\0\3"):
            p["visita_salida"] = None
        # La pantalla GM cambia sólo si el cierre pasó la guardia.
        if c in (0x1054, 0x1075) and cuerpo[:2] == b"\1\0":
            p["pantalla_gm"] = "lobby"
        if c == 0x104F:
            p["cargando"] = ahora
        elif c in (0x105E, 0x1075, 0x1069) and p.get("cargando"):
            sale += _soltar(p, d, "largada" if c == 0x105E else "salió de la stage")
    return sale


def _soltar(p, d, motivo):
    p.pop("cargando", None)
    g = p.pop("diferidos", [])
    if g:
        print("#%s carga: %d mensaje(s) 0x1003–0x100B guardados durante la carga, enviados después (%s)"
              % (d, len(g), motivo), flush=True)
    return [(c, cuerpo, 0, d) for c, cuerpo in g]
