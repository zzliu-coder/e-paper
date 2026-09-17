# Metalio E-Ink4 刷写与下载模式工作流

## dev.10 补充

2026-09-16：安全查询后下一进程使用 `--before no-reset` 未收到串口数据；
改用每次 `--before default-reset` 均成功。不要把历史 stub 状态当作跨进程
保证。dev.10 已在分区表、OTA 和设备身份匹配后写入，读回逐字节一致。
当前常驻服务会独占 USB，刷机前用 `host/service.py call service.stop` 释放；
完成后重启服务。常驻服务使用方式见 [DEVELOPMENT_SERVICE.md](DEVELOPMENT_SERVICE.md)。
以下 dev.9 命令保留作历史参考，候选地址与文件长度应读取当前 install-plan。

这份文档记录已经在本机和这台设备上验证过的状态转换、地址和命令。目标是让下一次开发写入可以快速复用，同时保留恢复边界。

## 已验证的事实

- 芯片：ESP32-S3 QFN56 rev0.2；设备 ID `1020ba6e0be0`；当前成功枚举的 USB 端口是 `/dev/cu.usbmodem11301`。dev.9 已在该端口完成 app-only 写入和读回。
- USB 端口存在不等于芯片在下载模式。正常应用和 ROM 下载器会复用同一个端口。
- 应用模式下，主机协议可以收到 `hello`；esptool 使用 `--before no-reset` 会失败，因为它不会替设备切换到下载器。
- 本机的 `--before default-reset` 已成功通过 USB 自动复位/下载控制线（RTS/DTR 时序）进入 ROM 下载器，不需要手按 BOOT；随后 esptool 上传了临时 stub，后续读写使用 `--before no-reset`。
- 读写命令使用 `--after no-reset` 时会停在下载器；使用 `--after hard-reset` 会回到应用模式。这解释了“刚才还在下载模式，随后又不在”的现象。
- 当前主机 CLI 每次新进程打开 USB Serial/JTAG 时可能触发一次应用复位；本次已连续观察到 `boot_id` 和 `uptime_ms` 在新进程间重新开始，且复位后几百毫秒内 `board_ready` 可能仍为 `false`。一个长期持有串口的客户端在同一会话内保持 `boot_id`，因此常驻连接/热更新应复用一个会话，而不是每条命令重新启动 CLI，并等待 `board_ready=true` 再判定初始化完成。
- 当前分区表哈希为 `b2ab8fbfdadaf0bcd91955cae6e646dcc92e698df9a364c7f9481334e5fa3113`；`ota_0` 位于 `0x00080000`、大小 `0x00500000`。OTA 数据 `slot0.ota_seq=1`、slot1 为空，选择 `ota_0`。
- dev.9 已按 app-only 方案写入，独立读回与候选镜像逐字节一致；当前清单见 [`install-plan.json`](../artifacts/dev9/install-plan.json)。

## 状态转换图

```text
正常应用模式
  ├─ metalio.py command hello  -> 读取 SDK 身份/能力
  ├─ esptool --before no-reset -> 失败（除非设备已经在下载器）
  └─ esptool --before default-reset --after no-reset
                                  ↓ USB 自动复位/下载控制线
ROM 下载器 / esptool stub
  ├─ read-flash / write-flash  -> 保持下载器（--after no-reset）
  └─ read-flash ... --after hard-reset
                                  ↓ 普通复位
正常应用模式（新固件）
```

## 下一次 app-only 开发写入

将以下变量指向本机路径；不要使用 `--erase-all` 或 `--force`。esptool 5.4.0 的 Flash 选项必须放在 `write-flash` 子命令之后。

```sh
SDK_ROOT=/Users/zheliu/Documents/Codex/2026-09-15/ni-h/outputs/metalio-sdk-1.0
ESP_PY=/Users/zheliu/Documents/Codex/2026-09-15/ni-h/work/metalio-eink4/esptool-venv/bin/python
PORT=/dev/cu.usbmodem11301
CANDIDATE="$SDK_ROOT/artifacts/dev9/xiaozhi.bin"

# 1. 无论设备当前是应用还是下载器，先用 default-reset 进入下载器并读分区表
PARTITION_FILE=$(mktemp -t metalio-partition)
"$ESP_PY" -m esptool --chip esp32s3 -p "$PORT" \
  --before default-reset --after no-reset --baud 460800 \
  read-flash 0x00008000 0x00000c00 "$PARTITION_FILE"
shasum -a 256 "$PARTITION_FILE"

# 2. 分区表哈希必须是上面记录的基线；不一致就停止，不写入

# 3. 只写 app0 应用段
"$ESP_PY" -m esptool --chip esp32s3 -p "$PORT" \
  --before no-reset --after no-reset --baud 460800 \
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x00080000 "$CANDIDATE"

# 4. 独立读回、比对哈希，并在读回结束后启动新应用
READBACK=$(mktemp -t metalio-app-readback)
"$ESP_PY" -m esptool --chip esp32s3 -p "$PORT" \
  --before no-reset --after hard-reset --baud 460800 \
read-flash 0x00080000 4025552 "$READBACK"
shasum -a 256 "$CANDIDATE" "$READBACK"
cmp "$CANDIDATE" "$READBACK"
```

