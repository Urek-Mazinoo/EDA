/* laboratorio/sonda/sonda_d3d9.c — ★ una prueba de Direct3D9 SIN el juego, para una máquina Windows sin placa (GitHub Actions:
 * WARP / «Microsoft Basic Render Driver»). Sin <windows.h> a propósito: se revisa también desde Linux con
 *     clang -target i686-pc-windows-gnu -fsyntax-only laboratorio/sonda/sonda_d3d9.c
 * y se compila con la receta del repo:
 *     zig cc -target x86-windows-gnu -O2 -o sonda.exe laboratorio/sonda/sonda_d3d9.c -lkernel32 -luser32 -lgdi32
 *
 *   sonda.exe sistema <prefijo>   el d3d9 de Windows (por ruta absoluta): la referencia, SIN nuestra dll
 *   sonda.exe proxy <prefijo>     "d3d9.dll" de la carpeta de la sonda: NUESTRA dll (con su eda_d3d9.ini al lado)
 *   sonda.exe modos <prefijo>     SÓLO LECTURA, sin ventana: adaptadores, modos, formatos y capacidades (el d3d9 de Windows)
 *
 * Qué mide (y deja en <prefijo>.json, <prefijo>-letra.bmp, <prefijo>-reset.bmp):
 *   1. si hay Direct3D9 y dispositivo (y cuál: el nombre del adaptador);
 *   2. LA LETRA SUELTA (Punto 6): una textura 16×16 con la tinta pegada arriba (filas 0..9) dibujada ×1,8 con WRAP y
 *      filtro lineal, como hace el juego a 1920×1080. «fantasma» = la tinta que aparece en las 3 filas de abajo del
 *      cuadro (la raya). Sistema: la sonda escala ella misma; proxy: dibuja en 800×600 y la escala la pone la dll;
 *   3. EL RESET CON UN RENDER TARGET VIVO (el cuelgue tras un Stage): Reset con una textura RENDERTARGET del pool
 *      DEFAULT viva → tiene que dar D3DERR_INVALIDCALL; soltándola → 0. Y otra vez a dibujar (que no se caiga).
 * El código de salida es 0 si pudo medir (aunque haya «fantasma»); 2 si no hay Direct3D9 / dispositivo en esta máquina.
 */
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef unsigned int UINT;
typedef int BOOL;
typedef long LONG;
typedef long HRESULT;
typedef void *HANDLE, *HWND, *HMODULE, *HINSTANCE, *HICON, *HCURSOR, *HBRUSH, *HMENU;
typedef unsigned int WPARAM;
typedef long LPARAM, LRESULT;
#define WINAPI __attribute__((stdcall))
typedef LRESULT (WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct { UINT style; WNDPROC proc; int a, b; HINSTANCE inst; HICON ic; HCURSOR cur; HBRUSH bg; const char *menu, *clase; } WNDCLASSA;
typedef struct { HWND h; UINT msg; WPARAM w; LPARAM l; DWORD t; LONG x, y; } MSG;
__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE, const char *);
__declspec(dllimport) UINT WINAPI GetSystemDirectoryA(char *, UINT);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char *);
__declspec(dllimport) void WINAPI Sleep(DWORD);
__declspec(dllimport) void WINAPI ExitProcess(UINT);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
__declspec(dllimport) unsigned short WINAPI RegisterClassA(const WNDCLASSA *);
__declspec(dllimport) HWND WINAPI CreateWindowExA(DWORD, const char *, const char *, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void *);
__declspec(dllimport) BOOL WINAPI ShowWindow(HWND, int);
__declspec(dllimport) LRESULT WINAPI DefWindowProcA(HWND, UINT, WPARAM, LPARAM);
__declspec(dllimport) BOOL WINAPI PeekMessageA(MSG *, HWND, UINT, UINT, UINT);
__declspec(dllimport) BOOL WINAPI TranslateMessage(const MSG *);
__declspec(dllimport) LRESULT WINAPI DispatchMessageA(const MSG *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
__declspec(dllimport) char *WINAPI GetCommandLineA(void);
#define VT(o) (*(void ***)(o))
#define NULLP ((void *)0)

/* ── salida ── */
static char g_json[8192];
static int g_jn;
static void J(const char *clave, const char *valor_json)
{
    g_jn += wsprintfA(g_json + g_jn, "%s\"%s\": %s", g_jn > 1 ? ",\n " : "\n ", clave, valor_json);
}
static void guardar(const char *ruta, const void *d, DWORD n)
{
    HANDLE f = CreateFileA(ruta, 0x40000000 /* GENERIC_WRITE */, 0, NULLP, 2 /* CREATE_ALWAYS */, 0x80, NULLP);
    if (f == (HANDLE)-1) return;
    DWORD w;
    WriteFile(f, d, n, &w, NULLP);
    CloseHandle(f);
}
/* un BMP de 24 bits con un recorte del backbuffer (filas de abajo hacia arriba) */
static BYTE g_bmp[54 + 400 * 300 * 3];
static void guardar_bmp(const char *ruta, const BYTE *px, int pitch, int x0, int y0, int w, int h)
{
    if (w > 400) w = 400;
    if (h > 300) h = 300;
    int fila = (w * 3 + 3) & ~3, tam = 54 + fila * h;
    BYTE *b = g_bmp;
    for (int i = 0; i < 54; i++) b[i] = 0;
    b[0] = 'B'; b[1] = 'M';
    *(DWORD *)(b + 2) = (DWORD)tam; *(DWORD *)(b + 10) = 54; *(DWORD *)(b + 14) = 40;
    *(LONG *)(b + 18) = w; *(LONG *)(b + 22) = h; *(WORD *)(b + 26) = 1; *(WORD *)(b + 28) = 24;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const BYTE *p = px + (y0 + y) * pitch + (x0 + x) * 4;
            BYTE *q = b + 54 + (h - 1 - y) * fila + x * 3;
            q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
        }
    guardar(ruta, b, (DWORD)tam);
}

