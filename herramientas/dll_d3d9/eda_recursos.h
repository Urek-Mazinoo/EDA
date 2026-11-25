/* eda_recursos.h — ★ (24-nov) CAMBIO DE PANTALLA EN VIVO TRAS UN STAGE: CORRECCIÓN DE LA FUGA DE REFERENCIA
 *   DE ÍNDICES EN CD3DDynamicLightWorld Y RED DE SEGURIDAD DE RECURSOS POOL_DEFAULT.
 *
 *   Evidencia verificada en EDA_limpio.exe:
 *     - RestartRender (0x00423C40) → r_TermRender(1, 0) (0x00485760):
 *         1) CGameClientShell::OnEvent(4, 0) (0x004E4D74) → HGE::Gfx_OnLostDevice (0x0077BE00) suelta TODOS
 *            los recursos POOL_DEFAULT de HGE (RT de pantalla, DS, lista de RTs +0x524 incl. Capítulos 1024×1024,
 *            IB 12 KB +0x510, VB 96 KB +0x50C, VB 128 B +0x51C, StateBlock +0x520).
 *         2) d3d_Term(0) (0x006637D0) → CD3D_Device::ReleaseAll(0) (0x0065BB60) y CRenderWorld::FreeAll (0x0066C510)
 *            sueltan TODOS los recursos POOL_DEFAULT de LithTech (geometría estática del Stage, CD3DDynamicLightWorld
 *            0x006EE260 → IB 24 KB 0x6EEF28, CModelShadow 0x6EC970 → IB 24 KB 0x6ED2E2).
 *     - LA CAUSA REAL de Reset → 0x8876086C (D3DERR_INVALIDCALL) tras entrar a un Stage:
 *         · CD3DDynamicLightWorld::SaveState (0x006EE2B0) llama en 0x006EE5BB (vuelta 0x006EE5C1) a
 *           IDirect3DDevice9::GetIndices(&[esi+0x90]) (vt 105), que suma +1 referencia COM al IndexBuffer puesto.
 *         · CD3DDynamicLightWorld::RestoreState (0x006EE990) llama en 0x006EECB1 (vuelta 0x006EECB7) a
 *           IDirect3DDevice9::SetIndices([esi+0x90]) (vt 104) y vuelve SIN llamar a [esi+0x90]->Release().
 *         · CD3DDynamicLightWorld::FreeAll (0x006EE260) tampoco suelta [esi+0x90].
 *         Resultado: el IB que estaba puesto al entrar al Stage (típicamente el de 12 KB de HGE, 0x77B90D, o
 *         cualquier IB del Stage durante una luz dinámica) queda con refs=1 tras r_TermRender → Reset falla.
 *
 *   Solución en dos capas (sin cambiar el pool de ningún recurso, 0 costo de FPS):
 *     1) Corrección exacta (solo si las firmas de 0x006EE5AA y 0x006EEC84 coinciden byte a byte): en GetIndices
 *        desde 0x006EE5C1 se anota (slot, ib); en SetIndices desde 0x006EECB7 se limpia *slot y se hace Release()
 *        de esa referencia fugada (y si quedara alguna pendiente antes de Reset, se drena ahí).
 *     2) Red de seguridad POOL_DEFAULT: rastrea los recursos POOL_DEFAULT vivos (Create* / Release → 0) clasificados
 *        por origen (PROPIO de la DLL, MOTOR verificado LithTech/HGE/d3dx9, OVERLAY externo previo, DESCONOCIDO).
 *        Antes de intentar RestartRender tras un Stage, exige: arreglo activo, tabla sin desbordes y 0 recursos
 *        DESCONOCIDOS vivos. Si aparece cualquiera desconocido, cae limpio a «se aplica al reiniciar».
 *
 *   Sin dependencias de Windows: se comparte entre eda_d3d9.c y las pruebas unitarias. */
#ifndef EDA_RECURSOS_H
#define EDA_RECURSOS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── 1. Firmas exactas en EDA_limpio.exe para la fuga COM de CD3DDynamicLightWorld ── */
#define EDA_DL_GET_SIG_ADDR  0x006EE5AAu
#define EDA_DL_GET_RET       0x006EE5C1u
#define EDA_DL_SET_SIG_ADDR  0x006EEC84u
#define EDA_DL_SET_RET       0x006EECB7u
#define EDA_DL_FREEALL_ADDR  0x006EE260u

static const unsigned char EDA_DL_GET_SIG[29] = {
    0xA1, 0xE8, 0x7F, 0x93, 0x00, 0x8B, 0x00, 0x8B, 0x08, 0x8D, 0x96, 0x90, 0x00, 0x00, 0x00,
    0x52, 0x50, 0xFF, 0x91, 0xA4, 0x01, 0x00, 0x00, 0xC6, 0x46, 0x10, 0x01, 0x5E, 0xC3
};

