# P1-NAS-001：配置后首次控制偶发 prepare 超时

状态：待复现，暂缓处理。用户要求最多排查 10 分钟，未定位或未复现则仅记录，不再阻塞后续工作。

## 原始证据

2026-11-17 23:56，首次恢复 NAS 配置后，Gateway 记录设备 `fbaacba62716ac76f65334706035dd8f` 的请求 `57d4a9a2-caa0-400d-bf33-e46f807096b3`：

- POST /api/timed，stateAgeMs=19057，mqttReady=true。
- 发布 prepare 后等待回执超时，HTTP 503 DEVICE_ACK_TIMEOUT。
- commandPublished=false，未进入 execute；该日志不能被解释为设备已经执行。
- 同时段 Broker 记录设备关闭连接。没有断连前设备日志，无法认定是 WiFi 波动、客户端重建或其他原因。

## 限时核查结果

2026-11-18 00:06:56 开始，至约 00:12 完成复现操作，未用满 10 分钟预算；未复现该异常。

环境：NAS 固件 0.7.1，镜像 SHA 前缀 77d0a15d；App 0.7.0+11；Gateway 0.1.0。串口打开引起一次明确的 USB_UART_CHIP_RESET，已作为人为复位记录。

测试路径：

1. 设备重启、MQTT 重连后执行首次控制。
2. 两轮通过 BLE 重新写入当前 NAS Broker 配置，收到连接成功确认后切换 MQTT 并立即临时排风。
3. 查询昨日范围统计、事件后立即执行控制。
4. 每轮恢复自动感应，结束时风机停止，App 留在 NAS MQTT。

App 日志共记录 7 次用户控制成功，整体请求耗时 562～864ms，无控制拒绝或错误；范围统计 1939ms、事件查询 1173ms。两轮重配后首次控制的 Gateway ready/applied 分别为 171/672ms、169/584ms。串口记录相应风机动作，没有观测到非人为重启或内存分配失败日志。这些短时结果不证明原始偶发问题已被修复。

附带发现：进入配置页时默认选中旧设备 bd20d4f6，配置操作后会关联当前 BLE 设备 fbaacba6。该体验问题留给 P2 引导优化，没有证据认定它就是本次超时根因。

## 处理决定

- 本轮不修改业务代码、不增加超时、不降低资源门槛。
- 按用户要求暂缓追查，此项不再阻塞 P2；之后自然复现再按新证据处理。
- 本轮未清空授权、未刷写固件、未停止 NAS 容器；重新写入相同 Broker 配置仅用于复现。
- 若再次出现，保留 requestId、配置成功与首次控制时间、同一时段 App/Gateway/Broker/串口日志。串口应提前打开并待设备启动稳定，避免调试复位混入故障时间段。

本机原始材料位于 build/p1-verification：first-control-timebox-serial.log、first-control-timebox-gateway.log、first-control-timebox-broker.log、first-control-timebox-app.jsonl、first-control-timebox-events.json。日志不作为 Release 附件。