static LRESULT WINAPI proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }
static void bombear(void)
{
    MSG m;
    while (PeekMessageA(&m, NULLP, 0, 0, 1)) { TranslateMessage(&m); DispatchMessageA(&m); }
}

/* ── Direct3D9 por la tabla (índices de d3d9.h) ── */
typedef void *(WINAPI *CREAR9)(UINT);
typedef HRESULT (WINAPI *F_IDENT)(void *, UINT, DWORD, void *);
typedef HRESULT (WINAPI *F_CREARDEV)(void *, UINT, DWORD, HWND, DWORD, DWORD *, void **);
typedef unsigned long ULONG_;
typedef ULONG_ (WINAPI *F_REL)(void *);
enum { D_RESET = 16, D_PRESENT = 17, D_GETBB = 18, D_CTEX = 23, D_CRT = 28, D_GETRTDATA = 32, D_STRETCH = 34, D_OFFSCREEN = 36, D_BEGIN = 41,
       D_END = 42, D_CLEAR = 43, D_SETTRANSFORM = 44, D_SETRS = 57, D_SETTEX = 65, D_SETSS = 69, D_DPUP = 83, D_SETFVF = 89 };
static void *g_dev;
static DWORD g_pp[14];
#define CALL(o, i, T) ((T)VT(o)[i])

static HRESULT d_clear(void) { return CALL(g_dev, D_CLEAR, HRESULT (WINAPI *)(void *, DWORD, const void *, DWORD, DWORD, float, DWORD))(g_dev, 0, NULLP, 1, 0xFF000000u, 1.0f, 0); }
static HRESULT d_rs(DWORD k, DWORD v) { return CALL(g_dev, D_SETRS, HRESULT (WINAPI *)(void *, DWORD, DWORD))(g_dev, k, v); }
static HRESULT d_ss(DWORD k, DWORD v) { return CALL(g_dev, D_SETSS, HRESULT (WINAPI *)(void *, DWORD, DWORD, DWORD))(g_dev, 0, k, v); }