static const unsigned char EDA_DL_SET_SIG[69] = {
    0x8B, 0x0D, 0xE8, 0x7F, 0x93, 0x00, 0x8B, 0xAE, 0x90, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x08,
    0x68, 0xDA, 0x01, 0x00, 0x00, 0x8B, 0xF9, 0xE8, 0xE1, 0x6F, 0xF7, 0xFF, 0x68, 0x89, 0x00,
    0x00, 0x00, 0x8B, 0xCF, 0xE8, 0xD5, 0x6F, 0xF7, 0xFF, 0x8B, 0x3F, 0x8B, 0x07, 0x55, 0x57,
    0xFF, 0x90, 0xA0, 0x01, 0x00, 0x00, 0x5F, 0xC7, 0x86, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xC6, 0x46, 0x10, 0x00, 0x5D, 0x5E, 0xC3
};

static inline int eda_fuga_ib_verificar_bytes(const unsigned char *get_b, const unsigned char *set_b)
{
    if (!get_b || !set_b) return 0;
    return memcmp(get_b, EDA_DL_GET_SIG, sizeof(EDA_DL_GET_SIG)) == 0
        && memcmp(set_b, EDA_DL_SET_SIG, sizeof(EDA_DL_SET_SIG)) == 0;
}

#define EDA_FUGA_CAP 8

typedef struct {
    void **slot;              /* &[esi + 0x90] que recibió GetIndices */
    void *ib;                 /* IDirect3DIndexBuffer9* con referencia sin liberar */
} EDA_FUGA_PEND;

typedef struct {
    int activo;               /* 1 = firmas verificadas y [pantalla] arreglar_fuga_ib=1 */
    int n;
    EDA_FUGA_PEND pend[EDA_FUGA_CAP];
    long vistas;
    long arregladas;
    long limpiadas_reset;
    long desbordes;
} EDA_FUGA_IB;

static inline void eda_fuga_ib_reiniciar(EDA_FUGA_IB *f, int activo)
{
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->activo = activo ? 1 : 0;
}

static inline void eda_fuga_ib_limpiar_pendientes(EDA_FUGA_IB *f)
{
    if (!f) return;
    f->n = 0;
}

/* Llamar DESPUÉS de GetIndices real: si viene de 0x006EE5C1 y devolvió un IB no nulo, se registra la ref fugada. */
static inline int eda_fuga_ib_al_get(EDA_FUGA_IB *f, unsigned long ret_addr, void **pp_ib, long hr)
{
    if (!f || !f->activo || ret_addr != EDA_DL_GET_RET || hr != 0 || !pp_ib || !*pp_ib) return 0;
    if (f->n >= EDA_FUGA_CAP) {
        f->desbordes++;
        return -1;
    }
    f->pend[f->n].slot = pp_ib;
    f->pend[f->n].ib = *pp_ib;
    f->n++;
    f->vistas++;
    return 1;
}

/* Llamar DESPUÉS de SetIndices real: si viene de 0x006EECB7 y coincide con un IB registrado, devuelve 1 y entrega slot. */
static inline int eda_fuga_ib_al_set(EDA_FUGA_IB *f, unsigned long ret_addr, void *ib, void ***slot_out)
{
    if (slot_out) *slot_out = NULL;
    if (!f || !f->activo || ret_addr != EDA_DL_SET_RET || !ib) return 0;
    for (int i = f->n - 1; i >= 0; i--) {
        if (f->pend[i].ib == ib) {
            if (slot_out) *slot_out = f->pend[i].slot;
            for (int j = i; j + 1 < f->n; j++) f->pend[j] = f->pend[j + 1];
            f->n--;
            f->arregladas++;
            return 1;
        }
    }
    return 0;
}

/* Justo antes de Reset: si SaveState corrió sin su RestoreState, suelta las referencias pendientes. */
static inline int eda_fuga_ib_drenar(EDA_FUGA_IB *f, void (*soltar)(void *ib, void **slot, void *ctx), void *ctx)
{
    if (!f || f->n <= 0) return 0;
    int k = 0;
    while (f->n > 0) {
        EDA_FUGA_PEND p = f->pend[--f->n];
        if (soltar && p.ib) soltar(p.ib, p.slot, ctx);
        f->limpiadas_reset++;
        k++;
    }
    return k;
}

