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

/* ── 3. Ajuste de render targets de pantalla de LithTech tras un Reset en vivo ──
 * Evidencia verificada en EDA_limpio.exe:
 *   - CD3D_RenderTargetMgr::GetSingleton (0x00665320): instancia global en 0x00938954 (bandera de init en 0x00938964
 *     bit 0; puntero al nodo centinela del std::map<HRENDERTARGET, CRenderTarget*> en 0x00938958 = +4).
 *   - CD3D_RenderTargetMgr::ReleaseAll (0x006647C0, llamada desde CD3D_Device::ReleaseAll 0x0065BB9B antes de Reset)
 *     suelta m_pTexture (+0x10) y m_pDepthStencil (+0x14) dejándolos en NULL.
 *   - CD3D_RenderTargetMgr::RecreateAll (0x00664830, llamada desde CD3D_Device::RestoreAll 0x0065AD20 justo al
 *     volver de Reset == 0) recorre el árbol RB y llama a CRenderTarget::Recreate (0x00665630), que crea la textura
 *     (0x006656B7, vuelta 0x006656BA) y su depth-stencil (0x00665724) con [esi+0] (m_nWidth) y [esi+4] (m_nHeight).
 *   - Al cambiar de resolución en vivo dentro/tras un Stage, m_nWidth/m_nHeight conservaban el tamaño viejo
 *     (p. ej. 1920×1080 en un backbuffer 800×600) → el efecto de distorsión al atacar copiaba/dibujaba desfasado
 *     («efecto agua»). Actualizando m_nWidth/m_nHeight justo después de Reset == 0 (antes de RecreateAll) para los
 *     render targets no cuadrados del tamaño de pantalla anterior, RecreateAll los crea al tamaño nuevo exacto y
 *     CLTRenderMgr::GetRenderTargetDims (0x0045CF30) también devuelve el tamaño nuevo. Los de tamaño fijo cuadrado
 *     (128/256/512/1024×1024) nunca se tocan. */
#define EDA_RT_MGR_GET_ADDR     0x00665320u
#define EDA_RT_MGR_RECALL_ADDR  0x00664830u
#define EDA_RT_RECREATE_ADDR    0x00665630u
#define EDA_RT_MGR_HEAD_ADDR    0x00938958u
#define EDA_RT_MGR_INIT_ADDR    0x00938964u

static const unsigned char EDA_RT_MGR_GET_SIG[27] = {
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF, 0x68, 0xCE, 0x04, 0x87, 0x00, 0x50,
    0xF6, 0x05, 0x64, 0x89, 0x93, 0x00, 0x01, 0x64, 0x89, 0x25, 0x00, 0x00, 0x00
};

static const unsigned char EDA_RT_MGR_RECALL_SIG[24] = {
    0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47, 0x04, 0x8B, 0x30, 0x3B, 0xF0, 0x74,
    0x5B, 0x8D, 0x49, 0x00, 0x8B, 0x4E, 0x10, 0xE8, 0xE8, 0x0D, 0x00, 0x00
};

static const unsigned char EDA_RT_RECREATE_SIG[24] = {
    0x51, 0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x10, 0x85, 0xC0, 0x8D, 0x5E,
    0x10, 0x57, 0x74, 0x0C, 0x8B, 0x08, 0x50, 0xFF, 0x51, 0x08, 0xC7, 0x03
};

static inline int eda_rt_verificar_bytes(const unsigned char *get_b, const unsigned char *recall_b,
                                         const unsigned char *recreate_b)
{
    if (!get_b || !recall_b || !recreate_b) return 0;
    return memcmp(get_b, EDA_RT_MGR_GET_SIG, sizeof(EDA_RT_MGR_GET_SIG)) == 0
        && memcmp(recall_b, EDA_RT_MGR_RECALL_SIG, sizeof(EDA_RT_MGR_RECALL_SIG)) == 0
        && memcmp(recreate_b, EDA_RT_RECREATE_SIG, sizeof(EDA_RT_RECREATE_SIG)) == 0;
}

typedef struct EDA_LT_RT {
    uint32_t w;           /* +0x00 m_nWidth */
    uint32_t h;           /* +0x04 m_nHeight */
    uint32_t color_fmt;   /* +0x08 m_eColorFormat */
    uint32_t depth_fmt;   /* +0x0C m_eDepthFormat */
    void *tex;            /* +0x10 m_pTexture (NULL tras ReleaseAll) */
    void *ds;             /* +0x14 m_pDepthStencil (NULL tras ReleaseAll) */
} EDA_LT_RT;

