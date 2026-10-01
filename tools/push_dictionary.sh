#!/usr/bin/env bash
set -euo pipefail
adb_bin="${ADB:-adb}"
source_file="${1:-app/src/main/assets/idoly-localify/dict.json}"
remote_dir='/sdcard/Android/data/game.qualiarts.idolypride/files/idoly-localify'
"$adb_bin" shell mkdir -p "$remote_dir"
"$adb_bin" push "$source_file" "$remote_dir/dict.json"
printf 'Dictionary copied. Restart the game to reload it.\n'
