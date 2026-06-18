#!/bin/bash
# ============================================================
# Script: tag_all_commits.sh
# Descripción: Genera un tag secuencial para cada commit de la
#              rama main que no tenga ya un tag asignado.
# Uso: ./tag_all_commits.sh [rama]
#      Por defecto, rama = main
# ============================================================

set -e  # Detener el script si algún comando falla

# Configuración
BRANCH=${1:-main}          # Rama a procesar (por defecto 'main')
PREFIX="v1.0."             # Prefijo del tag (ej. v1.0.1, v1.0.2...)
DRY_RUN=false              # Cambiar a true para simular sin crear tags

# Colores para output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # Sin color

echo -e "${YELLOW}🚀 Iniciando generación de tags para la rama '$BRANCH'...${NC}"

# Verificar que estamos en un repositorio git
if ! git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    echo -e "${RED}❌ Error: No estás dentro de un repositorio Git.${NC}"
    exit 1
fi

# Asegurarse de tener la última información remota
echo "📡 Actualizando referencias remotas..."
git fetch origin $BRANCH

# Obtener la lista de commits de la rama (orden cronológico ascendente: antiguo -> nuevo)
# Formato: HASH
commits=$(git rev-list --reverse $BRANCH)

# Verificar si hay commits
if [ -z "$commits" ]; then
    echo -e "${RED}⚠️ No se encontraron commits en la rama '$BRANCH'.${NC}"
    exit 0
fi

# Contador para versión
counter=1

# Lista para almacenar tags a crear (útil para dry-run)
tags_to_create=()

echo "🔍 Procesando commits..."

# Para cada commit
for commit_hash in $commits; do
    # Verificar si el commit ya tiene algún tag
    existing_tags=$(git tag --points-at $commit_hash)
    if [ -n "$existing_tags" ]; then
        echo -e "${YELLOW}  Commit $commit_hash ya tiene tag(s): $existing_tags - saltando${NC}"
        continue
    fi

    # Generar nombre de tag
    tag_name="${PREFIX}${counter}"
    tag_message="Auto-tag para commit $commit_hash"

    if [ "$DRY_RUN" = true ]; then
        echo -e "${GREEN}  [DRY RUN] Crearía tag $tag_name -> commit $commit_hash${NC}"
        tags_to_create+=("$tag_name|$commit_hash")
    else
        echo -e "${GREEN}  ✓ Creando tag $tag_name para commit $commit_hash${NC}"
        git tag -a "$tag_name" "$commit_hash" -m "$tag_message"
    fi

    ((counter++))
done

# Resumen
if [ "$DRY_RUN" = false ]; then
    echo -e "\n📦 Total de tags creados: $((counter-1))"
    echo "📤 Subiendo tags al remoto 'origin'..."
    git push origin --tags
    echo -e "${GREEN}✅ Proceso completado. Todos los tags han sido subidos.${NC}"
else
    echo -e "\n${YELLOW}🔎 Simulación completada. Se habrían creado los siguientes tags:${NC}"
    for entry in "${tags_to_create[@]}"; do
        tag=$(echo "$entry" | cut -d'|' -f1)
        hash=$(echo "$entry" | cut -d'|' -f2)
        echo "  $tag -> $hash"
    done
    echo -e "${YELLOW}Para ejecutar realmente, cambia DRY_RUN=false en el script.${NC}"
fi

git log origin/main

