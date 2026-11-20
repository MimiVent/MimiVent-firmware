#!/usr/bin/env python3
"""验证构建类型/正式分区并生成不含凭据的产物清单。"""
import argparse
import hashlib
import json
import re
import struct
from datetime import datetime, timezone
from pathlib import Path

EXPECTED_PARTITIONS = [
    ('nvs', 0x9000, 0x5000), ('otadata', 0xE000, 0x2000),
    ('app0', 0x10000, 0x1B0000), ('app1', 0x1C0000, 0x1B0000),
    ('statistics', 0x370000, 0x80000), ('coredump', 0x3F0000, 0x10000),
]


def inspect(image, partitions, target):
    expected = 'remote' if target == 'remote-scaffold' else target
    def marker(name):
        values = set(re.findall(name.encode() + rb'=([^\x00\r\n]+)', image))
        if len(values) != 1:
            raise ValueError('缺少或冲突的镜像元数据：' + name)
        return values.pop().decode('ascii')
    if marker('MIMIVENT_TRANSPORT_TARGET') != expected:
        raise ValueError('镜像目标与构建目标不符')
    if marker('MIMIVENT_PARTITION_PROFILE') != 'statistics-v2-ota-v1':
        raise ValueError('分区标识与正式基线不符')
    if len(image) > 0x1B0000 or not image or image[0] != 0xE9:
        raise ValueError('镜像格式或大小不合格')
    actual = []
    for offset in range(0, len(partitions), 32):
        row = partitions[offset:offset+32]
        if row[:2] != b'\xaaP':
            break
        _, _, _, start, size, label, _ = struct.unpack('<HBBII16sI', row)
        actual.append((label.rstrip(b'\0').decode(), start, size))
    if actual != EXPECTED_PARTITIONS:
        raise ValueError('实际分区表偏离冻结的正式布局')
    return {
        'version': marker('MIMIVENT_FIRMWARE_VERSION'),
        'target': expected, 'experimental': target == 'remote-scaffold',
        'phase': 'P1-development', 'imageBytes': len(image),
        'imageSha256': hashlib.sha256(image).hexdigest(),
        'partitionSha256': hashlib.sha256(partitions).hexdigest(),
        'partitionProfile': 'statistics-v2-ota-v1',
        'builtAtUtc': datetime.now(timezone.utc).isoformat(),
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--target', choices=['ble', 'nas', 'remote-scaffold'], required=True)
    parser.add_argument('--partition', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.image.read_bytes(), args.partition.read_bytes(), args.target)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print('构建目标、实际分区和镜像大小验证通过：' + str(args.output))
