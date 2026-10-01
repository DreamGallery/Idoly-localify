#!/usr/bin/env bash
set -euo pipefail

adb_bin="${ADB:-adb}"
destination="${1:-.analysis/apk}"
mkdir -p "$destination"
listing="$("$adb_bin" shell pm path game.qualiarts.idolypride)"
found_base=0
found_split=0
while IFS= read -r line; do
  remote="${line#package:}"
  remote="${remote%$'\r'}"
  case "${remote##*/}" in
    base.apk)
      "$adb_bin" pull "$remote" "$destination/base.apk"
      found_base=1
      ;;
    split_config.arm64_v8a.apk)
      "$adb_bin" pull "$remote" "$destination/split_config.arm64_v8a.apk"
      found_split=1
      ;;
  esac
done <<< "$listing"
if [[ "$found_base" != 1 || "$found_split" != 1 ]]; then
  printf '需要已安装的 arm64 原版游戏及 base/arm64 split APK。\n' >&2
  exit 1
fi
printf '已导出至 %s\n' "$destination"
