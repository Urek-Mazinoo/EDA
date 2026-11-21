#!/usr/bin/env python3
"""laboratorio/lab.py — compila y prueba EDA FUERA de la PC del dueño (nube de Claude, GitHub Actions o cualquier Linux).

    python3 laboratorio/lab.py rapido escala2d graficos   # sólo esas suites (durante el desarrollo)
    python3 laboratorio/lab.py regresion                   # todas (antes de entregar una candidata)
    python3 laboratorio/lab.py estado                      # imprime laboratorio/ESTADO.md (lo primero que se lee)
    python3 laboratorio/lab.py comparar A B                # dos resultados (carpetas o resumen.json)

Cada corrida deja laboratorio/resultados/<fecha>-<commit>/ con un .log COMPLETO por suite y resumen.json; en la
pantalla sólo sale el resumen (una línea por suite y, si falla, las líneas que dicen MAL/error, máx. 8). Una suite
que no se puede correr acá (falta zig, falta pycryptodome) queda «omitida» con el motivo: nunca «bien».
Al final reescribe la parte automática de laboratorio/ESTADO.md (la parte a mano está en laboratorio/estado.json).
"""
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.path.join(RAIZ, "laboratorio")
DLLD = os.path.join(RAIZ, "herramientas", "dll_d3d9")
TMP = os.environ.get("RUNNER_TEMP") or os.environ.get("TEMP") or "/tmp"
WIN = os.name == "nt"
EXE = ".exe" if WIN else ""


def zig():
    """zig: el de $ZIG (el buzón lo pasa: repo/_trabajo/zig/…/zig.exe) o el del PATH."""
    z = os.environ.get("ZIG")
    return z if z and os.path.exists(z) else shutil.which("zig")


def cc():
    """El compilador de las pruebas en C: gcc (Linux/Actions) o, si no hay, «zig cc» (la PC con Windows)."""
    if shutil.which("gcc"):
        return ["gcc"], True
    z = zig()
    return ([z, "cc"], False) if z else (None, False)


def sh(cmd, cwd=RAIZ, env=None, timeout=900):
    e = dict(os.environ, PYTHONIOENCODING="utf-8", EDA_SIN_PANELES="1")   # (20-nov) las pruebas nunca abren paneles en la PC
    e.update(env or {})
    p = subprocess.run(cmd, cwd=cwd, env=e, shell=isinstance(cmd, str), capture_output=True, text=True,
                       errors="replace", timeout=timeout)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def hay(prog):
    return shutil.which(prog) is not None


def _deps():
    """(20-nov) pycryptodome sin instalar nada: el repo\\_trabajo\\deps16 del juego ya lo trae (3.23). En la nube: None."""
    for d in (os.path.join(RAIZ, "_trabajo", "deps16"), os.path.join(os.path.dirname(RAIZ), "repo", "_trabajo", "deps16")):
        if os.path.isdir(os.path.join(d, "Crypto")):
            return d
    return None


DEPS = _deps()
ENV_DEPS = {"PYTHONPATH": DEPS} if DEPS else {}


def py_tiene(mod):
    return sh([sys.executable, "-c", "import " + mod], env=ENV_DEPS)[0] == 0


def contar(salida):
    """«N bien · M mal» de las pruebas en C; si no está, None."""
    m = re.findall(r"(\d+) bien · (\d+) mal", salida)
    return (int(m[-1][0]), int(m[-1][1])) if m else None


