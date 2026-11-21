# EDA — estado (generado por laboratorio/lab.py; lo fijo está en estado.json)

- **Último commit:** 65cb769 Laboratorio: estado tras la primera regresión en la nube
- **Última DLL candidata:** 4065e9232127509882ece90279e0983e (commit 8c36400, rama local punto6/graficos) — sin aprobar
- **Referencias aprobadas a mano:** ba078f3f (Punto 5, 5/5 en TEST) · d617fb02 (Punto 6, 6/7) · ba1e861b (estable en uso)
- **Próximo objetivo:** Cuelgue al cambiar resolución en vivo después de un Stage (Reset → INVALIDCALL y RestartRender).

## Aprobado (a mano, en el juego)
- Cambio de resolución en vivo sin Stage previo (1920→800→1280→1920), d617fb02 y 4065e923
- Brillo, perfiles Bajo/Máximo, destello rojo quitado, Exit Game/botones de arriba (d617fb02)
- HUD, minimapa, REWARD/AWARD, Options, Messenger a 1920 (4065e923, server local)
- Capítulos/Misiones completo y centrado a 1920 y 1280 (4065e923)

## Pendiente
- Cuelgue: Stage con habilidades → lobby/Shop → cambio en vivo (también en d617fb02)
- Distorsión del aura de la espada (no reproducida en el server local)
- Close de Capítulos a 1920 pantalla completa (zona arriba a la derecha: overlay)
- Letras: rótulos de botones/encabezados son imágenes 800×600 agrandadas ×1,8 (sin cambio)
- Prueba visual de 4065e923 por el dueño

## Pruebas automáticas (último resultado de cada una)
- ✅ escala2d: 76 bien · 0 mal — 65cb769 · 11-20 11:52 · nube (vm)
- ✅ graficos: 62 bien · 0 mal — 65cb769 · 11-20 11:52 · nube (vm)
- ✅ teclas: código 0 — 65cb769 · 11-20 11:52 · nube (vm)
- ✅ version: prueba_version.py: 34 bien · 0 mal — 65cb769 · 11-20 11:52 · nube (vm)
- ✅ canal: → TODO BIEN — 65cb769 · 11-20 11:52 · nube (vm)
- ⏸ server: falta pycryptodome — 65cb769 · 11-20 11:52 · nube (vm)
- ✅ relay: → TODO BIEN — 65cb769 · 11-20 11:52 · nube (vm)
- ⏸ piezas: falta pycryptodome — 65cb769 · 11-20 11:52 · nube (vm)
- ⏸ dll: falta zig — 65cb769 · 11-20 11:52 · nube (vm)
- ✅ sonda: compilada (no ejecutada: falta Windows) · md5 2584294e — 65cb769 · 11-20 11:52 · nube (vm)

_Pruebas automáticas ≠ prueba visual: nada de esto aprueba una pantalla del juego._