typedef struct EDA_LT_RBNODE {
    struct EDA_LT_RBNODE *left;    /* +0x00 */
    struct EDA_LT_RBNODE *parent;  /* +0x04 */
    struct EDA_LT_RBNODE *right;   /* +0x08 */
    uint32_t key;                  /* +0x0C HRENDERTARGET */
    EDA_LT_RT *val;                /* +0x10 CRenderTarget* */
    unsigned char color;           /* +0x14 */
    unsigned char is_nil;          /* +0x15 */
    unsigned char pad[2];
} EDA_LT_RBNODE;

typedef int (*EDA_LEGIBLE_FN)(const void *p, size_t n);

static inline int eda_rt_es_de_pantalla(int w, int h, int old_w, int old_h, const int viejos[][2], int n_viejos)
{
    if (w < 320 || h < 200 || w > 8192 || h > 8192 || w == h) return 0;
    if (old_w >= 320 && old_h >= 200 && w == old_w && h == old_h) return 1;
    if (viejos && n_viejos > 0) {
        for (int i = 0; i < n_viejos; i++)
            if (viejos[i][0] == w && viejos[i][1] == h) return 1;
    }
    return 0;
}

static inline int eda_rt_ajustar_arbol(void *head_ptr, int old_w, int old_h, int new_w, int new_h,
                                       const int viejos[][2], int n_viejos, EDA_LEGIBLE_FN ok_leer)
{
    if (!head_ptr || !ok_leer || new_w < 320 || new_h < 200 || new_w > 8192 || new_h > 8192) return 0;
    EDA_LT_RBNODE *head = (EDA_LT_RBNODE *)head_ptr;
    if (!ok_leer(head, sizeof(*head)) || !head->is_nil) return 0;
    EDA_LT_RBNODE *cur = head->left;
    int ajustados = 0;
    for (int paso = 0; paso < 64 && cur && cur != head; paso++) {
        if (!ok_leer(cur, sizeof(*cur)) || cur->is_nil) break;
        EDA_LT_RT *rt = cur->val;
        if (rt && ok_leer(rt, sizeof(*rt)) && rt->tex == NULL && rt->ds == NULL
            && eda_rt_es_de_pantalla((int)rt->w, (int)rt->h, old_w, old_h, viejos, n_viejos)) {
            if ((int)rt->w != new_w || (int)rt->h != new_h) {
                rt->w = (uint32_t)new_w;
                rt->h = (uint32_t)new_h;
                ajustados++;
            }
        }
        /* Avance in-order idéntico a std::map::iterator::operator++ en 0x00664830 */
        if (cur->right && ok_leer(cur->right, sizeof(*cur)) && !cur->right->is_nil) {
            cur = cur->right;
            for (int baj = 0; baj < 64 && cur->left && ok_leer(cur->left, sizeof(*cur)) && !cur->left->is_nil; baj++)
                cur = cur->left;
        } else {
            EDA_LT_RBNODE *p = cur->parent;
            for (int sub = 0; sub < 64 && p && ok_leer(p, sizeof(*p)) && !p->is_nil && cur == p->right; sub++) {
                cur = p;
                p = p->parent;
            }
            cur = p;
        }
    }
    return ajustados;
}