def prueba_c(nombre, fuente, extra=(), args=()):
    def correr():
        comp, es_gcc = cc()
        if not comp:
            return "omitida", "ni gcc ni zig", "sin compilador de C"
        exe = os.path.join(TMP, "lab_" + nombre + EXE)
        san = ["-g", "-fsanitize=address"] if nombre == "graficos" and es_gcc else []
        # «graficos» en Windows (zig cc) a 32 bits, como la d3d9.dll: modela LockRect con el puntero en un DWORD
        # (unsigned long), que en Windows de 64 bits mide 4 bytes y lo trunca (0xC0000005).
        # (escala2d sigue en 64 bits: su prueba define WINAPI vacío y a 32 bits choca con el stdcall de cursor_falso)
        if WIN and not es_gcc and nombre == "graficos":
            san = ["-target", "x86-windows-gnu"]
        rc, out = sh([*comp, "-std=gnu11", *san, "-o", exe, os.path.join(DLLD, fuente), *extra] + (["-lm"] if not WIN else []))
        if rc:
            return "mal", out, "no compila"
        rc, out2 = sh([exe, *args])
        c = contar(out2)
        det = "%d bien · %d mal" % c if c else ("código %d" % rc)
        return ("bien" if rc == 0 and (not c or c[1] == 0) else "mal"), out + out2, det
    return correr


def s_version():
    rc, out = sh([sys.executable, "prueba_version.py"], cwd=DLLD)
    return ("bien" if rc == 0 else "mal"), out, out.strip().splitlines()[-1][:90] if out.strip() else ""


def s_canal():
    rc, out = sh([sys.executable, "canal.py", "control"], cwd=DLLD)
    return ("bien" if rc == 0 else "mal"), out, out.strip().splitlines()[-1][:90] if out.strip() else ""


def s_server():
    if not py_tiene("Crypto"):
        return "omitida", "falta pycryptodome (pip install pycryptodome==3.24.1)", "falta pycryptodome"
    rc, out = sh([sys.executable, "servidor/juego.py", "control"], env=dict(ENV_DEPS, EDA_EXIGIR_DATOS="1"))
    return ("bien" if rc == 0 else "mal"), out, out.strip().splitlines()[-1][:90] if out.strip() else ""


def s_relay():
    rc, out = sh([sys.executable, "herramientas/relay_udp.py", "control"])
    return ("bien" if rc == 0 else "mal"), out, out.strip().splitlines()[-1][:90] if out.strip() else ""


def s_piezas():
    if not os.path.exists(os.path.join(RAIZ, "exe", "EDA_limpio.exe")):
        return "omitida", "falta exe/EDA_limpio.exe", "falta el exe"
    if not py_tiene("Crypto"):
        return "omitida", "falta pycryptodome", "falta pycryptodome"
    rc, out = sh([sys.executable, "herramientas/contar_piezas.py", "control"], env=ENV_DEPS)
    return ("bien" if rc == 0 else "mal"), out, out.strip().splitlines()[-1][:90] if out.strip() else ""


def s_dll():
    """La d3d9.dll con la receta del repo (zig 0.16.0), DOS veces: tiene que salir igual (reproducible)."""
    zig_ = zig()
    if not zig_:
        return "omitida", "no hay zig en este entorno (en GitHub Actions sí)", "falta zig"
    log, md5s = "", []
    rc, out = sh([sys.executable, "version.py", "--header", "eda_version.h"], cwd=DLLD)
    log += out
    for i in (1, 2):
        rc, out = sh([zig_, "cc", "-target", "x86-windows-gnu", "-shared", "-O2", "-s", "-o", "d3d9.dll", "eda_d3d9.c",
                      "d3d9.def", "-lkernel32", "-luser32", "-lwinmm", "-lws2_32", "-liphlpapi", "-lgdi32"], cwd=DLLD)
        log += out
        if rc:
            return "mal", log, "no compila (compilación %d)" % i
        md5s.append(hashlib.md5(open(os.path.join(DLLD, "d3d9.dll"), "rb").read()).hexdigest())
    if md5s[0] != md5s[1]:
        return "mal", log, "NO reproducible: %s ≠ %s" % tuple(md5s)
    return "bien", log + "\nmd5 " + md5s[0], "md5 " + md5s[0]