/* la proyección que arma el motor: ortográfica del tamaño REAL del backbuffer (lo que escala2d reconoce como «la pantalla») */
static void orto(float w, float h)
{
    float m[16] = {0};
    m[0] = 2 / w; m[5] = -2 / h; m[10] = 1; m[12] = -1; m[13] = 1; m[15] = 1;
    CALL(g_dev, D_SETTRANSFORM, HRESULT (WINAPI *)(void *, DWORD, const float *))(g_dev, 3, m);
}
/* un cuadro texturado (FVF XYZ|DIFFUSE|TEX1 = 0x142) */
static void cuadro(float x, float y, float w, float h)
{
    struct { float x, y, z; DWORD c; float u, v; } q[4] = {
        {x, y, 0, 0xFFFFFFFFu, 0, 0}, {x + w, y, 0, 0xFFFFFFFFu, 1, 0},
        {x, y + h, 0, 0xFFFFFFFFu, 0, 1}, {x + w, y + h, 0, 0xFFFFFFFFu, 1, 1}};
    CALL(g_dev, D_SETFVF, HRESULT (WINAPI *)(void *, DWORD))(g_dev, 0x142);
    CALL(g_dev, D_DPUP, HRESULT (WINAPI *)(void *, DWORD, UINT, const void *, UINT))(g_dev, 5 /* STRIP */, 2, q, sizeof(q[0]));
}
/* la letra: 16×16 A8R8G8B8 MANAGED, tinta blanca en las filas 0..9 y columnas 2..9 (pegada arriba, como las del juego) */
static void *letra(void)
{
    void *t = NULLP;
    if (CALL(g_dev, D_CTEX, HRESULT (WINAPI *)(void *, UINT, UINT, UINT, DWORD, DWORD, DWORD, void **, void *))(
            g_dev, 16, 16, 1, 0, 21, 1, &t, NULLP) != 0 || !t) return NULLP;
    DWORD lr[2];
    if (CALL(t, 19, HRESULT (WINAPI *)(void *, UINT, DWORD *, const void *, DWORD))(t, 0, lr, NULLP, 0) == 0) {
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                ((DWORD *)((BYTE *)(unsigned long)lr[1] + y * lr[0]))[x] = (y <= 9 && x >= 2 && x <= 9) ? 0xFFFFFFFFu : 0x00000000u;
        CALL(t, 20, HRESULT (WINAPI *)(void *, UINT))(t, 0);
    }
    return t;
}
/* copia el backbuffer a la memoria; devuelve el puntero bloqueado (pitch en *pitch) y la superficie en *s (soltarla).
 * GetRenderTargetData exige el MISMO formato y una fuente SIN antialias: en ventana el backbuffer es X8R8G8B8 (no A8R8G8B8)
 * y con la dll (antialias 2×) es multimuestra → primero StretchRect a un render target simple. g_leer: dónde falló. */
static char g_leer[160];
static const BYTE *leer_bb(int *pitch, void **s)
{
    void *bb = NULLP, *rt = NULLP, *mem = NULLP, *fuente;
    DWORD d[8] = {0};                       /* D3DSURFACE_DESC: Format, Type, Usage, Pool, MultiSampleType, Quality, W, H */
    HRESULT hr;
    *s = NULLP;
    hr = CALL(g_dev, D_GETBB, HRESULT (WINAPI *)(void *, UINT, UINT, DWORD, void **))(g_dev, 0, 0, 0, &bb);
    if (hr != 0) { wsprintfA(g_leer, "GetBackBuffer 0x%08lx", (unsigned long)hr); return NULLP; }
    CALL(bb, 12, HRESULT (WINAPI *)(void *, DWORD *))(bb, d);
    fuente = bb;
    if (d[4] != 0) {                        /* multimuestra: a un render target sin antialias, del mismo formato */
        hr = CALL(g_dev, D_CRT, HRESULT (WINAPI *)(void *, UINT, UINT, DWORD, DWORD, DWORD, BOOL, void **, void *))(
                g_dev, d[6], d[7], d[0], 0, 0, 0, &rt, NULLP);
        if (hr == 0) hr = CALL(g_dev, D_STRETCH, HRESULT (WINAPI *)(void *, void *, const void *, void *, const void *, DWORD))(
                g_dev, bb, NULLP, rt, NULLP, 0);
        if (hr != 0) {
            wsprintfA(g_leer, "antialias %lu: render target / StretchRect 0x%08lx", (unsigned long)d[4], (unsigned long)hr);
            if (rt) CALL(rt, 2, F_REL)(rt);
            CALL(bb, 2, F_REL)(bb);
            return NULLP;
        }
        fuente = rt;
    }
    hr = CALL(g_dev, D_OFFSCREEN, HRESULT (WINAPI *)(void *, UINT, UINT, DWORD, DWORD, void **, void *))(
            g_dev, d[6], d[7], d[0], 2 /* SYSTEMMEM */, &mem, NULLP);
    if (hr == 0) hr = CALL(g_dev, D_GETRTDATA, HRESULT (WINAPI *)(void *, void *, void *))(g_dev, fuente, mem);
    if (rt) CALL(rt, 2, F_REL)(rt);
    CALL(bb, 2, F_REL)(bb);
    DWORD lr[2];
    if (hr == 0) hr = CALL(mem, 13, HRESULT (WINAPI *)(void *, DWORD *, const void *, DWORD))(mem, lr, NULLP, 0x10);
    if (hr != 0) {
        wsprintfA(g_leer, "formato %lu, antialias %lu: copia 0x%08lx", (unsigned long)d[0], (unsigned long)d[4], (unsigned long)hr);
        if (mem) CALL(mem, 2, F_REL)(mem);
        return NULLP;
    }
    wsprintfA(g_leer, "formato %lu, antialias %lu", (unsigned long)d[0], (unsigned long)d[4]);
    *pitch = (int)lr[0];
    *s = mem;
    return (const BYTE *)(unsigned long)lr[1];
}
static void soltar_bb(void *s) { if (s) { CALL(s, 14, HRESULT (WINAPI *)(void *))(s); CALL(s, 2, F_REL)(s); } }


