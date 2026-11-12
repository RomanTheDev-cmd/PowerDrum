#!/bin/bash
# After Tools/fit_drums.py: the factory kits into Source/FactoryKits.inc, their VOLUME (the PLAY groove at -1 dBTP),
# the tests, and a demo WAV of every kit (Демо WAV/, not in Git).
#   bash Tools/finish_kits.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; PROJ="$(cd "$HERE/.." && pwd)"
C=~/Library/Caches/PowerKickTools/cmake/cmake/data/bin/cmake; U=~/Library/Caches/PowerDrumBuildUniversal
T=$U/powerdrum_tests_artefacts/Release/powerdrum_tests
export PK_NUMPY_PATH=~/Library/Caches/PowerKickTools/py
cd "$HERE"
python3 build_kits.py build
$C --build $U --target powerdrum_tests -j10 > ~/Library/Caches/PowerDrumBuildUniversal.log 2>&1 || { echo "build failed"; exit 1; }
cd "$PROJ"
for pass in 1 2 3; do   # the first pass reads peaks squeezed by the emergency ceiling: repeat until they sit at -1 dBTP
  "$T" --kit-peaks > ~/Library/Caches/powerdrum_kit_peaks.txt
  cd "$HERE" && python3 build_kits.py volume ~/Library/Caches/powerdrum_kit_peaks.txt && cd "$PROJ"
  $C --build $U --target powerdrum_tests -j10 > ~/Library/Caches/PowerDrumBuildUniversal.log 2>&1 || { echo "build failed"; exit 1; }
done
"$T" --kit-peaks > ~/Library/Caches/powerdrum_kit_peaks_after.txt
echo "kit peaks after VOLUME (dBTP):"; cat ~/Library/Caches/powerdrum_kit_peaks_after.txt
mkdir -p "$PROJ/Демо WAV"
n=$("$T" --kit-peaks | wc -l | tr -d ' ')
for ((i = 0; i < n; i++)); do
  name=$(sed -n "$((i + 1))p" ~/Library/Caches/powerdrum_kit_peaks_after.txt | cut -d'|' -f1)
  "$T" --render-kit $i "Демо WAV/$(printf '%02d' $((i + 1))) $name.wav"
done
echo "demos: $(ls "$PROJ/Демо WAV" | wc -l | tr -d ' ') files"
