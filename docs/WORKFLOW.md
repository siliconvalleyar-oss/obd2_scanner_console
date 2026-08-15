# Workflow de Desarrollo

Flujo de trabajo establecido para el proyecto VL53L0X.

## Principios

- **Cambios de codigo:** se hacen exclusivamente a nivel **local**, en `$PWD`
  (el repo local). Nada se edita directo en la maquina remota.
- **Compilacion:** se realiza **solo de forma remota**, en la Raspberry Pi.
- **Pruebas:** se ejecutan **solo de forma remota**, en la Raspberry Pi.
- **Generacion y modificacion:** todo archivo generado o modificado vive en
  `$PWD` local. No se dejan archivos generados en el remoto.


USER="admin"
LINK="proyecto"

$LINK
## Flujo paso a paso

1. **Editar local:** realizar los cambios de codigo/documentacion en `$PWD`.
2. **Commit local:** commitear con mensajes semanticos
   (`feat:`, `fix:`, `chore:`, `docs:`, etc.).
3. **Push:** pushear los commits a `origin/main`.
4. **Actualizar remoto:** en la Pi, `git pull` para traer los cambios.
5. **Compilar remoto:** build en la Pi.
6. **Probar remoto:** ejecutar las pruebas en la Pi.

## Comandos de referencia

```bash
# Edicion local (en $PWD)
nano src/$LINK/main.cpp

# Commit y push local
git add <archivos>
git commit -m "fix: ..."
git push origin main

# Bump de version y tag (cuando corresponda)
echo "1.1.9" > VERSION
git add VERSION && git commit -m "chore: bump version to 1.1.9"
git tag v1.1.9
git push origin main && git push origin v1.1.9

# Actualizar, compilar y probar en la Pi (solo remoto)
sshpass -e ssh -o StrictHostKeyChecking=no $USER@$HOSTMAME \
  "cd /home/$USER/src/$LINK && git pull --ff-only && make clean && make -j4 && make run"
```

## Version de la app

Las reglas detalladas de versionado estan en [docs/LEARNINGS.md](LEARNINGS.md)
(seccion "Git / Versionado"). Resumen:

- **Todo push debe llevar su tag.** No se pushea sin tag.
- **Tag = VERSION.** El tag lleva `v` (`v1.1.9`) y el archivo `VERSION`
  lleva el mismo numero sin `v` (`1.1.9`). Siempre deben coincidir.
- **Proximo numero:** tag actual + 1 en el ultimo segmento, respetando el
  ciclo patch 0-9 (ej: `v1.1.8` → `1.1.9`; `v1.1.9` → `1.2.0`).
- **Ciclo patch 0-9 obligatorio:** no se pasa de `v1.0.9` a `v1.1.1`; debe ir a
  `v1.1.0`. Cada minor tiene exactamente 10 patches (0 a 9).
- **Cada commit significativo debe tener su tag.** No se salta ningun numero
  de version (no se pierde la secuencia).
- **No eliminar tags publicados** y **no retroceder de version.** Si hay un
  error, se crea un nuevo tag con el siguiente numero de la secuencia.
- El versionado arranco en `1.0.0` (`v1.0.0`).

### Como hacer un bump

1. Obtener el ultimo tag publicado: `git tag --sort=-version:refname | head -1`
   (ej: `v1.1.9`).
2. Verificar que `VERSION` coincida con ese tag (sin `v`).
3. Calcular el siguiente numero segun el ciclo patch 0-9.
4. Actualizar `VERSION` con el nuevo numero.
5. Commit `chore: bump version to X.Y.Z`, tag `vX.Y.Z` y push.

```bash
echo "1.2.0" > VERSION
git add VERSION && git commit -m "chore: bump version to 1.2.0"
git tag v1.2.0
git push origin main && git push origin v1.2.0
```

### Version en la app

- El archivo `VERSION` (raiz del proyecto) es la fuente de la version.
- El Makefile la inyecta en el build con `-DAPP_VERSION`.
- Se muestra en el banner de presentacion de la app.

## Recordatorios

- No compilar ni probar en la maquina local.
- No editar archivos dentro del repo remoto de la Pi; solo `git pull` y build.
- La password de la Pi va en `$SSHPASS`; nunca mostrarla en pantalla.
