# Laboratorio de EDA (sin la PC del dueño)

**Lo primero que se lee:** `laboratorio/ESTADO.md` (último commit, DLL candidata, aprobado, pendiente, última corrida de cada prueba).

## Correr
| Dónde | Cómo | Qué hace |
|---|---|---|
| Nube de Claude / cualquier Linux | `python3 laboratorio/lab.py rapido escala2d graficos` | sólo esas pruebas (durante el desarrollo) |
| idem | `python3 laboratorio/lab.py regresion` | todas (antes de entregar una candidata) |
| GitHub Actions | push a una rama `lab/**` o «Run workflow» | regresión + la DLL con zig (2 veces, reproducible) + la sonda Direct3D9 en Windows |

Cada corrida deja `laboratorio/resultados/<fecha>-<commit>/` (un `.log` completo por prueba + `resumen.json`); en pantalla sólo el resumen. `lab.py comparar A B` muestra qué cambió entre dos corridas.

## Pruebas
- **Sin Windows (automáticas del todo):** escala 2D (mapeo de viewport/proyección/mouse, 76), gráficos (filtrado, antialias, perfiles, letras, CLAMP; 62, con AddressSanitizer), teclas, versión, canal, server de juego, relay UDP, piezas del exe, la DLL (compila y es reproducible), la sonda (compila).
- **Windows sin placa (experimento, `sonda/`):** Direct3D9 con WARP: la «raya» de las letras sueltas con y sin nuestra DLL, y un Reset con un render target vivo (el mecanismo del cuelgue tras un Stage).
- **Sólo con el juego y una placa (a mano):** cualquier pantalla del juego, el mouse, el Stage, la distorsión del aura, REWARD/AWARD.

Una prueba que no se puede correr queda ⏸ «omitida» con el motivo, nunca ✅.