如果第一步的 `default-reset` 也失败，先重新枚举端口并读取设备身份；不要反复尝试 `--force`，也不要擦除整片 Flash。端口可能在复位时短暂消失，重新运行 `ls /dev/cu.usbmodem*` 即可确认。

## 常驻连接的当前边界

当前协议和客户端已经支持“一个进程内连续发送命令、按 `boot_id` 检测重启、记录序号和丢包”。dev.9 的 `host/metalio.py watch` 还可以在同一会话内监视场景 JSON、发送 `scene.set` 并读回 RAM 帧哈希；详见 [`SCENE_WATCH.md`](SCENE_WATCH.md)。它还不是系统级 `launchd` 常驻守护进程：每次重新运行 `metalio.py` 都应当视为一次新的 USB 会话，工具会重新握手，设备可能因此复位。固件逻辑变化仍需 app-only 编译/写入/读回/重新握手。

本次现象的原始记录保存在 [`sessions/20260916-001435-command.jsonl`](../sessions/20260916-001435-command.jsonl) 和 [`sessions/20260916-001946-command.jsonl`](../sessions/20260916-001946-command.jsonl)：两个独立 CLI 会话都重新拿到低 `uptime_ms` 和新的 `boot_id`。

## 主机工具的固定位置

主机脚本在 SDK 输出目录的 `host/` 下，而不是工程根目录：

```sh
cd /Users/zheliu/Documents/Codex/2026-09-15/ni-h/outputs/metalio-sdk-1.0
/usr/bin/python3 host/hardware_acceptance.py \
  --port /dev/cu.usbmodem11301 \
  --device-id 1020ba6e0be0 \
  --log artifacts/dev9/hardware-probe-YYYYMMDD.jsonl \
  --report artifacts/dev9/hardware-probe-YYYYMMDD.json
```

默认探针是只读的；音调、震动和实体按键窗口必须显式启用。协议返回 `PASS` 只证明接口/驱动通路，声音、手感、屏幕物理刷新和触摸映射仍要分别记为 `NOT_PROVEN`，直到有实机观察证据。

场景热更新使用 `scene.set` / `scene.get` / `frame.read`，只改设备运行时 RAM/UI，不改 Flash；主机示例和限制见 [`SCENE_WATCH.md`](SCENE_WATCH.md)。

## 恢复边界

- 已核验官方应用可写回 `0x00080000`，哈希为 `695994ce4e109775c99d4eb4cbde583bc579bc5642e908bcec74c89b0ec9f769`。
- 原厂 16 MiB 完整备份哈希为 `5b7986151b18163f9087611edfedd563c49e37c32c12d999d0e0fa3d82a1cfaf`，整片恢复属于更大范围的独立操作。
- 本次及后续同一设备的常规开发写入默认限于 `ota_0` app-only；bootloader、分区表、OTA/NVS、资源、字体或整片恢复不能混入 app-only 命令。
- 永不修改 eFuse，不使用绕过保护的参数。

## 本次踩坑的可复用结论

1. 先判断“应用模式还是下载器模式”，不要只看 USB 端口是否存在。
2. `no-reset` 是保持当前状态，不是进入下载器；进入下载器使用已验证的 `default-reset`。
3. `hard-reset` 会让下载器回到应用，这是读回后验证新版本的最后一步。
4. esptool 5.4.0 的 `--flash-mode/--flash-freq/--flash-size` 放错位置会直接报 `No such option`，不会写入；修正后再执行。
5. 先核对地址、长度、哈希和分区，再写；写完必须做独立读回，不能只看烧录器内部的即时校验。
6. 文档、日志和回执要同时记录“主机协议成功”“设备报告成功”“物理效果已观察”三种不同证据。

## 离线测试入口

只运行项目自己的测试目录：

```sh
/usr/bin/python3 -m pytest -q tests
```

不要从 SDK 根目录裸跑 `pytest`。仓库带有厂商 `managed_components`，其中有需要额外参数的模型脚本；根目录收集会误把它们当作测试，产生与 SDK 无关的 `argparse` 错误。本次正确入口结果为 `8 passed`。
