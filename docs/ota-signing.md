# MimiVent 固件签名

无线 OTA 只接受签名的 `.mvota` v2 完整包或 `.mvdelta` v2 差分包。原始 `.bin` 是 ESP 镜像，只能作为打包输入，App 不接受它。

签名算法为 RSA-3072、RSA-PSS、SHA-256，固定 32 字节盐。签名覆盖完整目标镜像；设备在校验镜像及签名后，才允许切换启动分区。App 校验包结构并传输；设备内置公钥是最终信任边界。

## 固定格式约定

当前完整包和差分包的分块大小都是 **4096 字节（4 KiB）**。App、打包器和设备接收端必须一致；只改其中一端会使设备在传输开始后返回 `ERROR:PACKAGE_BLOCK` 或对应的块格式错误。

| 包格式 | App 解析器 | 打包器 | 固件接收端 | 固定分块 |
| --- | --- | --- | --- | ---: |
| `.mvota` v2 完整包 | `MimiVent-app/lib/features/firmware_update/ota_package.dart` 的 `OtaPackage.blockSize` | `scripts/build_ota_package.py` 的 `BLOCK_SIZE` | `OtaManager.cpp` 的 `kOtaPackageBlockBytes` | 4096 B |
| `.mvdelta` v2 差分包 | `MimiVent-app/lib/features/firmware_update/ota_delta_package.dart` 的 `OtaDeltaPackage.blockSize` | `scripts/build_ota_delta.py` 的 `BLOCK_SIZE` | `OtaManager.cpp` 的 `kOtaDeltaBlockBytes` | 4096 B |

修改任一分块常量时，必须同步检查同一行的另外两个实现，以及对应的解析/打包测试：完整包是 `MimiVent-app/test/ota_package_test.dart` 和 `tests/test_ota_package_builder.py`；差分包是 `MimiVent-app/test/ota_delta_package_test.dart` 和 `tests/test_ota_delta_builder.py`。`.mvota` v2 的分块大小写在 512 字节固定头的偏移 20（小端 uint32）；`.mvdelta` v2 写在 576 字节固定头的偏移 156（小端 uint32）。打包完成后应核对头字段为 4096，并确认包的签名针对同一份 `.bin` 镜像。

## 完整固件

正式发布时分别构建 BLE 与 NAS 固件。Remote TLS 尚未实现，不打包 Remote scaffold：

```sh
bash scripts/build_firmware.sh ble
bash scripts/build_firmware.sh nas
```

这会分别生成 `build/firmware-ble/MimiVent.ino.bin` 和 `build/firmware-nas/MimiVent.ino.bin`。两者共用正式分区布局，但控制传输目标不同；不能互相替代。旧的 `off`、`on`、`read4k` 命令仅为兼容开发流程，不应用作多目标正式发布命令。

再把 `.bin` 打成 App 可选的签名 `.mvota`：

```sh
mkdir -p build/ota/release-v0.7.1
python3 scripts/build_ota_package.py \
  build/firmware-ble/MimiVent.ino.bin \
  --key ~/.config/mimivent/firmware-signing/private.pem \
  --output build/ota/release-v0.7.1/MimiVent-Firmware-v0.7.1-ble.mvota
python3 scripts/build_ota_package.py \
  build/firmware-nas/MimiVent.ino.bin \
  --key ~/.config/mimivent/firmware-signing/private.pem \
  --output build/ota/release-v0.7.1/MimiVent-Firmware-v0.7.1-nas.mvota
```

App 的“固件升级”页面只接受与设备传输类型匹配的签名 `.mvota`。发布前检查镜像目标标记、实际分区、固件版本、包头分块值、镜像 SHA-256 和设备内置公钥验签；发布时同时记录 `.bin` 与 `.mvota` 的 SHA-256。不要把 `.bin` 改扩展名冒充 `.mvota`。升级 App 与 Gateway 的版本组合见对应仓库的 README。

## 差分固件

提供当前设备基线 `.bin` 和目标 `.bin`：

```sh
python3 scripts/build_ota_delta.py base.bin target.bin \
  --key ~/.config/mimivent/firmware-signing/private.pem
```

输出 `.mvdelta`。目标与基线必须是分区配置相同、设备当前运行的 `.bin` 镜像。如果希望 App 在设备不支持差分包时回退，需要把目标版本的签名 `.mvota` 放在同目录并使用相同文件名。

## 密钥管理

私钥保存在仓库外的 `~/.config/mimivent/firmware-signing/private.pem`，文件权限为 `600`。它不能提交到 Git、发送到设备或放入 App。保留至少一份离线加密备份；私钥丢失后，已烧录的公钥无法验证新密钥签发的固件，必须通过 USB 刷入更新公钥的固件。

设备公钥位于 `OtaManager.cpp`。轮换密钥时，先发布包含新公钥的固件，并以旧密钥签名完成升级；设备确认运行新固件后，再使用新密钥签署后续固件。当前尚未启用 ESP-IDF 硬件 Secure Boot，因此签名防止未经授权的无线固件安装，不防止可接触 Flash 的攻击者替换整个固件和公钥。
