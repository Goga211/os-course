#!/usr/bin/env bash
set -euo pipefail

# ===========================
# Usage
# ===========================
if [ $# -lt 1 ]; then
    echo "Usage: $0 <instances>"
    exit 1
fi

INSTANCES="$1"

# ===========================
# Константы (одинаковые для всех тестов)
# ===========================
FILE="test.dat"
BS=4096
BC=50000
REPEAT=10

SCRIPT="./scripts/run_io_multi.sh"

# ===========================
# Функция запуска одного теста
# ===========================
run_case() {
    local MODE="$1"
    local DIRECT="$2"

    echo ""
    echo "========================================"
    echo "   CASE: rw=$MODE  type=sequence  direct=$DIRECT"
    echo "========================================"

    $SCRIPT "$INSTANCES" "$FILE" "$BS" "$BC" "$REPEAT" "$MODE" "sequence" "$DIRECT"
}

# ===========================
# 4 сценария
# ===========================

run_case "read"  "off"
run_case "read"  "on"
run_case "write" "off"
run_case "write" "on"

echo ""
echo "========================================"
echo "    ALL 4 IO TESTS COMPLETED"
echo "========================================"