/* ── 4. Ajuste de cámaras de pantalla de LithTech (OT_CAMERA) tras un Reset en vivo ──
 * Evidencia verificada en EDA_limpio.exe y en el registro de sesión (24-nov 22:00:17, 1920×1080 → 800×600 en Stage):
 *   - Al entrar al Stage, 0x0055113B fija la cámara con SetCameraRect(cam, bFullScreen=0, 0, 0, W, H) (0x00410DE0) y
 *     SetCameraFOV(cam, 70°, 70°·H/W) (0x00413F60) una sola vez. Con bFullScreen=0 LithTech NO la redimensiona en
 *     RestartRender.
 *   - En cada cuadro del Stage tras cambiar en vivo de 1920×1080 a 800×600, RenderCamera (0x00417F33) copiaba el
 *     rectángulo viejo (0, 0, 1920, 1080) y m_yFov de 16:9 (39,375°) a SceneDesc (0x009385B0) → d3d_InitView
 *     (0x006632A0 → 0x0065B359) reconstruía g_ViewParams (0x009385E0) con m_Rect = 1920×1080 y proyección 16:9.
 *     Aunque el RT de pantalla (0x006656BA) ya era 800×600, los efectos D3DX (d3dx9_29.dll) recibían rect/proyección
 *     16:9 / 1920×1080 sobre el RT 4:3 de 800×600 → seguía la distorsión («efecto agua») al atacar.
 *   - Lista global de OT_CAMERA (tipo 5) en ObjectMgr: g_pClientMgr = *(void **)0x0092E51C (0x00410B96); en
 *     om_CreateObject (0x004789A0) la lista del tipo t está en g_pClientMgr + 0x264 + t*16 (+4 = centinela LTLink
 *     m_Head), o sea g_pClientMgr + 0x2B8 para OT_CAMERA (5). Cada nodo enlaza en obj + 0x78 con data = obj.
 *   - En CameraInstance: +0xA8 tipo (5), +0x190..+0x19C rect (left/top/right/bottom), +0x1A0 m_xFov, +0x1A4 m_yFov,
 *     +0x1A8 m_bFullScreen. Actualizando las cámaras de pantalla entera tras Reset == 0, RenderCamera y g_ViewParams
 *     pasan a usar el tamaño y la proporción nuevos en el primer cuadro. */
#define EDA_CAM_INSERT_ADDR     0x004789A0u
#define EDA_CAM_SETRECT_ADDR    0x00410DE0u
#define EDA_CAM_SETFOV_ADDR     0x00413F60u
#define EDA_CAM_CREOBJ_ADDR     0x00410B96u
#define EDA_CAM_MGR_ADDR        0x0092E51Cu
#define EDA_CAM_LIST_OFS        0x02B8u
#define EDA_CAM_TYPE_OFS        0x00A8u
#define EDA_CAM_RECT_OFS        0x0190u
#define EDA_CAM_FOV_OFS         0x01A0u
#define EDA_CAM_FULL_OFS        0x01A8u
#define EDA_CAM_OBJ_SIZE        0x01ACu
#define EDA_CAM_TYPE_VAL        5u

static const unsigned char EDA_CAM_INSERT_SIG[26] = {
    0x8D, 0x46, 0x78, 0x89, 0x70, 0x08, 0xC1, 0xE7, 0x04, 0x8D, 0x8C, 0x1F, 0x64,
    0x02, 0x00, 0x00, 0x8D, 0x51, 0x04, 0x89, 0x10, 0x8B, 0x52, 0x04, 0x89, 0x50
};

static const unsigned char EDA_CAM_SETRECT_SIG[24] = {
    0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0, 0x74, 0x3C, 0x80, 0xB8, 0xA8, 0x00,
    0x00, 0x00, 0x05, 0x75, 0x33, 0x0F, 0xB6, 0x4C, 0x24, 0x08, 0x8B, 0x54
};

static const unsigned char EDA_CAM_SETFOV_SIG[24] = {
    0x8B, 0x4C, 0x24, 0x04, 0x85, 0xC9, 0x0F, 0x84, 0x90, 0x00, 0x00, 0x00,
    0x80, 0xB9, 0xA8, 0x00, 0x00, 0x00, 0x05, 0x0F, 0x85, 0x83, 0x00, 0x00
};

static const unsigned char EDA_CAM_CREOBJ_SIG[12] = {
    0x8B, 0x0D, 0x1C, 0xE5, 0x92, 0x00, 0x68, 0xFF, 0xFF, 0x00, 0x00, 0x89
};

static inline int eda_cam_verificar_bytes(const unsigned char *ins_b, const unsigned char *rect_b,
                                          const unsigned char *fov_b, const unsigned char *mgr_b)
{
    if (!ins_b || !rect_b || !fov_b || !mgr_b) return 0;
    return memcmp(ins_b, EDA_CAM_INSERT_SIG, sizeof(EDA_CAM_INSERT_SIG)) == 0
        && memcmp(rect_b, EDA_CAM_SETRECT_SIG, sizeof(EDA_CAM_SETRECT_SIG)) == 0
        && memcmp(fov_b, EDA_CAM_SETFOV_SIG, sizeof(EDA_CAM_SETFOV_SIG)) == 0
        && memcmp(mgr_b, EDA_CAM_CREOBJ_SIG, sizeof(EDA_CAM_CREOBJ_SIG)) == 0;
}