/* ── «modos» (SÓLO LECTURA, sin ventana ni dispositivo): lo que Direct3D9 le ofrece a un juego en esta máquina.
 * LithTech (EDA) arma su lista de dispositivos con estos datos y en InitRender busca la resolución pedida
 * («Can't find an appropriate display mode!» / «Can't find any d3d devices to use!»), aunque juegue en ventana. */
static char g_modos[65536];
static int g_mn;
static void modos(void *d3d, const char *pre)
{
    static const DWORD fmts[] = {22 /* X8R8G8B8 */, 21 /* A8R8G8B8 */, 23 /* R5G6B5 */, 24 /* X1R5G5B5 */};
    static const char *nfmt[] = {"X8R8G8B8", "A8R8G8B8", "R5G6B5", "X1R5G5B5"};
    char txt[600], ruta[300];
    UINT na = CALL(d3d, 4, UINT (WINAPI *)(void *))(d3d);
    wsprintfA(txt, "%u", na); J("adaptadores", txt);
    for (UINT a = 0; a < na && a < 4; a++) {
        DWORD dm[4] = {0};
        HRESULT hr = CALL(d3d, 8, HRESULT (WINAPI *)(void *, UINT, DWORD *))(d3d, a, dm);
        char k[40];
        wsprintfA(k, "a%u_pantalla", a);
        wsprintfA(txt, "{\"hr\": \"0x%08lx\", \"ancho\": %lu, \"alto\": %lu, \"hz\": %lu, \"formato\": %lu}",
                  (unsigned long)hr, dm[0], dm[1], dm[2], dm[3]);
        J(k, txt);
        g_mn += wsprintfA(g_modos + g_mn, "== adaptador %u: pantalla %lux%lu %luHz formato %lu\r\n", a, dm[0], dm[1], dm[2], dm[3]);
        for (int f = 0; f < 4; f++) {
            UINT n = CALL(d3d, 6, UINT (WINAPI *)(void *, UINT, DWORD))(d3d, a, fmts[f]);
            int t800 = 0, t1280 = 0, t1920 = 0, t640 = 0;
            g_mn += wsprintfA(g_modos + g_mn, "-- %s: %u modos\r\n", nfmt[f], n);
            for (UINT i = 0; i < n; i++) {
                DWORD m[4] = {0};
                if (CALL(d3d, 7, HRESULT (WINAPI *)(void *, UINT, DWORD, UINT, DWORD *))(d3d, a, fmts[f], i, m) != 0) continue;
                if (g_mn < (int)sizeof(g_modos) - 80)
                    g_mn += wsprintfA(g_modos + g_mn, "   %lux%lu %luHz\r\n", m[0], m[1], m[2]);
                if (m[0] == 640 && m[1] == 480) t640 = 1;
                if (m[0] == 800 && m[1] == 600) t800 = 1;
                if (m[0] == 1280 && m[1] == 720) t1280 = 1;
                if (m[0] == 1920 && m[1] == 1080) t1920 = 1;
            }
            /* CheckDeviceType HAL en ventana y a pantalla completa con ese formato */
            HRESULT v = CALL(d3d, 9, HRESULT (WINAPI *)(void *, UINT, DWORD, DWORD, DWORD, BOOL))(d3d, a, 1, dm[3], fmts[f], 1);
            HRESULT c = CALL(d3d, 9, HRESULT (WINAPI *)(void *, UINT, DWORD, DWORD, DWORD, BOOL))(d3d, a, 1, fmts[f], fmts[f], 0);
            HRESULT z = CALL(d3d, 12, HRESULT (WINAPI *)(void *, UINT, DWORD, DWORD, DWORD, DWORD))(d3d, a, 1, dm[3], fmts[f], 75 /* D24S8 */);
            wsprintfA(k, "a%u_%s", a, nfmt[f]);
            wsprintfA(txt, "{\"modos\": %u, \"640x480\": %s, \"800x600\": %s, \"1280x720\": %s, \"1920x1080\": %s, "
                           "\"hal_ventana\": \"0x%08lx\", \"hal_completa\": \"0x%08lx\", \"d24s8\": \"0x%08lx\"}",
                      n, t640 ? "true" : "false", t800 ? "true" : "false", t1280 ? "true" : "false", t1920 ? "true" : "false",
                      (unsigned long)v, (unsigned long)c, (unsigned long)z);
            J(k, txt);
        }
        DWORD caps[128] = {0};                                 /* D3DCAPS9 (304 B) */
        hr = CALL(d3d, 14, HRESULT (WINAPI *)(void *, UINT, DWORD, DWORD *))(d3d, a, 1, caps);
        wsprintfA(k, "a%u_caps_hal", a);
        wsprintfA(txt, "{\"hr\": \"0x%08lx\", \"DevCaps\": \"0x%08lx\", \"textura_max\": \"%lux%lu\", \"texturas_simultaneas\": %lu, "
                       "\"streams\": %lu, \"vs\": \"0x%08lx\", \"ps\": \"0x%08lx\", \"DevCaps2\": \"0x%08lx\", \"hw_tnl\": %s}",
                  (unsigned long)hr, caps[7], caps[22], caps[23], caps[38], caps[47], caps[49], caps[51], caps[53],
                  (caps[7] & 0x10000) ? "true" : "false");
        J(k, txt);
    }
    wsprintfA(ruta, "%s-modos.txt", pre);
    guardar(ruta, g_modos, (DWORD)g_mn);
}

