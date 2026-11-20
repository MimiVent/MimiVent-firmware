#pragma once
#include <cstddef>
#include <cstring>
#include <cstdint>

// P1 仅接纳相同类型的已验签镜像；固定小缓冲，支持跨 Flash 读取边界的标记。
// 读取失败或缺少类型一律拒绝，不能由文件名推断。P4 才开放跨类型确认。
template <typename Read>
bool otaImageMatchesTransport(size_t imageSize, const char* expected, Read read) {
  const size_t markerBytes = std::strlen(expected) + 1;
  if (markerBytes < 2 || markerBytes > 64) return false;
  uint8_t bytes[256 + 64]{};
  size_t retained = 0;
  for (size_t offset = 0; offset < imageSize;) {
    const size_t count = imageSize - offset < 256 ? imageSize - offset : 256;
    if (!read(offset, bytes + retained, count)) return false;
    const size_t length = retained + count;
    for (size_t i = 0; i + markerBytes <= length; ++i) {
      if (std::memcmp(bytes + i, expected, markerBytes) == 0) return true;
    }
    retained = length < markerBytes - 1 ? length : markerBytes - 1;
    std::memmove(bytes, bytes + length - retained, retained);
    offset += count;
  }
  return false;
}

// 正式 NAS v0.7.0 尚无类型标记：只允许这一份已发布镜像作为回退基线，
// 不根据版本字符串猜测其他未知包的类型。调用前已完成签名和 Flash SHA 校验。
inline bool otaIsKnownStableNasRollback(const char* currentTarget, const char* digest) {
  return std::strcmp(currentTarget, "nas") == 0 &&
      std::strcmp(digest, "0f20984acd803e3e73e95faed5b7505dde85c32f204b4da9188aaeb14fba3a90") == 0;
}