typedef struct EDA_LT_LINK {
    struct EDA_LT_LINK *prev;     /* +0x00 */
    struct EDA_LT_LINK *next;     /* +0x04 */
    void *data;                   /* +0x08 */
} EDA_LT_LINK;

static inline float eda_rec_fabsf(float x) { return x < 0.0f ? -x : x; }

static inline int eda_cam_ratio_viejo(float r, int old_w, int old_h, const int viejos[][2], int n_viejos)
{
    if (old_w >= 320 && old_h >= 200) {
        float ro = (float)old_h / (float)old_w;
        if (eda_rec_fabsf(r - ro) < 0.02f) return 1;
    }
    if (viejos && n_viejos > 0) {
        for (int i = 0; i < n_viejos; i++) {
            if (viejos[i][0] >= 320 && viejos[i][1] >= 200) {
                float rv = (float)viejos[i][1] / (float)viejos[i][0];
                if (eda_rec_fabsf(r - rv) < 0.02f) return 1;
            }
        }
    }
    return 0;
}

static inline int eda_cam_ajustar_lista(void *head_ptr, int old_w, int old_h, int new_w, int new_h,
                                        const int viejos[][2], int n_viejos, EDA_LEGIBLE_FN ok_leer)
{
    if (!head_ptr || !ok_leer || new_w < 320 || new_h < 200 || new_w > 8192 || new_h > 8192) return 0;
    EDA_LT_LINK *head = (EDA_LT_LINK *)head_ptr;
    if (!ok_leer(head, sizeof(*head))) return 0;
    EDA_LT_LINK *cur = head->next;
    int ajustadas = 0;
    float r_nuevo = (float)new_h / (float)new_w;
    for (int paso = 0; paso < 64 && cur && cur != head; paso++) {
        if (!ok_leer(cur, sizeof(*cur))) break;
        unsigned char *cam = (unsigned char *)cur->data;
        if (cam && ok_leer(cam, EDA_CAM_OBJ_SIZE) && cam[EDA_CAM_TYPE_OFS] == EDA_CAM_TYPE_VAL) {
            int32_t left = 0, top = 0, right = 0, bottom = 0, full = 0;
            float fx = 0.0f, fy = 0.0f;
            memcpy(&left, cam + EDA_CAM_RECT_OFS + 0, 4);
            memcpy(&top, cam + EDA_CAM_RECT_OFS + 4, 4);
            memcpy(&right, cam + EDA_CAM_RECT_OFS + 8, 4);
            memcpy(&bottom, cam + EDA_CAM_RECT_OFS + 12, 4);
            memcpy(&fx, cam + EDA_CAM_FOV_OFS + 0, 4);
            memcpy(&fy, cam + EDA_CAM_FOV_OFS + 4, 4);
            memcpy(&full, cam + EDA_CAM_FULL_OFS, 4);
            int tocada = 0;
            if (left == 0 && top == 0
                && eda_rt_es_de_pantalla((int)right, (int)bottom, old_w, old_h, viejos, n_viejos)
                && ((int)right != new_w || (int)bottom != new_h)) {
                int32_t nw = (int32_t)new_w, nh = (int32_t)new_h;
                memcpy(cam + EDA_CAM_RECT_OFS + 8, &nw, 4);
                memcpy(cam + EDA_CAM_RECT_OFS + 12, &nh, 4);
                if (fx > 0.1f && fx < 3.0f) {
                    float fy_nuevo = fx * r_nuevo;
                    memcpy(cam + EDA_CAM_FOV_OFS + 4, &fy_nuevo, 4);
                }
                tocada = 1;
            } else if (full != 0 && fx > 0.1f && fx < 3.0f && fy > 0.1f && fy < 3.0f) {
                float r_cam = fy / fx;
                if (eda_rec_fabsf(r_cam - r_nuevo) > 0.01f
                    && eda_cam_ratio_viejo(r_cam, old_w, old_h, viejos, n_viejos)) {
                    float fy_nuevo = fx * r_nuevo;
                    memcpy(cam + EDA_CAM_FOV_OFS + 4, &fy_nuevo, 4);
                    tocada = 1;
                }
            }
            if (tocada) ajustadas++;
        }
        cur = cur->next;
    }
    return ajustadas;
}

#endif /* EDA_RECURSOS_H */
