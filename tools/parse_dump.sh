#!/bin/bash
# Parse PS Vita crash dump (.psp2dmp) files
# Usage: ./tools/parse_dump.sh <dump_file> [elf_file]
#
# Example:
#   ./tools/parse_dump.sh psp2core-xxx.psp2dmp
#   ./tools/parse_dump.sh psp2core-xxx.psp2dmp build/vita/VitaRPS5.elf

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Check arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <dump_file> [elf_file]"
    echo ""
    echo "Arguments:"
    echo "  dump_file  Path to the .psp2dmp crash dump file"
    echo "  elf_file   Path to the debug ELF file (default: build/vita/VitaRPS5.elf)"
    exit 1
fi

DUMP_FILE="$1"
ELF_FILE="${2:-build/vita/VitaRPS5.elf}"

# Function to get path relative to project root (macOS-compatible)
get_relative_path() {
    local abs_path="$1"
    local base="$PROJECT_ROOT"
    # Remove trailing slash from base
    base="${base%/}"
    # Remove base path prefix
    echo "${abs_path#$base/}"
}

# Convert to absolute paths if relative
if [[ ! "$DUMP_FILE" = /* ]]; then
    DUMP_FILE="$PROJECT_ROOT/$DUMP_FILE"
fi
if [[ ! "$ELF_FILE" = /* ]]; then
    ELF_FILE="$PROJECT_ROOT/$ELF_FILE"
fi

# Check files exist
if [ ! -f "$DUMP_FILE" ]; then
    echo "Error: Dump file not found: $DUMP_FILE"
    exit 1
fi

if [ ! -f "$ELF_FILE" ]; then
    echo "Error: ELF file not found: $ELF_FILE"
    echo "Hint: Run './tools/build.sh debug' to generate the ELF file"
    exit 1
fi

# Check if Docker image exists
if ! docker image inspect vitaki-fork-dev:latest >/dev/null 2>&1; then
    echo "Docker image not found. Building it..."
    "$SCRIPT_DIR/build.sh" shell -c "exit 0" 2>/dev/null || true
fi

# Get relative paths for Docker
DUMP_REL=$(get_relative_path "$DUMP_FILE")
ELF_REL=$(get_relative_path "$ELF_FILE")

echo "Parsing crash dump..."
echo "  Dump: $DUMP_FILE"
echo "  ELF:  $ELF_FILE"
echo ""

# Run parser in Docker container
docker run --rm \
    -v "$PROJECT_ROOT:/build/git" \
    vitaki-fork-dev:latest \
    python3 /build/git/scripts/vita/parse_core/main.py \
    "/build/git/$DUMP_REL" \
    "/build/git/$ELF_REL"
