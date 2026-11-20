# 三目标构建 P1：开发验证

> 历史记录：本文记录 P1 开发阶段，不代表当前实现或发布状态。后续 P2 已在拟发布固件 v0.7.1 中落地单服务授权；当前正式目标、兼容性和验证以仓库根目录 [README](../README.md) 与 [CHANGELOG](../CHANGELOG.md) 为准。Remote TLS 仍未实现，本版不提供 Remote 固件。

基线：固件 v0.7.0（7db6efc），本轮开发固件 0.7.1。此阶段保留 NAS 现有三槽授权、网络协议与蓝牙链路；P2 再单独实现服务授权。

## 构建

```sh
bash scripts/build_firmware.sh ble
bash scripts/build_firmware.sh nas
bash scripts/build_firmware.sh remote-scaffold
```

- BLE：完整蓝牙、统计、校时和 OTA，编译裁剪 WiFi/MQTT。
- NAS：沿用正式版完整网络配置、MQTT、动态 OTA 工作区与配对设置。
- Remote scaffold：仅为编译骨架；不提供 MQTT 控制、不提供 TLS，不可作为远程固件使用。`transportReady=false`；`remote` 发布目标直接报错。
- `off/on/read4k` 作为旧命令别名保留；`on/read4k` 映射完整 NAS。正式版 `read4k` 输出路径仍可用。
- 三版维持正式 partitions.csv（20 KiB NVS、两个 0x1B0000 app 槽、独立 statistics 和 coredump）；不改变统计及 OTA 存储。
- 每个构建输出 build-manifest.json，实际核对二进制的目标、版本、大小及全部分区地址，记录镜像与分区 SHA-256。Remote 标记 experimental。

## OTA 验证包

```sh
bash scripts/package_ota_targets.sh nas
bash scripts/package_ota_targets.sh ble
```

输出为 `build/ota/MimiVent-v0.7.1-<target>-p1-<镜像哈希前8位>.mvota`，不是正式 Release。签名密钥沿用现有仓库外路径，可用 `MIMIVENT_SIGNING_KEY` 指定。包格式及分块保持 MVOTA2/4096 字节。

P1 固件在完整镜像验签、Flash 哈希确认后检查镜像类型，仅允许同类型升级，跨类型确认留待 P4。唯一不含类型标记的例外是已发布 NAS v0.7.0 的固定镜像 SHA-256（0f20984acd803e3e73e95faed5b7505dde85c32f204b4da9188aaeb14fba3a90），仅 NAS 可用其回退；其他无类型/不同类型包拒绝。正式 v0.7.0 接收端还没有此新校验，首次进入 P1 时需人工核对选择 NAS 包。

不变更 Bootloader 或分区表，不清空现有配网/授权/统计。改变分区的 USB 全量刷写不能代替本阶段的普通 OTA 验证。

## 验证记录

- 新增编译组合原生测试：默认 BLE、显式 NAS、旧发布参数、冲突配置、Remote 误启用明文 MQTT 均已覆盖。
- 新增类型标记原生测试：跨读取边界、缺失/不同类型、读取失败、固定正式回退镜像策略。
- 固件全量本机测试存在正式 v0.7.0 自身失败；已在临时目录解包未修改的 Git HEAD 对照复现。主要包括 String 测试替身缺少 reserve、统计容量旧断言、250ms/1000ms 旧断言及去重恢复满表行为。不能将它们当作本轮新增问题，也不能宣称全量测试已通过。去重项须在 P2 修改授权/去重前单独查清。
- 三目标最终构建/产物清单及真机记录以本轮实际结果补充；编译通过不等于三目标实机验收通过。


## P1 验证中发现的独立 OTA 缺陷

在正式 v0.7.0 接收端上传 P1 NAS 包时，手动暂停并续传，末尾返回 `PACKAGE_BLOCK`。包全部 377 个记录经主机逐块解压、CRC 检查通过。检查点为包偏移 200522，最终错误停在 1056161/1064581；失败镜像未安装。

原 `finishOtaPackageBlock()` 每 4 KiB 保存镜像偏移，却仅每 16 KiB 更新包偏移；这两个偏移可能不对应同一个压缩块。差分包已在同一条件内同时更新。新增原生测试直接执行完整包的块完成函数，原实现复现偏移不一致，修复后通过，同时覆盖保存失败终止。

