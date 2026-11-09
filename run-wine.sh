#!/usr/bin/env bash
# Builds repro.exe if needed (x86_64-w64-mingw32-clang or -gcc) and runs the
# four cases under the `wine` on PATH, in a prefix of its own beside it.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here" || exit 1
if [ ! -f repro.exe ]; then
  cc="$(command -v x86_64-w64-mingw32-clang || command -v x86_64-w64-mingw32-gcc)"
  "${cc:?no x86_64-w64-mingw32 compiler on PATH}" -O2 -o repro.exe repro.c -luser32 || exit 1
fi
# No Mono or Gecko prompts in a new prefix: the reproducer needs neither.
export WINEPREFIX="${REPRO_PREFIX:-$here/prefix-wine}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree=;mshtml="
echo "$(wine --version)"
for args in "none avail" "defproc avail" "validate avail" "defproc plain"; do
  wine "$here/repro.exe" $args 2>/dev/null | tr -d '\r'
done
