#!/usr/bin/env bash
set -euo pipefail

git submodule update --init

cd modules/umbf/
git submodule update --init -- modules/3rd-party/rectpack2D
git submodule update --init -- modules/amal
cd - >/dev/null

cd modules/aecl/
git submodule update --init -- modules/3rd-party/earcut/
cd - >/dev/null

echo "Submodules initialized."