针对修复：完整包也仅在 16 KiB 边界保存一致的两项偏移。不更换检查点存储、包格式、签名或蓝牙调度。首次从旧接收端进入修复版本应从零完整上传、不暂停；新接收端运行后再验证暂停续传。完整 OTA 相关测试 12 项通过。此缺陷不能被泛化为此前 NVS INVALID_STATE 的根因。

## 最终构建与实机边界（2026-11-17）

| 目标 | 镜像大小 | SHA-256 | 实机状态 |
|---|---:|---|---|
| BLE | 810448 B | `409a86aaecf2b2a99493b9d63ccd040a845cdd88cf67a6fcffcbd46deed0d76e` | 已编译和打包，尚未刷入真机；不能宣称 BLE 目标矩阵通过 |
| NAS | 1543216 B | `0361aecdb81cbf401f7fd66ad4e42dca2c58abb8ebe0230efc1700914eba8664` | 从正式 v0.7.0 完整上传、验签、确认安装后，App 显示运行 v0.7.1；CAPS 返回 nas |
| Remote scaffold | 1357968 B | `a02c7f3f8172bec95903732765c309497427d53bc53bb78d0e4c871289e11eeb` | 仅编译，不生成 OTA 包，不刷入真机 |

三目标分区 SHA-256 相同：`12ea2e313fb7ad6e67b2660f6eb1956aa8345902edc2587fe9882299f447ddb1`。具体构建日志、产物清单与验证记录位于 `build/p1-verification/`、`build/p1-builds.json`，它们是本机验证材料，不属于发布附件。

新增原生测试共 7 项通过，OTA 专项共 12 项通过。全套当前 40 项有 7 failure / 3 error；未修改的正式基线 33 项也存在完全相同的 10 个失败项，新增失败 0。全套尚未通过，不作为发版依据。

App 仍为正式 v0.7.0+11，Gateway 仍为正式 v0.1.0，本轮未修改两端业务代码。App 按构建类型隐藏网络入口、三端类型联合校验及跨类型切换尚未完成；目前仅固件接收端拒绝非同类型镜像。单服务授权、Gateway 手机授权及 Remote TLS 均未实施。BLE/NAS 全部控制、统计与网络切换的真机矩阵仍需补齐，不能把安装成功当作全部功能验收通过。

### 修复后 NAS 断点恢复实测

在已运行修复版 NAS 0.7.1 的设备上再次上传同一个 `0361aecd` 镜像包：手动暂停，设备确认包检查点 `285567/1064624`；强制停止并重新启动正式 App，BLE 重新连接后仍返回同一 taskId 和检查点。从 `285567` 续传到末尾，设备返回 `VERIFIED|0.7.1|0361aecdb81cbf401f7fd66ad4e42dca2c58abb8ebe0230efc1700914eba8664`，App 弹出确认安装。续传均速约 10.2 KB/s。第二次不需要重复安装，首次完整安装已确认运行同一镜像。

本场景覆盖暂停、App 重启、BLE 断连和重新连接后的续传；未覆盖上传中设备断电，不将其标记为断电恢复通过。截图与 App 原始日志保存在本机验证目录。

## P1 后续回归（2026-11-17）

### 去重满表问题

原正式代码虽然先按 10 分钟窗口寻找可回收槽，但随后仍允许提前淘汰任何终态记录。原生测试满表断言因此失败；如果 ACK 丢失，已执行请求的去重证据可能被抹去。已删除这一兜底淘汰路径，完整保护窗口内的新请求返回容量错误；未更改文件布局或三槽授权。测试覆盖满表不写 Flash、原编号仍可查询、恢复后重建保护窗、再次重启不提前回收，以及保护期结束后写入失败的内存回滚。

### 测试基线清理

补齐主机 Arduino String 替身的 reserve 后，原本被编译错误遮挡的用例均可执行。旧断言更新为正式代码已存在的 schema 8、216 B 单日记录、11656 B 满载快照、1000ms 状态检测；不更改生产统计逻辑或采样周期。当前固件全套 **47 项通过**，本机运行日志为 `build/p1-verification/p1-regression-cleanup.log`。先前 40 项/10 失败是清理前的历史结果。

### 当前 NAS 构建

加入去重修复后，NAS 镜像变为 `77d0a15d4fd85b0476eb6c7f6f74b801d95ba7fa4e919c2ddf209aaec3491d94`（1543056 B）；对应包 `MimiVent-v0.7.1-nas-p1-77d0a15d.mvota`，包 SHA-256 `942f7fb3a10e9baf837b13957b56b9cb7c605bb0d2a5b17c53ed5071c1138911`。前面的 `0361aecd` 属于加入此修复前的验证构建；后续部署以新清单为准。BLE 和 Remote scaffold 的生产代码不受该网络去重修复影响。

