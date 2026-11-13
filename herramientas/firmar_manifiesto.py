r"""firmar_manifiesto -- el manifiesto del cliente de EDA FIRMADO (Ed25519, JWS compacto), para el launcher.

Etapa 4 del plan (13-nov): el launcher de R2Beat (2.6) baja el manifiesto de EDA directo del dominio de
EDA, sin pasar por la sesión de R2Beat, y no baja nada si la firma no verifica. Formato acordado con
ellos: UN archivo (manifiesto y firma juntos, así nunca quedan desparejos en una caché):

    cabecera  {"alg": "EdDSA", "typ": "eda-manifiesto", "kid": sha256(clave pública)[:16] en hex}
    carga     el eda.json de siempre (version, servidor, canje, archivos[{ruta, url, md5, tam}])
              + "aud": "eda-launcher" + "iat"

El launcher RECHAZA: otro alg o kid; otro aud; una firma que no verifica; una versión MENOR que la
última que aceptó (guarda la última: así nadie le vuelve a servir un manifiesto viejo bien firmado).
Hay DOS claves públicas en el launcher: la actual (la privada vive SÓLO en el VPS) y una de RESERVA
guardada fuera de línea, para rotar sin sacar otro launcher.

    py herramientas\firmar_manifiesto.py generar <archivo de la privada>      # una clave nueva
    py herramientas\firmar_manifiesto.py firmar <eda.json> <privada> > manifiesto.jws
    py herramientas\firmar_manifiesto.py verificar <manifiesto.jws> <pública b64url> [versión mínima]
    py herramientas\firmar_manifiesto.py control                              # ★ con sus reveses
"""
import base64
import hashlib
import json
import os
import sys
import time

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:                                            # noqa: BLE001
    pass

AUD = "eda-launcher"
TYP = "eda-manifiesto"


def b64(b):
    return base64.urlsafe_b64encode(b).rstrip(b"=").decode()