/* ── 2. Rastreador liviano de recursos POOL_DEFAULT vivos (red de seguridad) ── */
enum {
    EDA_RT_TEX = 1,
    EDA_RT_CUBETEX = 2,
    EDA_RT_VOLTEX = 3,
    EDA_RT_VB = 4,
    EDA_RT_IB = 5,
    EDA_RT_RT = 6,
    EDA_RT_DS = 7,
    EDA_RT_SB = 8
};

enum {
    EDA_RO_PROPIO = 1,        /* creado por nuestra DLL (g_bloque, g_gr_bloque, g_rj_rt): lo soltamos en mi_reset */
    EDA_RO_MOTOR = 2,         /* LithTech (0x650000..0x740000), HGE (0x776000..0x77E000), d3dx9_29: los suelta r_TermRender */
    EDA_RO_OVERLAY = 3,       /* módulo externo conocido / ya presente antes del 1er Stage: lo suelta su propio Reset */
    EDA_RO_DESCONOCIDO = 4    /* cualquier otro origen: bloquea el cambio en vivo tras un Stage */
};

static inline const char *eda_rec_tipo_nombre(int tipo)
{
    switch (tipo) {
    case EDA_RT_TEX:     return "Tex";
    case EDA_RT_CUBETEX: return "CubeTex";
    case EDA_RT_VOLTEX:  return "VolTex";
    case EDA_RT_VB:      return "VB";
    case EDA_RT_IB:      return "IB";
    case EDA_RT_RT:      return "RT";
    case EDA_RT_DS:      return "DS";
    case EDA_RT_SB:      return "SB";
    default:             return "?";
    }
}

/* Clasifica el llamador de un Create* en POOL_DEFAULT.
 * Rangos verificados en EDA_limpio.exe:
 *   - LithTech renderer: 0x00650000..0x00740000 (d3d_Term 0x6637D0 → ReleaseAll 0x65BB60 + CRenderWorld::FreeAll 0x66C510)
 *   - HGE renderer:      0x00776000..0x0077E000 (HGE::Gfx_OnLostDevice 0x77BE00) */
static inline int eda_rec_clasificar(unsigned long ret_addr, int es_propio, int en_exe, int en_d3dx9, int overlay_conocido)
{
    if (es_propio) return EDA_RO_PROPIO;
    if (en_exe) {
        if ((ret_addr >= 0x00650000u && ret_addr < 0x00740000u)
            || (ret_addr >= 0x00776000u && ret_addr < 0x0077E000u))
            return EDA_RO_MOTOR;
        return EDA_RO_DESCONOCIDO;
    }
    if (en_d3dx9) return EDA_RO_MOTOR;
    if (overlay_conocido) return EDA_RO_OVERLAY;
    return EDA_RO_DESCONOCIDO;
}

#define EDA_REC_CAP 2048      /* potencia de 2; pico observado en Stage ~740 */

typedef struct {
    void *obj;
    unsigned long caller;
    unsigned long bytes;
    unsigned char estado;     /* 0 libre · 1 vivo · 2 borrado (tombstone) */
    unsigned char tipo;
    unsigned char origen;
} EDA_REC_E;

typedef struct {
    EDA_REC_E e[EDA_REC_CAP];
    int vivos;
    int pico;
    int tumbas;
    int desbordado;
    int vivos_origen[5];
    long altas;
    long bajas;
} EDA_REC_TABLA;