int main(int argc, char **argv)
{
    const char *modo = argc > 1 ? argv[1] : "sistema", *pre = argc > 2 ? argv[2] : "sonda";
    int proxy = modo[0] == 'p';
    char ruta[300], txt[600];
    g_json[0] = '{'; g_jn = 1;
    wsprintfA(txt, "\"%s\"", modo); J("modo", txt);
    if (proxy) wsprintfA(ruta, "d3d9.dll");
    else { UINT n = GetSystemDirectoryA(ruta, 260); wsprintfA(ruta + n, "\\d3d9.dll"); }
    HMODULE d9 = LoadLibraryA(ruta);
    CREAR9 crear = d9 ? (CREAR9)GetProcAddress(d9, "Direct3DCreate9") : NULLP;
    void *d3d = crear ? crear(32) : NULLP;
    J("direct3d9", d3d ? "true" : "false");
    int salida = 2;
    if (d3d) {
        BYTE id[1100] = {0};                                   /* D3DADAPTER_IDENTIFIER9: Driver[512], Description[512] … */
        if (CALL(d3d, 5, F_IDENT)(d3d, 0, 0, id) == 0) {
            int k = 0;
            txt[k++] = '"';
            for (int i = 512; i < 1024 && id[i] && k < 200; i++) if (id[i] >= 32 && id[i] != '"' && id[i] != '\\') txt[k++] = (char)id[i];
            txt[k++] = '"'; txt[k] = 0;
            J("adaptador", txt);
        }
        if (modo[0] == 'm') {                                  /* sonda.exe modos <prefijo>: sólo leer, sin ventana */
            modos(d3d, pre);
            CALL(d3d, 2, F_REL)(d3d);
            g_json[g_jn++] = '\n'; g_json[g_jn++] = '}'; g_json[g_jn++] = '\n';
            wsprintfA(ruta, "%s.json", pre);
            guardar(ruta, g_json, (DWORD)g_jn);
            ExitProcess(0);
        }
        WNDCLASSA c = {0};
        c.proc = proc; c.inst = GetModuleHandleA(NULLP); c.clase = "sonda";
        RegisterClassA(&c);
        HWND w = CreateWindowExA(0, "sonda", "sonda d3d9", 0x10CF0000 /* OVERLAPPEDWINDOW|VISIBLE */, 0, 0, 1000, 700,
                                 NULLP, NULLP, c.inst, NULLP);
        ShowWindow(w, 1);
        bombear();
        /* 1920×1080 en ventana (como el juego a pantalla completa sin bordes): escala2d ACTIVA en modo proxy */
        g_pp[0] = 1920; g_pp[1] = 1080; g_pp[2] = 0; g_pp[3] = 1; g_pp[6] = 1 /* DISCARD */; g_pp[7] = (DWORD)(unsigned long)w;
        g_pp[8] = 1; g_pp[13] = 0x80000000u /* IMMEDIATE */;
        HRESULT hr = CALL(d3d, 16, F_CREARDEV)(d3d, 0, 1 /* HAL */, w, 0x20 /* SOFTWARE_VERTEXPROCESSING */, g_pp, &g_dev);
        wsprintfA(txt, "\"0x%08lx\"", (unsigned long)hr); J("crear_dispositivo", txt);
        if (hr == 0 && g_dev) {
            salida = 0;
            /* ── 2. la letra ×1,8 con WRAP y lineal ── */
            void *t = letra();
            d_rs(137, 0); d_rs(22, 1); d_rs(27, 1); d_rs(19, 5); d_rs(20, 6);   /* sin luz, sin culling, alfa normal */
            d_ss(1, 1); d_ss(2, 1); d_ss(5, 2); d_ss(6, 2);                     /* WRAP, WRAP, LINEAR, LINEAR */
            float x = 100, y = 100, l = 16;                                       /* (en 800×600) */
            if (!proxy) { x = 240 + x * 1.8f; y = y * 1.8f; l = l * 1.8f; }      /* (sin la dll: la escala la hace la sonda) */
            for (int f = 0; f < 3; f++) {
                d_clear();
                CALL(g_dev, D_BEGIN, HRESULT (WINAPI *)(void *))(g_dev);
                orto(1920, 1080);
                CALL(g_dev, D_SETTEX, HRESULT (WINAPI *)(void *, DWORD, void *))(g_dev, 0, t);
                cuadro(x, y, l, l);
                CALL(g_dev, D_END, HRESULT (WINAPI *)(void *))(g_dev);
                if (f < 2) CALL(g_dev, D_PRESENT, HRESULT (WINAPI *)(void *, void *, void *, HWND, void *))(g_dev, NULLP, NULLP, NULLP, NULLP);
                bombear();
            }
            int pitch; void *s;
            const BYTE *px = leer_bb(&pitch, &s);
            if (px) {
                /* el cuadro en pantalla: 240+100·1,8 = 420, 180 … 28,8 px de lado; la tinta llega hasta la fila 10/16 */
                int X0 = 420, Y0 = 180, L = 29, fantasma = 0, tinta = 0;
                for (int yy = Y0 + L - 4; yy < Y0 + L + 1; yy++)
                    for (int xx = X0; xx < X0 + L; xx++) { int v = px[yy * pitch + xx * 4 + 1]; if (v > fantasma) fantasma = v; }
                for (int yy = Y0; yy < Y0 + 10; yy++)
                    for (int xx = X0; xx < X0 + L; xx++) { int v = px[yy * pitch + xx * 4 + 1]; if (v > tinta) tinta = v; }
                wsprintfA(txt, "{\"tinta\": %d, \"fantasma_abajo\": %d, \"raya\": %s}", tinta, fantasma, fantasma > 24 ? "true" : "false");
                J("letra", txt);
                wsprintfA(ruta, "%s-letra.bmp", pre);
                guardar_bmp(ruta, px, pitch, X0 - 40, Y0 - 40, 120, 120);
                soltar_bb(s);
            } else { wsprintfA(txt, "\"no se pudo leer el backbuffer: %s\"", g_leer); J("letra", txt); }
            wsprintfA(txt, "\"%s\"", g_leer); J("lectura", txt);
            /* ── 3. el Reset con un render target vivo ── */
            void *rt = NULLP;
            CALL(g_dev, D_CTEX, HRESULT (WINAPI *)(void *, UINT, UINT, UINT, DWORD, DWORD, DWORD, void **, void *))(
                g_dev, 1024, 1024, 1, 1 /* RENDERTARGET */, 21, 0 /* DEFAULT */, &rt, NULLP);
            if (t) CALL(g_dev, D_SETTEX, HRESULT (WINAPI *)(void *, DWORD, void *))(g_dev, 0, NULLP);
            g_pp[0] = 1280; g_pp[1] = 720;
            HRESULT r1 = CALL(g_dev, D_RESET, HRESULT (WINAPI *)(void *, DWORD *))(g_dev, g_pp);
            if (rt) CALL(rt, 2, F_REL)(rt);
            g_pp[0] = 1280; g_pp[1] = 720;
            HRESULT r2 = CALL(g_dev, D_RESET, HRESULT (WINAPI *)(void *, DWORD *))(g_dev, g_pp);
            int dibuja = 0;
            if (r2 == 0) {
                d_clear();
                CALL(g_dev, D_BEGIN, HRESULT (WINAPI *)(void *))(g_dev);
                orto(1280, 720);
                CALL(g_dev, D_SETTEX, HRESULT (WINAPI *)(void *, DWORD, void *))(g_dev, 0, t);
                cuadro(proxy ? 100 : 160 + 100 * 1.2f, proxy ? 100 : 120, proxy ? 16 : 19.2f, proxy ? 16 : 19.2f);
                CALL(g_dev, D_END, HRESULT (WINAPI *)(void *))(g_dev);
                dibuja = CALL(g_dev, D_PRESENT, HRESULT (WINAPI *)(void *, void *, void *, HWND, void *))(g_dev, NULLP, NULLP, NULLP, NULLP) == 0;
                const BYTE *p2 = leer_bb(&pitch, &s);
                if (p2) { wsprintfA(ruta, "%s-reset.bmp", pre); guardar_bmp(ruta, p2, pitch, 200, 80, 160, 120); soltar_bb(s); }
            }
            wsprintfA(txt, "{\"con_rt_vivo\": \"0x%08lx\", \"sin_rt\": \"0x%08lx\", \"dibuja_despues\": %s}",
                      (unsigned long)r1, (unsigned long)r2, dibuja ? "true" : "false");
            J("reset", txt);
            if (t) CALL(t, 2, F_REL)(t);
            CALL(g_dev, 2, F_REL)(g_dev);
        }
        CALL(d3d, 2, F_REL)(d3d);
    }
    g_json[g_jn++] = '\n'; g_json[g_jn++] = '}'; g_json[g_jn++] = '\n';
    wsprintfA(ruta, "%s.json", pre);
    guardar(ruta, g_json, (DWORD)g_jn);
    ExitProcess((UINT)salida);
    return salida;
}

#ifdef SIN_CRT
/* (para enlazar sin la biblioteca de C: lld-link con bibliotecas de importación hechas con llvm-dlltool; ver lab.py) */
void __main(void) {}                              /* (lo llama main() en mingw: inicia la CRT, acá nada) */
void *memset(void *d, int c, unsigned int n) { BYTE *p = (BYTE *)d; while (n--) *p++ = (BYTE)c; return d; }
void *memcpy(void *d, const void *s, unsigned int n) { BYTE *p = (BYTE *)d; const BYTE *q = (const BYTE *)s; while (n--) *p++ = *q++; return d; }
void WINAPI entrada(void)
{
    static char copia[1024], *av[8];
    char *c = GetCommandLineA();
    int ac = 0, i = 0;
    while (c[i] && i < 1023) { copia[i] = c[i]; i++; }
    copia[i] = 0;
    for (char *p = copia; *p && ac < 8;) {
        while (*p == ' ') p++;
        if (!*p) break;
        int q = *p == '"';
        if (q) p++;
        av[ac++] = p;
        while (*p && (q ? *p != '"' : *p != ' ')) p++;
        if (*p) *p++ = 0;
    }
    ExitProcess((UINT)main(ac, av));
}
#endif
