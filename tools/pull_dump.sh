#!/usr/bin/env bash
set -euo pipefail
adb_bin="${ADB:-adb}"
serial="${2:-${ANDROID_SERIAL:-}}"
if [[ -z "$serial" ]]; then
  printf 'Specify the test device as argument 2 or ANDROID_SERIAL\n' >&2
  exit 2
fi
remote='/sdcard/Android/data/game.qualiarts.idolypride/files/idoly-localify/dump.jsonl'
destination="${1:-translations/dump.jsonl}"
"$adb_bin" -s "$serial" pull "$remote" "$destination"
printf 'Saved %s\n' "$destination"