def s_sonda():
    """La sonda de Direct3D9 (laboratorio/sonda/) como .exe de Windows de 32 bits. ACÁ sólo se COMPILA: correrla
    necesita Windows (lo hace el trabajo «windows» de .github/workflows/laboratorio.yml)."""
    d = os.path.join(LAB, "sonda")
    out_exe = os.path.join(TMP, "sonda.exe")
    if all(hay(x) for x in ("clang", "lld-link", "llvm-dlltool")):
        log = ""
        for lib in ("kernel32", "user32"):
            rc, out = sh(["llvm-dlltool", "-m", "i386", "-k", "-d", os.path.join(d, lib + ".def"),
                          "-l", os.path.join(TMP, "lib%s.a" % lib)])
            log += out
        rc, out = sh(["clang", "-target", "i686-pc-windows-gnu", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-DSIN_CRT",
                      "-fno-builtin", "-c", "-o", os.path.join(TMP, "sonda.o"), os.path.join(d, "sonda_d3d9.c")])
        log += out
        if rc:
            return "mal", log, "no compila"
        rc, out = sh(["lld-link", "/subsystem:windows", "/entry:entrada@0", "/Brepro", "/out:" + out_exe, os.path.join(TMP, "sonda.o"),
                      os.path.join(TMP, "libkernel32.a"), os.path.join(TMP, "libuser32.a"), "/nodefaultlib"])
        log += out
    elif zig():
        rc, log = sh([zig(), "cc", "-target", "x86-windows-gnu", "-O2", "-o", out_exe, os.path.join(d, "sonda_d3d9.c"),
                      "-lkernel32", "-luser32"])
    else:
        return "omitida", "ni clang+lld ni zig", "sin compilador para Windows"
    if rc or not os.path.exists(out_exe):
        return "mal", log, "no enlaza"
    md5 = hashlib.md5(open(out_exe, "rb").read()).hexdigest()
    if ".exe" in log and "warning" in log.lower():
        return "mal", log, "con advertencias"
    return "bien", log + "\nmd5 " + md5, "compilada (no ejecutada: falta Windows) · md5 " + md5[:8]


SUITES = {
    "escala2d": prueba_c("escala2d", "prueba_escala2d.c"),
    "graficos": prueba_c("graficos", "prueba_graficos.c"),
    "teclas": prueba_c("teclas", "prueba_teclas.c"),
    "version": s_version,
    "canal": s_canal,
    "server": s_server,
    "relay": s_relay,
    "piezas": s_piezas,
    "dll": s_dll,
    "sonda": s_sonda,
}


def commit():
    rc, out = sh(["git", "log", "-1", "--format=%h %s"])
    return out.strip()[:100] if rc == 0 else "?"


