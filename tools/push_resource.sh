#!/usr/bin/env bash
set -euo pipefail

adb_bin="${ADB:-adb}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
toolkit_dir="${IDOLY_TOOLKIT_DIR:-$project_dir/../HoshimiToolkit}"
source_dir="${1:-${IDOLY_STORY_OUTPUT_DIR:-$toolkit_dir/cache/local-files/resource}}"
remote_dir='/sdcard/Android/data/game.qualiarts.idolypride/files/idoly-localify/resource'

if [[ ! -d "$source_dir" ]]; then
    printf 'Resource directory not found: %s\n' "$source_dir" >&2
    exit 1
fi

"$adb_bin" shell mkdir -p "$remote_dir"
count=0
while IFS= read -r -d '' source_file; do
    file_name="$(basename "$source_file")"
    "$adb_bin" push "$source_file" "$remote_dir/$file_name"
    count=$((count + 1))
done < <(find "$source_dir" -type f -name '*.txt' -print0)
printf 'Copied %d TXT resources. Restart the game to reload them.\n' "$count"