### NAS 回归及离线兜底（持续至 2026-11-18）

芯片已恢复为加入去重修复的 NAS 镜像 `77d0a15d`，USB 写后哈希验证通过。WiFi 原配置仍在，但 MQTT Broker 未配置，App 起初展示旧设备绑定缓存。现有流程建立当前设备绑定并写入 NAS 配置后，设备上报状态，成功切换 MQTT。未修改 App/Gateway 业务代码。

- 临时排风：Gateway ready 168ms、applied 475ms；App 请求约 691ms，串口记录风机 ON。
- 恢复自动：Gateway ready 179ms、applied 583ms；App 请求约 771ms，串口记录风机 OFF。
- NAS 范围统计约 2881ms、3065ms，事件查询约 488ms，均成功。跨午夜后昨日统计有 7 条事件，BLE/NAS 切换保留数据。
- 临时停止 Broker 后，BLE 仍能临时排风、恢复自动并查询统计。Broker 启动后设备/Gateway 自动重连，App 成功切回 MQTT。容器已恢复 Up，未修改 NAS 数据或配置。
- 两个短时内存样本 free/largest/min：49780/40948/39348 B、45544/34804/33680 B。不代表长期无泄漏或 Remote TLS 资源达标。

未闭环异常：首次刚配置后的一次控制在 prepare 阶段超时，当时 stateAgeMs=19057、commandPublished=false，未发布 execute；Broker 同时段记录设备关闭连接。缺少断连前的串口日志，不能认定唯一根因。后续监控覆盖的两次控制成功，不代表首次超时已修复。另外一次统计超时发生在主动 USB 复位重连期间，与稳定查询成绩分开记录。

串口打开会引发 USB_UART_CHIP_RESET，不能把本轮主动调试复位计为自主崩溃，也不在上传期间打开监视器。原始材料保存在本机 `build/p1-verification/` 的 `gateway-followup.log`、`nas-control-repro-serial.log`、`phone-followup-final.jsonl` 和相关截图。

### 剩余验收边界

2026-11-18 补充（P1 阶段快照）：按用户要求对首次控制超时最多限时排查 10 分钟，本轮约 5 分钟复现操作未复现，7 次控制均成功。已单独记录为 [P1-NAS-001](issues/p1-nas-first-control-timeout.md)，当时暂缓处理。未修改业务代码，未宣称偶发问题已修复。其他未执行的验收场景仍保持原状态。

P1 阶段结项快照：核心编译、BLE/NAS 基本控制/统计/切换、BLE 版 OTA 复位续传安装、Broker 离线兜底已验证。全部控制模式、满页真机分页、错误类型包真机拒绝、Flash 写入瞬间断电、首次配置后断连根因、24 小时压力测试当时尚未全部验收；P2 与 Remote TLS 当时未开始。此段只描述 P1 阶段，不是当前状态或发布结论。当前拟发布版本及验收见仓库根目录 README/CHANGELOG。

### 纯 BLE 目标实机

通过 USB 仅写入 app0 和 OTA 选择区，未改分区表或擦除 NVS/统计。App 识别 4 个 GATT 服务（无 WiFi 管理服务），CAPS 明确返回 `transportTarget=ble`。校时请求返回 200（83ms），临时排风启动后状态为 timed_on；今日范围查询、事件查询返回 200，事件查询样本 87ms、昨日范围样本 268ms。当前记录不足一页，不能据此宣称真机多页加载已验证。

使用 `409a86aa` BLE 包，暂停于 `157899/538472`，通过 esptool read-mac 的进入下载模式/硬复位使设备真正重启；重启并重新连接 App 后仍恢复同一个 taskId、镜像 SHA 和检查点。随后从原位置续传，零重试，至 VERIFIED 约 21 秒，再收到 INSTALLING、INSTALLED，App 显示升级成功；CAPS 仍为 ble，最后任务 IDLE。该测试是已确认暂停后的设备复位，不等同于 Flash 写入瞬间物理断电。

此处补充了前述“BLE 尚未刷入真机”的进展；错误芯片/错误类型包真机拒绝、所有控制模式及全部统计分页仍不在本次通过清单内。
