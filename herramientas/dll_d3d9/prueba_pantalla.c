/* prueba_pantalla.c — ★ (24-nov) pruebas automáticas de:
 *   A. Guardia general de reentrada en todos los ganchos D3D9 (Reset normal, Reset con Plan B de 3 intentos,
 *      orig verificado dentro de d3d9.dll, overlay previo + segundo overlay, corte seguro si no hay orig,
 *      Present / CreateVertexBuffer / SetStreamSource / DrawPrimitive / DrawIndexedPrimitive / los 10 de
 *      escala2d / gráficos / índices / Create*, y aislamiento por hilo).
 *   B. Corrección de la fuga de referencia COM en CD3DDynamicLightWorld (0x6EE5BB / 0x6EECB1) con firmas de
 *      EDA_limpio.exe y red de seguridad POOL_DEFAULT (clasificación, desconocidos bloquean, compactación).
 *   C. Pantalla completa ↔ ventana en monitores 1920×1080 y 1280×1024 (5:4) + alineación del mouse.
 *
 *   gcc -std=gnu11 -Wall -Werror -o /tmp/pp herramientas/dll_d3d9/prueba_pantalla.c -lm && /tmp/pp exe/EDA_limpio.exe
 *   zig cc -target x86-windows-gnu -std=gnu11 -Wall -Werror -o prueba_pantalla.exe herramientas/dll_d3d9/prueba_pantalla.c */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long g_test_tid = 100ul;
#define EDA_HILO_ACTUAL() (g_test_tid)

#include "eda_pantalla.h"
#include "eda_ganchos.h"
#include "eda_recursos.h"

/* lee n bytes del exe en la dirección virtual va (EDA_limpio.exe: sección 0 con VirtualAddress 0x1000 y
 *   PointerToRawData 0x1000 → offset = va − 0x400000, como la prueba de la fuga de índices) */
