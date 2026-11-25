/* eda_ganchos.h — ★ (24-nov) GUARDIA GENERAL DE REENTRADA PARA TODOS LOS GANCHOS DEL DISPOSITIVO D3D9.
 *   Un overlay (NVIDIA, Discord, Steam, Windhawk…) puede engancharse ENCIMA del nuestro en la vtable del
 *   dispositivo: su «original» apunta a nuestra función. Cuando reafirmar_ganchos nos vuelve a poner arriba
 *   y guarda el suyo como «real», la cadena queda: nuestro → overlay → nuestro → … (recursión infinita /
 *   juego colgado al arrancar o al cambiar resolución).
 *
 *   Esta guardia mantiene por cada ranura de gancho:
 *     - real:    el siguiente en la cadena (el overlay, si reafirmar lo encadenó) → el overlay sigue funcionando;
 *     - orig:    la primera dirección verificada dentro de d3d9.dll de System32/SysWOW64 (VirtualQuery →
 *                AllocationBase == g_real);
 *     - primero: el primer puntero ajeno visto al instalar (respaldo si otro overlay ya estaba antes que nosotros);
 *     - dentro[hilo][gancho]: contador por hilo, sin locks en el camino rápido (comparar un entero en hilos[0]).
 *   Si nuestra función vuelve a entrar en el mismo hilo (el overlay nos devolvió la llamada), salta DIRECTO a
 *   orig (o a primero si es distinto de real), ejecutando nuestro cuerpo UNA sola vez y d3d9 UNA vez por intento.
 *
 *   Sin dependencias de Windows: se comparte entre eda_d3d9.c, eda_escala2d.c, eda_graficos.c y las pruebas. */
#ifndef EDA_GANCHOS_H
#define EDA_GANCHOS_H

#include <stddef.h>
#include <string.h>

#ifndef EDA_D3DERR_INVALIDCALL
#define EDA_D3DERR_INVALIDCALL ((long)0x8876086CL)
#endif

#ifndef EDA_HILO_ACTUAL
#define EDA_HILO_ACTUAL() 1ul
#endif

enum {
    EDA_G_RESET = 0,          /* vt 16 */
    EDA_G_PRESENT,            /* vt 17 */
    EDA_G_CTEX,               /* vt 23 */
    EDA_G_CCUBETEX,           /* vt 24 */
    EDA_G_CVOLTEX,            /* vt 25 */
    EDA_G_CVB,                /* vt 26 */
    EDA_G_CIB,                /* vt 27 */
    EDA_G_CRT,                /* vt 28 */
    EDA_G_CDS,                /* vt 29 */
    EDA_G_STRETCH,            /* vt 34 */
    EDA_G_SETRT,              /* vt 37 */
    EDA_G_SETTRANSFORM,       /* vt 44 */
    EDA_G_GETTRANSFORM,       /* vt 45 */
    EDA_G_SETVIEWPORT,        /* vt 47 */
    EDA_G_GETVIEWPORT,        /* vt 48 */
    EDA_G_CSB,                /* vt 59 */
    EDA_G_ENDSB,              /* vt 61 */
    EDA_G_SETTEX,             /* vt 65 */
    EDA_G_SETSAMPLER,         /* vt 69 */
    EDA_G_SETSCISSOR,         /* vt 75 */
    EDA_G_GETSCISSOR,         /* vt 76 */
    EDA_G_DP,                 /* vt 81 */
    EDA_G_DIP,                /* vt 82 */
    EDA_G_DPUP,               /* vt 83 */
    EDA_G_DIPUP,              /* vt 84 */
    EDA_G_SETFVF,             /* vt 89 */
    EDA_G_SSS,                /* vt 100 */
    EDA_G_SETIB,              /* vt 104 */
    EDA_G_GETIB,              /* vt 105 */
    EDA_G_NUM,
    EDA_G_MAX = 32
};

#define EDA_G_HILOS 16

typedef int (*EDA_G_EN_D3D9)(const void *p);
typedef void (*EDA_G_AL_REENTRAR)(int id, const char *nombre, void *directo, long veces);

typedef struct {
    void *real;               /* siguiente en la cadena (puede ser un overlay) */
    void *orig;               /* original verificado dentro de d3d9.dll de System32/SysWOW64 */
    void *primero;            /* primer puntero ajeno visto al instalar */
    void *nuestro;            /* nuestra función gancho */
    int vt_idx;
    const char *nombre;
    long reentradas;
} EDA_GANCHO;

typedef struct {
    volatile unsigned long tid;
    unsigned char dentro[EDA_G_MAX];
} EDA_G_HILO;

typedef struct {
    EDA_GANCHO s[EDA_G_MAX];
    EDA_G_HILO hilos[EDA_G_HILOS];
    EDA_G_EN_D3D9 en_d3d9;
    EDA_G_AL_REENTRAR al_reentrar;
} EDA_GANCHOS;

static EDA_GANCHOS g_ganchos;

