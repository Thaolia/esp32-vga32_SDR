#!/usr/bin/env bash
# Applique patches/fabgl/*.patch sur le submodule FabGL ; ignore ceux déjà appliqués.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
shopt -s nullglob
for p in "$root"/patches/fabgl/*.patch; do
  if git -C "$root/third_party/FabGL" apply --reverse --check "$p" 2>/dev/null; then
    echo "déjà appliqué : $(basename "$p")"
  else
    git -C "$root/third_party/FabGL" apply "$p" && echo "appliqué : $(basename "$p")"
  fi
done
