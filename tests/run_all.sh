#!/usr/bin/env bash
# Runs every hardware-free test layer.
#   tests/run_all.sh          host C++ tests + config/codegen tests (~2 min)
#   tests/run_all.sh --full   ... plus real ESP-IDF builds of all radios/drivers
set -euo pipefail

cd "$(dirname "$0")/.."
PYTHON="${ESPHOME_PYTHON:-$HOME/esphome-venv/bin/python}"

echo "== host C++ tests (make -C tests) =="
make -C tests -j"$(nproc)"

echo
echo "== config / codegen tests (pytest tests/codegen) =="
"$PYTHON" -m pytest tests/codegen -q "$@"