def correr(nombres):
    c = commit()
    carpeta = os.path.join(os.environ.get("LAB_ESTADO_DIR") or LAB, "resultados", time.strftime("%Y%m%d-%H%M%S") + "-" + c.split()[0])
    os.makedirs(carpeta, exist_ok=True)
    res = {"commit": c, "fecha": time.strftime("%Y-%m-%d %H:%M:%S"), "donde": donde(), "suites": {}}
    for n in nombres:
        t0 = time.time()
        try:
            estado, log, det = SUITES[n]()
        except Exception as ex:                                   # noqa: BLE001
            estado, log, det = "mal", repr(ex), "excepción del laboratorio"
        open(os.path.join(carpeta, n + ".log"), "w", encoding="utf-8").write(log)
        res["suites"][n] = {"estado": estado, "detalle": det, "segundos": round(time.time() - t0, 1)}
        marca = {"bien": "✅", "mal": "❌", "omitida": "⏸"}[estado]
        print("%s %-9s %s" % (marca, n, det))
        if estado == "mal":
            for l in [l for l in log.splitlines() if re.search(r"\bMAL\b|error|Error|Traceback", l)][:8]:
                print("     " + l[:150])
    json.dump(res, open(os.path.join(carpeta, "resumen.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("registro completo: " + os.path.relpath(carpeta, RAIZ))
    escribir_estado(res)
    return 0 if all(s["estado"] != "mal" for s in res["suites"].values()) else 1


def donde():
    if os.environ.get("GITHUB_ACTIONS"):
        return "GitHub Actions %s · %s" % (os.environ.get("RUNNER_OS", "?"), os.environ.get("GITHUB_RUN_ID", ""))
    return ("PC (buzón)" if WIN else "nube") + " (%s)" % platform.node()[:20]


def escribir_estado(res):
    """ESTADO.md: la parte a mano (estado.json) + el ÚLTIMO resultado de cada suite (ultimas.json: una corrida «rapido»
    no borra lo que dijo la regresión de las otras). Breve a propósito: es lo primero que se lee."""
    m = json.load(open(os.path.join(LAB, "estado.json"), encoding="utf-8"))
    sal = os.environ.get("LAB_ESTADO_DIR") or LAB          # (el buzón lo manda afuera: la copia de trabajo queda limpia)
    os.makedirs(sal, exist_ok=True)
    ru = os.path.join(sal, "ultimas.json")
    ult = json.load(open(ru, encoding="utf-8")) if os.path.exists(ru) else {}
    for n, x in res["suites"].items():
        ult[n] = dict(x, fecha=res["fecha"], commit=res["commit"].split()[0], donde=res["donde"])
    json.dump(ult, open(ru, "w", encoding="utf-8"), ensure_ascii=False, indent=1, sort_keys=True)
    L = ["# EDA — estado (generado por laboratorio/lab.py; lo fijo está en estado.json)", "",
         "- **Último commit:** " + res["commit"],
         "- **Última DLL candidata:** " + m["dll_candidata"],
         "- **Referencias aprobadas a mano:** " + " · ".join(m["referencias"]),
         "- **Próximo objetivo:** " + m["proximo"], "",
         "## Aprobado (a mano, en el juego)"] + ["- " + x for x in m["aprobado"]] + [
         "", "## Pendiente"] + ["- " + x for x in m["pendiente"]] + [
         "", "## Pruebas automáticas (último resultado de cada una)"]
    for n in SUITES:
        if n in ult:
            s = ult[n]
            L.append("- %s %s: %s — %s · %s · %s" % ({"bien": "✅", "mal": "❌", "omitida": "⏸"}[s["estado"]], n, s["detalle"],
                                                   s["commit"], s["fecha"][5:16], s["donde"]))
    L += ["", "_Pruebas automáticas ≠ prueba visual: nada de esto aprueba una pantalla del juego._", ""]
    open(os.path.join(sal, "ESTADO.md"), "w", encoding="utf-8").write("\n".join(L))


def comparar(a, b):
    def cargar(x):
        return json.load(open(os.path.join(x, "resumen.json") if os.path.isdir(x) else x, encoding="utf-8"))
    A, B = cargar(a), cargar(b)
    print("A: %s · %s\nB: %s · %s" % (A["commit"], A["fecha"], B["commit"], B["fecha"]))
    for n in sorted(set(A["suites"]) | set(B["suites"])):
        x, y = A["suites"].get(n, {}), B["suites"].get(n, {})
        if x.get("estado") != y.get("estado") or x.get("detalle") != y.get("detalle"):
            print("  %-9s %s (%s) → %s (%s)" % (n, x.get("estado", "—"), x.get("detalle", ""), y.get("estado", "—"),
                                               y.get("detalle", "")))
    return 0


def main(a):
    if not a or a[0] == "estado":
        print(open(os.path.join(LAB, "ESTADO.md"), encoding="utf-8").read())
        return 0
    if a[0] == "regresion":
        return correr(list(SUITES))
    if a[0] == "rapido":
        malas = [n for n in a[1:] if n not in SUITES]
        if malas or len(a) < 2:
            print("suites: " + " ".join(SUITES))
            return 2
        return correr(a[1:])
    if a[0] == "comparar" and len(a) == 3:
        return comparar(a[1], a[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