def de_b64(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def kid_de(publica_raw):
    return hashlib.sha256(publica_raw).hexdigest()[:16]


def version(v):
    """«2026.11.13.2» → (2026, 11, 13, 2), para comparar."""
    return tuple(int(x) for x in str(v).split("."))


def generar(ruta):
    from Crypto.PublicKey import ECC
    if os.path.exists(ruta):
        raise SystemExit("⛔ ya existe %s: no se pisa una clave" % ruta)
    k = ECC.generate(curve="Ed25519")
    with open(ruta, "w") as f:
        f.write(k.export_key(format="PEM"))
    try:
        os.chmod(ruta, 0o600)
    except OSError:
        pass
    pub = k.public_key().export_key(format="raw")
    return b64(pub), kid_de(pub)


def firmar(manifiesto, privada_pem, ahora=None):
    from Crypto.PublicKey import ECC
    from Crypto.Signature import eddsa
    k = ECC.import_key(privada_pem)
    pub = k.public_key().export_key(format="raw")
    carga = dict(manifiesto, aud=AUD, iat=int(ahora if ahora is not None else time.time()))
    h = b64(json.dumps({"alg": "EdDSA", "typ": TYP, "kid": kid_de(pub)}, separators=(",", ":")).encode())
    d = b64(json.dumps(carga, separators=(",", ":"), ensure_ascii=False).encode())
    firma = eddsa.new(k, "rfc8032").sign((h + "." + d).encode("ascii"))
    return h + "." + d + "." + b64(firma)


def verificar(jws, publicas_b64, version_minima=None):
    """La carga si el launcher la ACEPTARÍA; si no, ValueError con el motivo. `publicas_b64`: las que
    tiene el launcher (la actual y la de reserva)."""
    from Crypto.Signature import eddsa
    partes = jws.strip().split(".")
    if len(partes) != 3:
        raise ValueError("formato")
    h, d, f = partes
    cab, carga = json.loads(de_b64(h)), json.loads(de_b64(d))
    if cab.get("alg") != "EdDSA" or cab.get("typ") != TYP:
        raise ValueError("algoritmo o tipo")
    claves = {kid_de(de_b64(p)): de_b64(p) for p in publicas_b64}
    if cab.get("kid") not in claves:
        raise ValueError("kid desconocido")
    try:
        eddsa.new(eddsa.import_public_key(claves[cab["kid"]]), "rfc8032").verify(
            (h + "." + d).encode("ascii"), de_b64(f))
    except ValueError:
        raise ValueError("firma")
    if carga.get("aud") != AUD:
        raise ValueError("aud")
    if version_minima and version(carga.get("version", "0")) < version(version_minima):
        raise ValueError("versión vieja")
    return carga


def control():
    import tempfile
    bien = []

    def chequear(n, c):
        bien.append(c)
        print("  %s %s" % ("✅" if c else "⛔", n))

    def rechazo(jws, pubs, vmin=None):
        try:
            verificar(jws, pubs, vmin)
            return None
        except ValueError as e:
            return str(e)
    with tempfile.TemporaryDirectory() as d:
        pub, kid = generar(os.path.join(d, "actual.pem"))
        pub2, _ = generar(os.path.join(d, "otra.pem"))
        priv = open(os.path.join(d, "actual.pem")).read()
        priv2 = open(os.path.join(d, "otra.pem")).read()
        m = {"version": "2026.11.13.2", "servidor": "eda.example:25900", "canje": "https://x/eda/pase",
             "archivos": [{"ruta": "d3d9.dll", "url": "https://x/d3d9.dll", "md5": "0" * 32, "tam": 1}]}
        jws = firmar(m, priv, ahora=1700000000)
        c = verificar(jws, [pub, pub2], "2026.11.13.1")
        chequear("firmado → verifica, con la carga entera + aud + iat",
                 c["version"] == m["version"] and c["archivos"] == m["archivos"] and c["aud"] == AUD)
        chequear("kid = sha256(pública)[:16] (%s)" % kid, json.loads(de_b64(jws.split(".")[0]))["kid"] == kid)
        h, dd, f = jws.split(".")
        trucho = b64(json.dumps(dict(json.loads(de_b64(dd)), servidor="malo:1")).encode())
        chequear("★ el revés: cambiarle la carga sin volver a firmar → «firma»",
                 rechazo(h + "." + trucho + "." + f, [pub]) == "firma")
        chequear("★ el revés: firmado con OTRA clave que el launcher no tiene → «kid desconocido»",
                 rechazo(firmar(m, priv2), [pub]) == "kid desconocido")
        chequear("…y con la de RESERVA en el launcher, sí verifica (rotar sin otro launcher)",
                 rechazo(firmar(m, priv2), [pub, pub2]) is None)
        chequear("★ el revés: una versión MENOR que la última aceptada → «versión vieja»",
                 rechazo(jws, [pub], "2026.11.14") == "versión vieja")
        otra_aud = firmar(m, priv)
        hh, d2, _ = otra_aud.split(".")
        from Crypto.PublicKey import ECC
        from Crypto.Signature import eddsa
        d3 = b64(json.dumps(dict(json.loads(de_b64(d2)), aud="otro")).encode())
        f3 = b64(eddsa.new(ECC.import_key(priv), "rfc8032").sign((hh + "." + d3).encode()))
        chequear("★ el revés: bien firmado pero para otro destinatario (aud) → «aud»",
                 rechazo(hh + "." + d3 + "." + f3, [pub]) == "aud")
        chequear("★ el revés: basura → «formato»", rechazo("no-es-un-jws", [pub]) == "formato")
    print("→ TODO BIEN" if all(bien) else "→ ⛔ algo falló")
    return all(bien)


def main():
    a = sys.argv[1:]
    if not a or a[0] == "control":
        return 0 if control() else 1
    if a[0] == "generar":
        pub, kid = generar(a[1])
        print("pública (raw, b64url): %s\nkid: %s\n(la PRIVADA quedó en %s: no se publica)" % (pub, kid, a[1]))
        return 0
    if a[0] == "firmar":
        print(firmar(json.load(open(a[1], encoding="utf-8")), open(a[2]).read()))
        return 0
    if a[0] == "verificar":
        c = verificar(open(a[1]).read(), [a[2]], a[3] if len(a) > 3 else None)
        print("✅ el launcher lo aceptaría: versión %s, %d archivos" % (c.get("version"), len(c.get("archivos", []))))
        return 0
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
