#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:-nas}"
case "$target" in
  ble|nas) ;;
  *) echo 'P1 只打包 ble/nas；Remote 脚手架不可发布或进行 OTA。' >&2; exit 2 ;;
esac
build_dir="$repo_dir/build/firmware-$target"
image="$build_dir/MimiVent.ino.bin"
manifest="$build_dir/build-manifest.json"
python3 "$repo_dir/scripts/inspect_firmware_build.py" "$image" \
  --target "$target" --partition "$build_dir/MimiVent.ino.partitions.bin" --output "$manifest"
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$manifest")"
mkdir -p "$repo_dir/build/ota"
image_hash="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["imageSha256"][:8])' "$manifest")"
output="$repo_dir/build/ota/MimiVent-v${version}-${target}-p1-${image_hash}.mvota"
key_path="${MIMIVENT_SIGNING_KEY:-$HOME/.config/mimivent/firmware-signing/private.pem}"
python3 "$repo_dir/scripts/build_ota_package.py" "$image" --key "$key_path" --output "$output"
python3 - "$manifest" "$output" <<'PY'
import hashlib,json,sys
from pathlib import Path
manifest=Path(sys.argv[1]);package=Path(sys.argv[2]);data=json.loads(manifest.read_text())
data['packageFile']=package.name
data['packageSha256']=hashlib.sha256(package.read_bytes()).hexdigest()
data['packageBytes']=package.stat().st_size
manifest.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
print('P1 开发包：',package)
print('SHA256：',data['packageSha256'])
PY