static inline void eda_g_iniciar(EDA_GANCHOS *t, EDA_G_EN_D3D9 en_d3d9, EDA_G_AL_REENTRAR al_reentrar)
{
    memset(t, 0, sizeof(*t));
    t->en_d3d9 = en_d3d9;
    t->al_reentrar = al_reentrar;
}

/* Al crear un dispositivo nuevo (otra vtable): limpia punteros previos pero conserva callbacks y contadores de hilo. */
static inline void eda_g_reiniciar_slots(EDA_GANCHOS *t)
{
    if (!t) return;
    memset(t->s, 0, sizeof(t->s));
}

static inline void eda_g_registrar(EDA_GANCHOS *t, int id, int vt_idx, const char *nombre, void *nuestro, void *actual)
{
    if (!t || id < 0 || id >= EDA_G_NUM) return;
    EDA_GANCHO *g = &t->s[id];
    g->vt_idx = vt_idx;
    g->nombre = nombre;
    g->nuestro = nuestro;
    if (actual && actual != nuestro) {
        g->real = actual;
        if (!g->primero) g->primero = actual;
        if (!g->orig && (!t->en_d3d9 || t->en_d3d9(actual))) g->orig = actual;
    }
}

static inline void eda_g_al_reafirmar(EDA_GANCHOS *t, int id, void *nuevo_real)
{
    if (!t || id < 0 || id >= EDA_G_NUM) return;
    EDA_GANCHO *g = &t->s[id];
    if (!nuevo_real || nuevo_real == g->nuestro) return;
    g->real = nuevo_real;
    if (!g->primero) g->primero = nuevo_real;
    if (!g->orig && (!t->en_d3d9 || t->en_d3d9(nuevo_real))) g->orig = nuevo_real;
}

/* Destino seguro en una reentrada: nunca nuestro gancho y nunca un círculo. */
static inline void *eda_g_directo(const EDA_GANCHOS *t, int id)
{
    if (!t || id < 0 || id >= EDA_G_NUM) return NULL;
    const EDA_GANCHO *g = &t->s[id];
    if (g->orig && g->orig != g->nuestro) return g->orig;
    if (g->primero && g->primero != g->nuestro && g->primero != g->real) return g->primero;
    return NULL;
}

/* Ranura del hilo actual: el camino común (hilo de render = hilos[0]) es UNA comparación entera. */
static inline EDA_G_HILO *eda_g_hilo(EDA_GANCHOS *t, unsigned long tid)
{
    if (!tid) tid = 1ul;
    if (t->hilos[0].tid == tid) return &t->hilos[0];
    for (int i = 1; i < EDA_G_HILOS; i++)
        if (t->hilos[i].tid == tid) return &t->hilos[i];
    for (int i = 0; i < EDA_G_HILOS; i++) {
        if (t->hilos[i].tid == 0) {
#ifdef EDA_G_CAS
            if (EDA_G_CAS(&t->hilos[i].tid, tid, 0) == 0) return &t->hilos[i];
#else
            t->hilos[i].tid = tid;
            return &t->hilos[i];
#endif
        }
    }
    return &t->hilos[EDA_G_HILOS - 1];
}

/* Devuelve 1 si es la entrada externa (el llamador DEBE llamar a eda_g_salir al terminar).
 * Devuelve 0 si es REENTRADA en el mismo hilo: *directo_out recibe la función de d3d9.dll (o NULL si no hay segura). */
static inline int eda_g_entrar(EDA_GANCHOS *t, int id, unsigned long tid, void **directo_out)
{
    if (!t || id < 0 || id >= EDA_G_NUM) return 1;
    EDA_G_HILO *h = eda_g_hilo(t, tid);
    if (h->dentro[id]) {
        long n = ++t->s[id].reentradas;
        void *d = eda_g_directo(t, id);
        if (directo_out) *directo_out = d;
        if (t->al_reentrar) t->al_reentrar(id, t->s[id].nombre, d, n);
        return 0;
    }
    h->dentro[id] = 1;
    return 1;
}

static inline void eda_g_salir(EDA_GANCHOS *t, int id, unsigned long tid)
{
    if (!t || id < 0 || id >= EDA_G_NUM) return;
    EDA_G_HILO *h = eda_g_hilo(t, tid);
    h->dentro[id] = 0;
}

/* Atajos para el cuerpo de cada gancho: si hay reentrada, salta directo al original sin correr nuestro código. */
#define EDA_G_ENTRAR_HR(id, tipo_fn, ...)                                                            \
    unsigned long _eda_tid = EDA_HILO_ACTUAL();                                                      \
    void *_eda_dir = NULL;                                                                           \
    if (!eda_g_entrar(&g_ganchos, (id), _eda_tid, &_eda_dir)) {                                      \
        if (!_eda_dir) return EDA_D3DERR_INVALIDCALL;                                                \
        return ((tipo_fn)_eda_dir)(__VA_ARGS__);                                                     \
    }

#define EDA_G_SALIR(id) eda_g_salir(&g_ganchos, (id), _eda_tid)

#endif /* EDA_GANCHOS_H */
