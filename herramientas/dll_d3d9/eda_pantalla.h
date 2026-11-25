/* eda_pantalla.h — ★ (24-nov) pantalla segura: tope de resolución proporcional (por defecto 1920×1080) y
 *   conversión cliente ↔ backbuffer para el mouse. Sin dependencias de Windows: se comparte entre eda_escala2d.c,
 *   eda_graficos.c, eda_opciones.c y las pruebas (prueba_graficos.c, prueba_escala2d.c). */
#ifndef EDA_PANTALLA_H
#define EDA_PANTALLA_H

#include <stdio.h>
#include <string.h>
#include <math.h>

#define PAN_TOPE_DEF_W 1920
#define PAN_TOPE_DEF_H 1080

/* Lee [pantalla] tope= del .ini: "1920x1080" por defecto; "0" / "no" / "off" = sin tope (0, 0). */
static inline void pan_parsear_tope(const char *v, int *tw, int *th)
{
    *tw = PAN_TOPE_DEF_W;
    *th = PAN_TOPE_DEF_H;
    if (!v) return;
    while (*v == ' ' || *v == '\t') v++;
    if (!*v) return;
    if (!strcmp(v, "0") || !strcmp(v, "no") || !strcmp(v, "NO") || !strcmp(v, "off") || !strcmp(v, "OFF")) {
        *tw = 0;
        *th = 0;
        return;
    }
    int w = 0, h = 0;
    if (sscanf(v, "%dx%d", &w, &h) == 2 && w >= 800 && h >= 600 && w <= 7680 && h <= 4320) {
        *tw = w;
        *th = h;
    }
}

/* Limita w×h a lo más grande que entre en tw×th manteniendo la proporción w:h.
 *   Ejemplos con 1920×1080: 2560×1440 → 1920×1080, 3840×2160 → 1920×1080, 3440×1440 → 1920×804.
 *   tw <= 0 o th <= 0 = sin tope. Devuelve 1 si se recortó, 0 si quedó igual. */
static inline int pan_limitar_res(int w, int h, int tw, int th, int *ow, int *oh)
{
    if (ow) *ow = w;
    if (oh) *oh = h;
    if (w <= 0 || h <= 0 || tw <= 0 || th <= 0) return 0;
    if (w <= tw && h <= th) return 0;
    long long lhs = (long long)w * th;
    long long rhs = (long long)tw * h;
    int nw, nh;
    if (lhs >= rhs) {
        nw = tw;
        nh = (int)(((long long)h * tw + w / 2) / w);
    } else {
        nh = th;
        nw = (int)(((long long)w * th + h / 2) / h);
    }
    if (nw < 320) nw = 320;
    if (nh < 200) nh = 200;
    if (nw > tw) nw = tw;
    if (nh > th) nh = th;
    if (ow) *ow = nw;
    if (oh) *oh = nh;
    return nw != w || nh != h;
}

/* Conversión cliente (cw×ch) ↔ backbuffer (bw×bh) antes/después del escalador 2D. */
static inline void pan_cli_a_bb(int x, int y, int cw, int ch, int bw, int bh, float *bx, float *by)
{
    if (cw <= 0 || ch <= 0 || bw <= 0 || bh <= 0) { *bx = (float)x; *by = (float)y; return; }
    *bx = (float)x * (float)bw / (float)cw;
    *by = (float)y * (float)bh / (float)ch;
}

static inline void pan_bb_a_cli(float bx, float by, int bw, int bh, int cw, int ch, int *x, int *y)
{
    if (cw <= 0 || ch <= 0 || bw <= 0 || bh <= 0) { *x = (int)floorf(bx + 0.5f); *y = (int)floorf(by + 0.5f); return; }
    *x = (int)floorf(bx * (float)cw / (float)bw + 0.5f);
    *y = (int)floorf(by * (float)ch / (float)bh + 0.5f);
}

#endif /* EDA_PANTALLA_H */