static int leer_pe_va(const char *ruta, unsigned long va, unsigned char *dst, size_t n)
{
    FILE *f = fopen(ruta, "rb");
    if (!f) return 0;
    int ok = va > 0x00400000ul && fseek(f, (long)(va - 0x00400000ul), SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
    fclose(f);
    return ok;
}

static int ok, mal;
static void chequear(const char *que, int si)
{
    printf("  %s  %s\n", si ? "OK " : "MAL", que);
    if (si) ok++;
    else mal++;
}

/* ════════════════════════ PARTE A: DISPOSITIVO + OVERLAY DE MENTIRA ════════════════════════ */
typedef long (*F_RESET)(void *, void *);
typedef long (*F_GEN)(void *, int);

static int g_cuerpo_reset_n, g_ov_reset_n, g_d3d_reset_n, g_ov1_reset_n;
static int g_d3d_fail_aa, g_d3d_max_w, g_d3d_max_h;
static int g_reentradas_cb;
static void *g_vt_fake[128];

/* Simula VirtualQuery → AllocationBase == g_real: sólo estas funciones «viven» en d3d9.dll */
static long d3d9_reset(void *dev, void *pp)
{
    (void)dev;
    g_d3d_reset_n++;
    unsigned long *p = (unsigned long *)pp;
    if (g_d3d_fail_aa && p[4] > 0) return EDA_D3DERR_INVALIDCALL;
    if (g_d3d_max_w && ((int)p[0] > g_d3d_max_w || (int)p[1] > g_d3d_max_h)) return EDA_D3DERR_INVALIDCALL;
    return 0;
}

static int g_d3d_gen_n[EDA_G_MAX], g_ov_gen_n[EDA_G_MAX], g_mi_gen_n[EDA_G_MAX];
static long d3d9_gen(void *dev, int id)
{
    (void)dev;
    g_d3d_gen_n[id]++;
    return 0;
}

static int ptr_en_d3d9_falso(const void *p)
{
    return p == (const void *)d3d9_reset || p == (const void *)d3d9_gen;
}
static void on_reentrada_falso(int id, const char *nom, void *dir, long veces)
{
    (void)id; (void)nom; (void)dir; (void)veces;
    g_reentradas_cb++;
}

/* Overlay que guarda el puntero que estaba en la vtable (el nuestro) y nos vuelve a llamar */
static F_RESET g_ov_reset_sig;
static long overlay_reset(void *dev, void *pp)
{
    g_ov_reset_n++;
    return g_ov_reset_sig(dev, pp);
}

/* Primer overlay instalado ANTES que nosotros (su siguiente es d3d9_reset, no nosotros) */
static F_RESET g_ov1_reset_sig;
static long overlay1_reset(void *dev, void *pp)
{
    g_ov1_reset_n++;
    return g_ov1_reset_sig(dev, pp);
}

/* Nuestro Reset con Plan B simplificado (misma estructura que mi_reset → mi_reset_cuerpo → gr_reset_plan_b) */
static F_RESET g_mi_reset_real;
static int g_bueno_w = 800, g_bueno_h = 600;
static long mi_reset_cuerpo_test(void *dev, void *pp)
{
    g_cuerpo_reset_n++;
    unsigned long *p = (unsigned long *)pp;
    long hr = g_mi_reset_real(dev, pp);
    if (hr == 0) return 0;
    if (p[4] > 0) {
        p[4] = 0;
        hr = g_mi_reset_real(dev, pp);
        if (hr == 0) return 0;
    }
    if ((int)p[0] != g_bueno_w || (int)p[1] != g_bueno_h) {
        p[0] = (unsigned long)g_bueno_w;
        p[1] = (unsigned long)g_bueno_h;
        hr = g_mi_reset_real(dev, pp);
    }
    return hr;
}
static long mi_reset_test(void *dev, void *pp)
{
    EDA_G_ENTRAR_HR(EDA_G_RESET, F_RESET, dev, pp);
    long hr = mi_reset_cuerpo_test(dev, pp);
    EDA_G_SALIR(EDA_G_RESET);
    return hr;
}

/* Gancho genérico para probar TODOS los demás slots (Present, CVB, SSS, DP, DIP, escala2d, gráficos, índices, Create*) */
static F_GEN g_mi_gen_real[EDA_G_MAX];
static F_GEN g_ov_gen_sig[EDA_G_MAX];
static long overlay_gen(void *dev, int id)
{
    g_ov_gen_n[id]++;
    return g_ov_gen_sig[id](dev, id);
}
static long mi_gen_test(void *dev, int id)
{
    EDA_G_ENTRAR_HR(id, F_GEN, dev, id);
    g_mi_gen_n[id]++;
    long hr = g_mi_gen_real[id](dev, id);
    EDA_G_SALIR(id);
    return hr;
}

static void instalar_hook(int gid, int idx, const char *nom, void *nuestro, void **real)
{
    *real = g_vt_fake[idx];
    g_vt_fake[idx] = nuestro;
    eda_g_registrar(&g_ganchos, gid, idx, nom, nuestro, *real);
}
static void reafirmar_hook(int gid, int idx, void *nuestro, void **real)
{
    void *ahora = g_vt_fake[idx];
    if (!ahora || ahora == nuestro) return;
    *real = ahora;
    g_vt_fake[idx] = nuestro;
    eda_g_al_reafirmar(&g_ganchos, gid, ahora);
}

/* ════════════════════════ PARTE B: SIMULADOR DE CD3DDynamicLightWorld ════════════════════════ */
typedef struct {
    int ref;
    int pool;
    int destruido;
} FAKE_IB;

static EDA_FUGA_IB g_fuga_t;
static FAKE_IB *g_dev_ib;
static void ib_addref(FAKE_IB *b) { if (b) b->ref++; }
static int ib_release(FAKE_IB *b)
{
    if (!b) return 0;
    if (--b->ref == 0) b->destruido = 1;
    return b->ref;
}
static void fake_soltar_pend(void *ib, void **slot, void *ctx)
{
    (void)ctx;
    if (slot && *slot == ib) *slot = NULL;
    ib_release((FAKE_IB *)ib);
}
static long fake_getib_hook(unsigned long ret_addr, void **pp_ib)
{
    /* Simula IDirect3DDevice9::GetIndices: suma +1 ref al IB puesto */
    *pp_ib = g_dev_ib;
    if (g_dev_ib) ib_addref(g_dev_ib);
    eda_fuga_ib_al_get(&g_fuga_t, ret_addr, pp_ib, 0);
    return 0;
}
static long fake_setib_hook(unsigned long ret_addr, void *ib)
{
    /* Simula IDirect3DDevice9::SetIndices: toma ref del nuevo y suelta el anterior */
    FAKE_IB *nuevo = (FAKE_IB *)ib;
    if (nuevo != g_dev_ib) {
        if (nuevo) ib_addref(nuevo);
        if (g_dev_ib) ib_release(g_dev_ib);
        g_dev_ib = nuevo;
    }
    void **slot = NULL;
    if (eda_fuga_ib_al_set(&g_fuga_t, ret_addr, ib, &slot)) {
        if (slot && *slot == ib) *slot = NULL;
        ib_release((FAKE_IB *)ib);
    }
    return 0;
}
/* Simula Reset: falla con 0x8876086C si queda algún IB POOL_DEFAULT con ref > 0 */
static long fake_reset_comprueba(FAKE_IB **lista, int n)
{
    for (int i = 0; i < n; i++)
        if (lista[i] && lista[i]->pool == 0 && lista[i]->ref > 0) return EDA_D3DERR_INVALIDCALL;
    return 0;
}

static void probar_parte_a(void)
{
    printf("== Parte A: guardia de Reset y todos los ganchos con overlay en círculo ==\n");
    void *dev = (void *)0x1234;

    /* 1. Reset normal con overlay encima + reafirmar */
    eda_g_iniciar(&g_ganchos, ptr_en_d3d9_falso, on_reentrada_falso);
    g_cuerpo_reset_n = g_ov_reset_n = g_d3d_reset_n = g_reentradas_cb = 0;
    g_d3d_fail_aa = 0; g_d3d_max_w = 0; g_d3d_max_h = 0;
    g_vt_fake[16] = (void *)d3d9_reset;
    instalar_hook(EDA_G_RESET, 16, "Reset", (void *)mi_reset_test, (void **)&g_mi_reset_real);
    /* Overlay se engancha encima de nosotros y guarda nuestro puntero */
    g_ov_reset_sig = (F_RESET)g_vt_fake[16];
    g_vt_fake[16] = (void *)overlay_reset;
    /* reafirmar_ganchos nos vuelve a poner arriba y encadena al overlay */
    reafirmar_hook(EDA_G_RESET, 16, (void *)mi_reset_test, (void **)&g_mi_reset_real);

    unsigned long pp[14] = {1280, 720, 22, 1, 0, 0, 1, 0, 1, 1, 75, 0, 0, 0};
    long hr = ((F_RESET)g_vt_fake[16])(dev, pp);
    chequear("Reset con overlay en círculo: cuerpo 1×, overlay 1×, d3d9 1×, sin recursión",
             hr == 0 && g_cuerpo_reset_n == 1 && g_ov_reset_n == 1 && g_d3d_reset_n == 1
             && g_ganchos.s[EDA_G_RESET].reentradas == 1 && g_reentradas_cb == 1);
    chequear("orig verificado dentro de d3d9.dll y distinto del overlay",
             g_ganchos.s[EDA_G_RESET].orig == (void *)d3d9_reset
             && g_ganchos.s[EDA_G_RESET].real == (void *)overlay_reset);

    /* 2. Reset con Plan B (3 intentos: falla AA → falla tamaño → último tamaño bueno) pasando por overlay cada vez */
    g_cuerpo_reset_n = g_ov_reset_n = g_d3d_reset_n = g_reentradas_cb = 0;
    g_ganchos.s[EDA_G_RESET].reentradas = 0;
    g_d3d_fail_aa = 1; g_d3d_max_w = 1280; g_d3d_max_h = 720;
    g_bueno_w = 1280; g_bueno_h = 720;
    unsigned long pp_pb[14] = {1920, 1080, 22, 1, 4, 0, 1, 0, 1, 1, 75, 0, 0, 0};
    hr = ((F_RESET)g_vt_fake[16])(dev, pp_pb);
    chequear("Reset Plan B (3 intentos) con overlay: cuerpo 1×, overlay 3×, d3d9 3×, cae a 1280×720 sin AA",
             hr == 0 && g_cuerpo_reset_n == 1 && g_ov_reset_n == 3 && g_d3d_reset_n == 3
             && g_ganchos.s[EDA_G_RESET].reentradas == 3 && pp_pb[0] == 1280 && pp_pb[1] == 720 && pp_pb[4] == 0);

    /* 3. Caso extremo: orig NO está en d3d9.dll y primero == real (no hay camino seguro) → nunca recursión infinita */
    eda_g_iniciar(&g_ganchos, ptr_en_d3d9_falso, on_reentrada_falso);
    g_cuerpo_reset_n = g_ov_reset_n = g_d3d_reset_n = 0;
    g_bueno_w = 800; g_bueno_h = 600;
    g_ov_reset_sig = mi_reset_test;
    g_vt_fake[16] = (void *)overlay_reset;
    instalar_hook(EDA_G_RESET, 16, "Reset", (void *)mi_reset_test, (void **)&g_mi_reset_real);
    unsigned long pp_sin[14] = {800, 600, 22, 1, 0, 0, 1, 0, 1, 1, 75, 0, 0, 0};
    hr = ((F_RESET)g_vt_fake[16])(dev, pp_sin);
    chequear("sin orig en d3d9.dll y con bucle cerrado: corta con INVALIDCALL, nunca recursión infinita",
             hr == EDA_D3DERR_INVALIDCALL && g_cuerpo_reset_n == 1 && g_ov_reset_n == 1 && g_d3d_reset_n == 0
             && g_ganchos.s[EDA_G_RESET].orig == NULL && eda_g_directo(&g_ganchos, EDA_G_RESET) == NULL);

    /* 4. Overlay 1 ya estaba antes que nosotros; luego Overlay 2 se engancha encima y reafirmamos */
    eda_g_iniciar(&g_ganchos, ptr_en_d3d9_falso, on_reentrada_falso);
    g_cuerpo_reset_n = g_ov_reset_n = g_ov1_reset_n = g_d3d_reset_n = 0;
    g_d3d_fail_aa = 0; g_d3d_max_w = 0; g_d3d_max_h = 0;
    g_ov1_reset_sig = d3d9_reset;
    g_vt_fake[16] = (void *)overlay1_reset;
    instalar_hook(EDA_G_RESET, 16, "Reset", (void *)mi_reset_test, (void **)&g_mi_reset_real);
    g_ov_reset_sig = (F_RESET)g_vt_fake[16];
    g_vt_fake[16] = (void *)overlay_reset;
    reafirmar_hook(EDA_G_RESET, 16, (void *)mi_reset_test, (void **)&g_mi_reset_real);
    unsigned long pp_ov2[14] = {1280, 720, 22, 1, 0, 0, 1, 0, 1, 1, 75, 0, 0, 0};
    hr = ((F_RESET)g_vt_fake[16])(dev, pp_ov2);
    chequear("overlay1 previo + overlay2 encima: reentrada usa overlay1 (distinto de overlay2) → d3d9 1×",
             hr == 0 && g_cuerpo_reset_n == 1 && g_ov_reset_n == 1 && g_ov1_reset_n == 1 && g_d3d_reset_n == 1);

    /* 5. Si después aparece el original de d3d9.dll al reafirmar, se captura orig */
    g_vt_fake[16] = (void *)d3d9_reset;
    reafirmar_hook(EDA_G_RESET, 16, (void *)mi_reset_test, (void **)&g_mi_reset_real);
    chequear("si otro programa devuelve el original de d3d9.dll en la vtable, reafirmar captura orig",
             g_ganchos.s[EDA_G_RESET].orig == (void *)d3d9_reset);

    /* 6. TODOS los demás ganchos del dispositivo (Present, CVB, SSS, DP, DIP, escala2d, gráficos, índices, Create*) */
    static const struct { int gid; int idx; const char *nom; } TODOS[] = {
        {EDA_G_PRESENT, 17, "Present"},
        {EDA_G_CTEX, 23, "CreateTexture"},
        {EDA_G_CCUBETEX, 24, "CreateCubeTexture"},
        {EDA_G_CVOLTEX, 25, "CreateVolumeTexture"},
        {EDA_G_CVB, 26, "CreateVertexBuffer"},
        {EDA_G_CIB, 27, "CreateIndexBuffer"},
        {EDA_G_CRT, 28, "CreateRenderTarget"},
        {EDA_G_CDS, 29, "CreateDepthStencilSurface"},
        {EDA_G_STRETCH, 34, "StretchRect"},
        {EDA_G_SETRT, 37, "SetRenderTarget"},
        {EDA_G_SETTRANSFORM, 44, "SetTransform"},
        {EDA_G_GETTRANSFORM, 45, "GetTransform"},
        {EDA_G_SETVIEWPORT, 47, "SetViewport"},
        {EDA_G_GETVIEWPORT, 48, "GetViewport"},
        {EDA_G_CSB, 59, "CreateStateBlock"},
        {EDA_G_ENDSB, 61, "EndStateBlock"},
        {EDA_G_SETTEX, 65, "SetTexture"},
        {EDA_G_SETSAMPLER, 69, "SetSamplerState"},
        {EDA_G_SETSCISSOR, 75, "SetScissorRect"},
        {EDA_G_GETSCISSOR, 76, "GetScissorRect"},
        {EDA_G_DP, 81, "DrawPrimitive"},
        {EDA_G_DIP, 82, "DrawIndexedPrimitive"},
        {EDA_G_DPUP, 83, "DrawPrimitiveUP"},
        {EDA_G_DIPUP, 84, "DrawIndexedPrimitiveUP"},
        {EDA_G_SETFVF, 89, "SetFVF"},
        {EDA_G_SSS, 100, "SetStreamSource"},
        {EDA_G_SETIB, 104, "SetIndices"},
        {EDA_G_GETIB, 105, "GetIndices"},
    };
    int n_todos = (int)(sizeof(TODOS) / sizeof(TODOS[0]));
    eda_g_iniciar(&g_ganchos, ptr_en_d3d9_falso, on_reentrada_falso);
    memset(g_d3d_gen_n, 0, sizeof(g_d3d_gen_n));
    memset(g_ov_gen_n, 0, sizeof(g_ov_gen_n));
    memset(g_mi_gen_n, 0, sizeof(g_mi_gen_n));
    int todos_ok = 1;
    for (int i = 0; i < n_todos; i++) {
        int gid = TODOS[i].gid, idx = TODOS[i].idx;
        g_vt_fake[idx] = (void *)d3d9_gen;
        instalar_hook(gid, idx, TODOS[i].nom, (void *)mi_gen_test, (void **)&g_mi_gen_real[gid]);
        g_ov_gen_sig[gid] = (F_GEN)g_vt_fake[idx];
        g_vt_fake[idx] = (void *)overlay_gen;
        reafirmar_hook(gid, idx, (void *)mi_gen_test, (void **)&g_mi_gen_real[gid]);
        long r = ((F_GEN)g_vt_fake[idx])(dev, gid);
        if (r != 0 || g_mi_gen_n[gid] != 1 || g_ov_gen_n[gid] != 1 || g_d3d_gen_n[gid] != 1
            || g_ganchos.s[gid].reentradas != 1 || g_ganchos.s[gid].orig != (void *)d3d9_gen) {
            printf("    falló %s (r=%ld mi=%d ov=%d d3d=%d re=%ld)\n", TODOS[i].nom, r,
                   g_mi_gen_n[gid], g_ov_gen_n[gid], g_d3d_gen_n[gid], g_ganchos.s[gid].reentradas);
            todos_ok = 0;
        }
    }
    chequear("los 28 demás ganchos (Present, CVB, SSS, DP, DIP, escala2d, gráficos, índices, Create*) con overlay en círculo",
             todos_ok && n_todos == EDA_G_NUM - 1);

    /* 7. Aislamiento por hilo: hilo 1 adentro de Present no bloquea hilo 2 */
    void *dir1 = NULL, *dir2 = NULL;
    g_test_tid = 1001ul;
    int e1 = eda_g_entrar(&g_ganchos, EDA_G_PRESENT, g_test_tid, &dir1);
    int e1_re = eda_g_entrar(&g_ganchos, EDA_G_PRESENT, g_test_tid, &dir1);
    g_test_tid = 2002ul;
    int e2 = eda_g_entrar(&g_ganchos, EDA_G_PRESENT, g_test_tid, &dir2);
    eda_g_salir(&g_ganchos, EDA_G_PRESENT, 2002ul);
    eda_g_salir(&g_ganchos, EDA_G_PRESENT, 1001ul);
    g_test_tid = 100ul;
    chequear("guardia por hilo: hilo 1 reentra (va a orig), hilo 2 entra limpio sin bloquearse",
             e1 == 1 && e1_re == 0 && dir1 == (void *)d3d9_gen && e2 == 1);
}

static void probar_parte_b(const char *ruta_exe)
{
    printf("== Parte B: fuga COM en CD3DDynamicLightWorld y red de seguridad POOL_DEFAULT ==\n");

    /* 8. Firmas exactas (sintético + contra EDA_limpio.exe si se pasó) */
    chequear("firmas exactas de 0x6EE5AA (29 B) y 0x6EEC84 (69 B) aceptadas",
             eda_fuga_ib_verificar_bytes(EDA_DL_GET_SIG, EDA_DL_SET_SIG) == 1);
    unsigned char mut[69];
    memcpy(mut, EDA_DL_SET_SIG, sizeof(mut));
    mut[47] ^= 0xFF;
    chequear("1 byte distinto en 0x6EEC84 → firma rechazada (no se activa en exe distinto)",
             eda_fuga_ib_verificar_bytes(EDA_DL_GET_SIG, mut) == 0);
    if (ruta_exe && ruta_exe[0]) {
        FILE *f = fopen(ruta_exe, "rb");
        int exe_ok = 0;
        if (f) {
            /* En EDA_limpio.exe (sección 0: VirtualAddress 0x1000, PointerToRawData 0x1000 → VA - 0x400000 = offset) */
            unsigned char b_get[29], b_set[69];
            if (fseek(f, (long)(EDA_DL_GET_SIG_ADDR - 0x00400000u), SEEK_SET) == 0
                && fread(b_get, 1, sizeof(b_get), f) == sizeof(b_get)
                && fseek(f, (long)(EDA_DL_SET_SIG_ADDR - 0x00400000u), SEEK_SET) == 0
                && fread(b_set, 1, sizeof(b_set), f) == sizeof(b_set)) {
                exe_ok = eda_fuga_ib_verificar_bytes(b_get, b_set);
            }
            fclose(f);
        }
        chequear("EDA_limpio.exe real coincide byte a byte en 0x6EE5AA y 0x6EEC84", exe_ok == 1);
    }

    /* 9a. Reproducción SIN arreglo: SaveState (0x6EE5C1) + RestoreState (0x6EECB7) deja refs=1 → Reset 0x8876086C */
    FAKE_IB hge_ib = {1, 0, 0}, dl_ib = {1, 0, 0};
    FAKE_IB *todos_ib[2] = {&hge_ib, &dl_ib};
    void *m_pOldIB = NULL;
    g_dev_ib = NULL;
    eda_fuga_ib_reiniciar(&g_fuga_t, 0);
    fake_setib_hook(0x0077BE99u, &hge_ib);             /* HGE pone su IB (ref 2) */
    fake_getib_hook(EDA_DL_GET_RET, &m_pOldIB);        /* SaveState (ref 3) */
    fake_setib_hook(0x006EE5F0u, &dl_ib);              /* DynamicLightWorld usa el suyo (hge_ib ref 2) */
    fake_setib_hook(EDA_DL_SET_RET, m_pOldIB);         /* RestoreState restaura sin Release (hge_ib ref 3) */
    /* RestartRender: r_TermRender suelta HGE (ref 2) + dl_ib (ref 0) + gr_reset_desligar SetIndices(NULL) (ref 1) */
    ib_release(&hge_ib);
    ib_release(&dl_ib);
    fake_setib_hook(0x00000001u, NULL);
    chequear("SIN arreglo: tras Stage + r_TermRender queda hge_ib con ref=1 y Reset falla (0x8876086C)",
             hge_ib.ref == 1 && !hge_ib.destruido && fake_reset_comprueba(todos_ib, 2) == EDA_D3DERR_INVALIDCALL);

    /* 9b. Con arreglo ACTIVO: SaveState + RestoreState libera la ref fugada y limpia m_pOldIB → Reset OK */
    hge_ib.ref = 1; hge_ib.destruido = 0;
    dl_ib.ref = 1; dl_ib.destruido = 0;
    m_pOldIB = NULL; g_dev_ib = NULL;
    eda_fuga_ib_reiniciar(&g_fuga_t, 1);
    fake_setib_hook(0x0077BE99u, &hge_ib);
    fake_getib_hook(EDA_DL_GET_RET, &m_pOldIB);
    fake_setib_hook(0x006EE5F0u, &dl_ib);
    fake_setib_hook(EDA_DL_SET_RET, m_pOldIB);
    chequear("CON arreglo: RestoreState limpia m_pOldIB y devuelve hge_ib a ref=2 (HGE + dispositivo)",
             m_pOldIB == NULL && hge_ib.ref == 2 && g_fuga_t.vistas == 1 && g_fuga_t.arregladas == 1 && g_fuga_t.n == 0);
    ib_release(&hge_ib);
    ib_release(&dl_ib);
    fake_setib_hook(0x00000001u, NULL);
    chequear("CON arreglo: tras r_TermRender hge_ib queda destruido (ref=0) y Reset → D3D_OK (0)",
             hge_ib.ref == 0 && hge_ib.destruido == 1 && fake_reset_comprueba(todos_ib, 2) == 0);

    /* 9c. También con un IB del Stage (p. ej. luz dinámica durante combate) y sin RestoreState → drenado en Reset */
    FAKE_IB stage_ib = {1, 0, 0};
    FAKE_IB *lista_st[1] = {&stage_ib};
    void *m_pOldIB2 = NULL;
    g_dev_ib = NULL;
    fake_setib_hook(0x006AAEE0u, &stage_ib);
    fake_getib_hook(EDA_DL_GET_RET, &m_pOldIB2);       /* SaveState SIN RestoreState */
    ib_release(&stage_ib);                             /* r_TermRender suelta stage_ib */
    fake_setib_hook(0x00000001u, NULL);                /* gr_reset_desligar */
    int dren = eda_fuga_ib_drenar(&g_fuga_t, fake_soltar_pend, NULL);
    chequear("SaveState sin RestoreState (IB del Stage): drenado antes de Reset → ref=0 y Reset → D3D_OK",
             dren == 1 && m_pOldIB2 == NULL && stage_ib.ref == 0 && stage_ib.destruido == 1
             && fake_reset_comprueba(lista_st, 1) == 0);

    /* 9d. Llamadores normales de GetIndices/SetIndices NO se tocan */
    FAKE_IB otro_ib = {1, 0, 0};
    void *tmp_ib = NULL;
    long v0 = g_fuga_t.vistas, a0 = g_fuga_t.arregladas;
    fake_setib_hook(0x00650100u, &otro_ib);
    fake_getib_hook(0x00650120u, &tmp_ib);
    fake_setib_hook(0x00650140u, tmp_ib);
    ib_release((FAKE_IB *)tmp_ib);
    fake_setib_hook(0x00650160u, NULL);
    chequear("GetIndices/SetIndices desde otros sitios del exe no se alteran",
             g_fuga_t.vistas == v0 && g_fuga_t.arregladas == a0 && otro_ib.ref == 1);

    /* 10. Red de seguridad POOL_DEFAULT: clasificación, desconocidos bloquean, compactación */
    chequear("clasificador: HGE (0x77B90D), LithTech (0x6EEF28, 0x708B9A) y d3dx9 → MOTOR",
             eda_rec_clasificar(0x0077B90Du, 0, 1, 0, 0) == EDA_RO_MOTOR
             && eda_rec_clasificar(0x006EEF28u, 0, 1, 0, 0) == EDA_RO_MOTOR
             && eda_rec_clasificar(0x00708B9Au, 0, 1, 0, 0) == EDA_RO_MOTOR
             && eda_rec_clasificar(0x0128E217u, 0, 0, 1, 0) == EDA_RO_MOTOR);
    chequear("clasificador: propio → PROPIO · overlay previo → OVERLAY · código del juego (0x451234) → DESCONOCIDO",
             eda_rec_clasificar(0x10001234u, 1, 0, 0, 0) == EDA_RO_PROPIO
             && eda_rec_clasificar(0x60001234u, 0, 0, 0, 1) == EDA_RO_OVERLAY
             && eda_rec_clasificar(0x00451234u, 0, 1, 0, 0) == EDA_RO_DESCONOCIDO
             && eda_rec_clasificar(0x70001234u, 0, 0, 0, 0) == EDA_RO_DESCONOCIDO);

    static EDA_REC_TABLA rt;
    eda_rec_reiniciar(&rt);
    for (int i = 0; i < 750; i++)
        eda_rec_alta(&rt, (void *)(uintptr_t)(0x100000 + i * 16), EDA_RT_VB, 0x00708B9Au, EDA_RO_MOTOR, 4096);
    eda_rec_alta(&rt, (void *)(uintptr_t)0x200010, EDA_RT_SB, 0x10002000u, EDA_RO_PROPIO, 0);
    eda_rec_alta(&rt, (void *)(uintptr_t)0x200020, EDA_RT_RT, 0x10003000u, EDA_RO_PROPIO, 768);
    eda_rec_alta(&rt, (void *)(uintptr_t)0x300010, EDA_RT_TEX, 0x60005000u, EDA_RO_OVERLAY, 65536);
    int desc = -1;
    chequear("750 MOTOR + 2 PROPIO + 1 OVERLAY → seguro para cambio en vivo tras Stage",
             eda_rec_vivo_tras_stage_seguro(&rt, &g_fuga_t, &desc) == 1 && desc == 0 && rt.vivos == 753);

    void *unk = (void *)(uintptr_t)0x400010;
    eda_rec_alta(&rt, unk, EDA_RT_RT, 0x00451234u, EDA_RO_DESCONOCIDO, 262144);
    char lista[160];
    eda_rec_listar_origen(&rt, EDA_RO_DESCONOCIDO, lista, (int)sizeof(lista));
    chequear("1 recurso POOL_DEFAULT desconocido vivo → BLOQUEA cambio en vivo y anota su llamador",
             eda_rec_vivo_tras_stage_seguro(&rt, &g_fuga_t, &desc) == 0 && desc == 1 && strstr(lista, "RT@0x00451234"));

    eda_rec_baja(&rt, unk);
    chequear("al liberarse el recurso desconocido → vuelve a ser seguro",
             eda_rec_vivo_tras_stage_seguro(&rt, &g_fuga_t, &desc) == 1 && desc == 0);

    EDA_FUGA_IB fuga_off = g_fuga_t;
    fuga_off.activo = 0;
    chequear("con arreglo de fuga apagado (exe distinto) → bloquea cambio en vivo tras Stage",
             eda_rec_vivo_tras_stage_seguro(&rt, &fuga_off, &desc) == 0);

    /* Compactación tras muchas altas/bajas (sin desbordar) */
    eda_rec_reiniciar(&rt);
    for (int k = 0; k < 3000; k++) {
        void *p = (void *)(uintptr_t)(0x500000 + k * 32);
        eda_rec_alta(&rt, p, EDA_RT_IB, 0x006EEF28u, EDA_RO_MOTOR, 24576);
        eda_rec_baja(&rt, p);
    }
    chequear("3000 ciclos alta/baja con compactación: 0 vivos y sin desborde",
             rt.vivos == 0 && rt.desbordado == 0 && rt.altas == 3000 && rt.bajas == 3000);
}

static void probar_parte_c(void)
{
    printf("== Parte C: monitores 1920×1080 y 1280×1024 (5:4), pantalla completa y mouse ==\n");
    /* Monitor 1 (1920×1080, 16:9) y Monitor 2 (1280×1024, 5:4) */
    static const struct { int cw, ch, bw, bh; const char *nom; } MODOS[] = {
        {1920, 1080, 1920, 1080, "monitor 1 pantalla completa 1920×1080 (16:9)"},
        {1600, 900,  1600, 900,  "monitor 1 ventana 1600×900 (16:9)"},
        {1280, 1024, 1280, 1024, "monitor 2 pantalla completa 1280×1024 (5:4)"},
        {1024, 768,  1024, 768,  "monitor 2 ventana 1024×768 (4:3)"},
        {1280, 1024, 800,  600,  "monitor 2 sin bordes estirado 800×600 → 1280×1024"},
        {1920, 1080, 1280, 720,  "monitor 1 plan B 1280×720 en cliente 1920×1080"},
    };
    int n_m = (int)(sizeof(MODOS) / sizeof(MODOS[0]));
    for (int i = 0; i < n_m; i++) {
        int cw = MODOS[i].cw, ch = MODOS[i].ch, bw = MODOS[i].bw, bh = MODOS[i].bh;
        int ow, oh;
        pan_limitar_res(bw, bh, PAN_TOPE_DEF_W, PAN_TOPE_DEF_H, &ow, &oh);
        float s = (float)ow / 800.0f < (float)oh / 600.0f ? (float)ow / 800.0f : (float)oh / 600.0f;
        float rx = ((float)ow - 800.0f * s) / 2.0f;
        float ry = ((float)oh - 600.0f * s) / 2.0f;
        int pts[5][2] = {{0, 0}, {799, 0}, {0, 599}, {799, 599}, {400, 300}};
        int bien = (ow == bw && oh == bh);
        for (int k = 0; k < 5; k++) {
            float bx = rx + (float)pts[k][0] * s, by = ry + (float)pts[k][1] * s;
            int cx, cy;
            pan_bb_a_cli(bx, by, ow, oh, cw, ch, &cx, &cy);
            float bx2, by2;
            pan_cli_a_bb(cx, cy, cw, ch, ow, oh, &bx2, &by2);
            int ux = (int)floorf((bx2 - rx) / s + 0.5f);
            int uy = (int)floorf((by2 - ry) / s + 0.5f);
            if (abs(ux - pts[k][0]) > 1 || abs(uy - pts[k][1]) > 1) bien = 0;
        }
        chequear(MODOS[i].nom, bien);
    }
    /* Verifica específicamente que 1280×1024 (5:4) centra verticalmente (rx=0, ry=32, s=1.6) */
    {
        float s = 1280.0f / 800.0f < 1024.0f / 600.0f ? 1280.0f / 800.0f : 1024.0f / 600.0f;
        float rx = (1280.0f - 800.0f * s) / 2.0f, ry = (1024.0f - 600.0f * s) / 2.0f;
        chequear("1280×1024 (monitor secundario 5:4): escala 1,6×, rx=0, ry=32 (bandas arriba/abajo)",
                 fabsf(s - 1.6f) < 1e-4f && fabsf(rx) < 1e-4f && fabsf(ry - 32.0f) < 1e-4f);
    }
}

static int rt_legible_siempre(const void *p, size_t n) { return p != NULL && n > 0; }

static void probar_rt_motor(const char *exe_ruta)
{
    printf("== Parte D: render targets de pantalla de LithTech (0x665320 / 0x664830 / 0x665630) ==\n");
    unsigned char s_get[sizeof(EDA_RT_MGR_GET_SIG)];
    unsigned char s_rec[sizeof(EDA_RT_MGR_RECALL_SIG)];
    unsigned char s_one[sizeof(EDA_RT_RECREATE_SIG)];
    memcpy(s_get, EDA_RT_MGR_GET_SIG, sizeof(s_get));
    memcpy(s_rec, EDA_RT_MGR_RECALL_SIG, sizeof(s_rec));
    memcpy(s_one, EDA_RT_RECREATE_SIG, sizeof(s_one));
    chequear("firmas CD3D_RenderTargetMgr (3 funciones) coinciden → válidas",
             eda_rt_verificar_bytes(s_get, s_rec, s_one) == 1);
    s_one[8] ^= 0xFF;
    chequear("1 byte distinto en CRenderTarget::Recreate → firma rechazada",
             eda_rt_verificar_bytes(s_get, s_rec, s_one) == 0);
    s_one[8] ^= 0xFF;

    if (exe_ruta && exe_ruta[0]) {
        unsigned char r_get[sizeof(EDA_RT_MGR_GET_SIG)] = {0};
        unsigned char r_rec[sizeof(EDA_RT_MGR_RECALL_SIG)] = {0};
        unsigned char r_one[sizeof(EDA_RT_RECREATE_SIG)] = {0};
        int ok_exe = leer_pe_va(exe_ruta, EDA_RT_MGR_GET_ADDR, r_get, sizeof(r_get))
                  && leer_pe_va(exe_ruta, EDA_RT_MGR_RECALL_ADDR, r_rec, sizeof(r_rec))
                  && leer_pe_va(exe_ruta, EDA_RT_RECREATE_ADDR, r_one, sizeof(r_one))
                  && eda_rt_verificar_bytes(r_get, r_rec, r_one);
        chequear("EDA_limpio.exe real: firmas 0x665320 / 0x664830 / 0x665630 verificadas byte a byte", ok_exe);
    }

    /* Árbol RB sintético como std::map<HRENDERTARGET, CRenderTarget*> en 0x00938954:
     *   - rt1: pantalla actual 1920×1080 (liberado tras ReleaseAll) → debe pasar a 800×600
     *   - rt2: pantalla anterior 1280×720 en viejos[] (liberado) → debe pasar a 800×600
     *   - rt3: fijo cuadrado 1024×1024 (p. ej. sombra/cubo/capítulos) → NO se toca
     *   - rt4: 1920×1080 pero todavía vivo (tex != NULL) → NO se toca por seguridad */
    EDA_LT_RT rt1 = {1920, 1080, 1, 0, NULL, NULL};
    EDA_LT_RT rt2 = {1280, 720,  1, 0, NULL, NULL};
    EDA_LT_RT rt3 = {1024, 1024, 0, 0, NULL, NULL};
    EDA_LT_RT rt4 = {1920, 1080, 1, 0, (void *)(uintptr_t)0x1234, NULL};

    EDA_LT_RBNODE head, n1, n2, n3, n4;
    memset(&head, 0, sizeof(head));
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    memset(&n3, 0, sizeof(n3));
    memset(&n4, 0, sizeof(n4));
    head.is_nil = 1;
    head.parent = &n2; /* root */
    head.left = &n1;   /* begin */
    head.right = &n4;  /* last */

    n1.left = &head; n1.parent = &n2; n1.right = &head; n1.key = 1; n1.val = &rt1;
    n2.left = &n1;   n2.parent = &head; n2.right = &n3; n2.key = 2; n2.val = &rt2;
    n3.left = &head; n3.parent = &n2; n3.right = &n4;   n3.key = 3; n3.val = &rt3;
    n4.left = &head; n4.parent = &n3; n4.right = &head; n4.key = 4; n4.val = &rt4;

    const int viejos[2][2] = {{1280, 720}, {1600, 900}};
    int adj1 = eda_rt_ajustar_arbol(&head, 1920, 1080, 800, 600, viejos, 2, rt_legible_siempre);
    chequear("Reset 1920×1080 → 800×600: ajusta los 2 RT de pantalla (1920×1080 y 1280×720) a 800×600",
             adj1 == 2 && rt1.w == 800 && rt1.h == 600 && rt2.w == 800 && rt2.h == 600);
    chequear("RT cuadrado 1024×1024 y RT con textura viva NO se tocan",
             rt3.w == 1024 && rt3.h == 1024 && rt4.w == 1920 && rt4.h == 1080);

    int adj2 = eda_rt_ajustar_arbol(&head, 800, 600, 1280, 720, viejos, 2, rt_legible_siempre);
    chequear("2º Reset 800×600 → 1280×720: ambos RT de pantalla siguen al tamaño nuevo (1280×720)",
             adj2 == 2 && rt1.w == 1280 && rt1.h == 720 && rt2.w == 1280 && rt2.h == 720 && rt3.w == 1024);

    /* Árbol corrupto con ciclo: termina acotado sin colgarse */
    EDA_LT_RBNODE c_head, c_a, c_b;
    memset(&c_head, 0, sizeof(c_head));
    memset(&c_a, 0, sizeof(c_a));
    memset(&c_b, 0, sizeof(c_b));
    c_head.is_nil = 1; c_head.left = &c_a;
    c_a.right = &c_b; c_a.parent = &c_head; c_a.left = &c_head;
    c_b.right = &c_a; c_b.parent = &c_a; c_b.left = &c_head;
    int adj_ciclo = eda_rt_ajustar_arbol(&c_head, 1920, 1080, 800, 600, NULL, 0, rt_legible_siempre);
    chequear("árbol RB corrupto con ciclo termina de forma segura (sin bucle infinito)", adj_ciclo == 0);
}

int main(int argc, char **argv)
{
    probar_parte_a();
    probar_parte_b(argc > 1 ? argv[1] : NULL);
    probar_parte_c();
    probar_rt_motor(argc > 1 ? argv[1] : NULL);
    printf("prueba_pantalla: %d bien · %d mal\n", ok, mal);
    return mal ? 1 : 0;
}