static inline void eda_rec_reiniciar(EDA_REC_TABLA *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static inline unsigned eda_rec_hash(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    v = (v >> 3) ^ (v >> 15);
    return (unsigned)(v * 2654435761u) & (EDA_REC_CAP - 1u);
}

static inline void eda_rec_compactar(EDA_REC_TABLA *t)
{
    EDA_REC_E copia[EDA_REC_CAP];
    int n = 0;
    for (int i = 0; i < EDA_REC_CAP; i++)
        if (t->e[i].estado == 1) copia[n++] = t->e[i];
    memset(t->e, 0, sizeof(t->e));
    t->tumbas = 0;
    for (int i = 0; i < n; i++) {
        unsigned idx = eda_rec_hash(copia[i].obj);
        while (t->e[idx].estado == 1) idx = (idx + 1u) & (EDA_REC_CAP - 1u);
        t->e[idx] = copia[i];
    }
}

static inline int eda_rec_alta(EDA_REC_TABLA *t, void *obj, int tipo, unsigned long caller, int origen, unsigned long bytes)
{
    if (!t || !obj) return 0;
    if (origen < 1 || origen > 4) origen = EDA_RO_DESCONOCIDO;
    if (t->tumbas > EDA_REC_CAP / 4 && t->vivos < EDA_REC_CAP - 64) eda_rec_compactar(t);
    unsigned idx = eda_rec_hash(obj);
    int primera_tumba = -1;
    for (int paso = 0; paso < EDA_REC_CAP; paso++) {
        unsigned k = (idx + (unsigned)paso) & (EDA_REC_CAP - 1u);
        unsigned char st = t->e[k].estado;
        if (st == 1 && t->e[k].obj == obj) {
            /* Mismo puntero reutilizado sin pasar por baja (p. ej. wrapper): actualiza origen/datos */
            if (t->e[k].origen != (unsigned char)origen) {
                t->vivos_origen[t->e[k].origen]--;
                t->e[k].origen = (unsigned char)origen;
                t->vivos_origen[origen]++;
            }
            t->e[k].tipo = (unsigned char)tipo;
            t->e[k].caller = caller;
            t->e[k].bytes = bytes;
            return 1;
        }
        if (st == 2 && primera_tumba < 0) primera_tumba = (int)k;
        if (st == 0) {
            int pos = primera_tumba >= 0 ? primera_tumba : (int)k;
            if (primera_tumba >= 0) t->tumbas--;
            t->e[pos].obj = obj;
            t->e[pos].caller = caller;
            t->e[pos].bytes = bytes;
            t->e[pos].estado = 1;
            t->e[pos].tipo = (unsigned char)tipo;
            t->e[pos].origen = (unsigned char)origen;
            t->vivos++;
            t->vivos_origen[origen]++;
            t->altas++;
            if (t->vivos > t->pico) t->pico = t->vivos;
            return 1;
        }
    }
    if (primera_tumba >= 0) {
        t->tumbas--;
        t->e[primera_tumba].obj = obj;
        t->e[primera_tumba].caller = caller;
        t->e[primera_tumba].bytes = bytes;
        t->e[primera_tumba].estado = 1;
        t->e[primera_tumba].tipo = (unsigned char)tipo;
        t->e[primera_tumba].origen = (unsigned char)origen;
        t->vivos++;
        t->vivos_origen[origen]++;
        t->altas++;
        if (t->vivos > t->pico) t->pico = t->vivos;
        return 1;
    }
    t->desbordado = 1;
    return 0;
}

static inline int eda_rec_baja(EDA_REC_TABLA *t, void *obj)
{
    if (!t || !obj || t->vivos <= 0) return 0;
    unsigned idx = eda_rec_hash(obj);
    for (int paso = 0; paso < EDA_REC_CAP; paso++) {
        unsigned k = (idx + (unsigned)paso) & (EDA_REC_CAP - 1u);
        unsigned char st = t->e[k].estado;
        if (st == 0) return 0;
        if (st == 1 && t->e[k].obj == obj) {
            t->e[k].estado = 2;
            t->e[k].obj = NULL;
            t->vivos--;
            t->tumbas++;
            if (t->e[k].origen >= 1 && t->e[k].origen <= 4 && t->vivos_origen[t->e[k].origen] > 0)
                t->vivos_origen[t->e[k].origen]--;
            t->bajas++;
            return 1;
        }
    }
    return 0;
}

/* Red de seguridad antes de intentar RestartRender tras un Stage:
 *   1 = seguro (arreglo activo, sin desbordes, 0 recursos POOL_DEFAULT desconocidos vivos);
 *   0 = NO intentar en vivo (dejar para reinicio, sin riesgo de cierre). */
static inline int eda_rec_vivo_tras_stage_seguro(const EDA_REC_TABLA *t, const EDA_FUGA_IB *f, int *desconocidos_out)
{
    int desc = t ? t->vivos_origen[EDA_RO_DESCONOCIDO] : 0;
    if (desconocidos_out) *desconocidos_out = desc;
    if (!f || !f->activo || f->desbordes > 0) return 0;
    if (!t || t->desbordado) return 0;
    if (desc > 0) return 0;
    return 1;
}

static inline int eda_rec_listar_origen(const EDA_REC_TABLA *t, int origen, char *out, int cap)
{
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!t) return 0;
    int pos = 0, n = 0;
    for (int i = 0; i < EDA_REC_CAP && n < 6; i++) {
        if (t->e[i].estado != 1) continue;
        if (origen > 0 && t->e[i].origen != (unsigned char)origen) continue;
        int w = snprintf(out + pos, (size_t)(cap - pos), "%s%s@0x%08lX(%luB)",
                         n ? ", " : "", eda_rec_tipo_nombre(t->e[i].tipo),
                         t->e[i].caller, t->e[i].bytes);
        if (w <= 0 || pos + w >= cap) break;
        pos += w;
        n++;
    }
    return n;
}

#endif /* EDA_RECURSOS_H */
