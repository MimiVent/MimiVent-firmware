#!/usr/bin/env bash
set -euo pipefail

target="${1:-ble}"
# 保留旧发布命令的输出目录，便于已有打包流程继续运行。
case "$target" in
  off) target=ble; output_name=wifi-off ;;
  on) target=nas; output_name=wifi-on ;;
  read4k) target=nas; output_name=wifi-read4k ;;
  ble|nas|remote-scaffold) output_name="$target" ;;
  remote)
    echo "Remote 尚未实现 TLS；当前仅允许 remote-scaffold 开发构建，不可部署为远程固件。" >&2
    exit 2 ;;
  *)
    echo "用法：$0 [ble|nas|remote-scaffold]（兼容 off/on/read4k）" >&2
    exit 2 ;;
esac

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
temp_root="$(mktemp -d "${TMPDIR:-/tmp}/mimivent-build.XXXXXX")"
sketch_dir="$temp_root/MimiVent"
build_dir="$repo_dir/build/firmware-$output_name"
mkdir -p "$sketch_dir" "$repo_dir/build"
trap 'rm -rf "$temp_root"' EXIT

rsync -a \
  --exclude='.git' \
  --exclude='.codegraph' \
  --exclude='.DS_Store' \
  --exclude='build' \
  "$repo_dir/" "$sketch_dir/"

# 三目标沿用正式版 C3/分区/SDK。USB CDC 提供诊断日志，不改变 BLE 通信。
fqbn="esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc"
flags=""
case "$target" in
  ble) flags+=" -DMIMIVENT_TRANSPORT_TARGET=0" ;;
  nas) flags+=" -DMIMIVENT_TRANSPORT_TARGET=1 -DMIMIVENT_OTA_DYNAMIC_WORKSPACE=1 -DMIMIVENT_BLE_PAIRING_PROBE=1" ;;
  remote-scaffold) flags+=" -DMIMIVENT_TRANSPORT_TARGET=2 -DMIMIVENT_OTA_DYNAMIC_WORKSPACE=1 -DMIMIVENT_REMOTE_SCAFFOLD=1" ;;
esac
compile_args=(
  --fqbn "$fqbn"
  --build-property upload.maximum_size=1769472
  --build-path "$build_dir"
  --build-property "compiler.cpp.extra_flags=$flags"
)

arduino-cli compile "${compile_args[@]}" "$sketch_dir"
image="$build_dir/MimiVent.ino.bin"
if [[ ! -f "$image" ]]; then
  echo "没有找到固件镜像：$image" >&2
  exit 1
fi
echo "target=$target image_bytes=$(wc -c < "$image" | tr -d ' ')"
shasum -a 256 "$image"

# 构建后核对实际镜像和分区，不以宏存在代替产物验证。
python3 "$repo_dir/scripts/inspect_firmware_build.py" \
  "$image" --target "$target" --partition "$build_dir/MimiVent.ino.partitions.bin" \
  --output "$build_dir/build-manifest.json"
