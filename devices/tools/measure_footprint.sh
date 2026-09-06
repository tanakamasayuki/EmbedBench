#!/bin/sh
# Measure what the device models cost on a small target.
#
# Written for the harness discussion (docs/HARNESS_REQUESTS.ja.md, C-8):
# "can a probe firmware carry the models?" is answerable with numbers
# rather than opinion. Run from the repository root.
#
# RAM is each model's sizeof; .text is the object compiled on its own, so
# it is an upper bound — the linker drops unused parts with --gc-sections.
set -e
D=devices/src
ARM=$(ls "$HOME"/.arduino15/packages/arduino/tools/arm-none-eabi-gcc/*/bin/arm-none-eabi-g++ 2>/dev/null | head -1)
RV=$(ls "$HOME"/.arduino15/packages/WCH/tools/riscv-none-embed-gcc/*/bin/riscv-none-embed-g++ 2>/dev/null | head -1)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

{
  echo '#include <cstdio>'
  for h in "$D"/*_model.h; do echo "#include \"$(pwd)/$h\""; done
  echo 'int main(){'
  grep -h '^class ' "$D"/*_model.h |
    sed 's/^class \([A-Za-z]*\).*/  printf("%-24s %6zu\\n", "\1", sizeof(\1));/'
  echo '  return 0; }'
} > "$OUT/sizes.cpp"
g++ -std=c++11 -O2 -Isrc -I"$D" "$OUT/sizes.cpp" "$D"/*_model.cpp -o "$OUT/sizes"

printf '%-24s %6s %9s %9s\n' model RAM 'ARM .text' 'RV .text'
"$OUT/sizes" | while read -r name ram; do
  src="$D/$(echo "$name" | sed 's/\([a-z0-9]\)\([A-Z]\)/\1_\2/g' | tr 'A-Z' 'a-z').cpp"
  [ -f "$src" ] || src=$(grep -l "class $name" "$D"/*_model.h | sed 's/\.h$/.cpp/')
  a=0; r=0
  [ -n "$ARM" ] && $ARM -std=c++11 -Os -mcpu=cortex-m0plus -mthumb -fno-exceptions \
      -fno-rtti -ffunction-sections -c -Isrc -I"$D" "$src" -o "$OUT/a.o" 2>/dev/null &&
      a=$(size "$OUT/a.o" | tail -1 | awk '{print $1}')
  [ -n "$RV" ] && $RV -std=c++11 -Os -march=rv32imac -mabi=ilp32 -fno-exceptions \
      -fno-rtti -ffunction-sections -c -Isrc -I"$D" "$src" -o "$OUT/r.o" 2>/dev/null &&
      r=$(size "$OUT/r.o" | tail -1 | awk '{print $1}')
  printf '%-24s %6s %9s %9s\n' "$name" "$ram" "$a" "$r"
done
