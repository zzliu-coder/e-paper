# 场景热更新

dev.4 增加了一个受限的 800×480 黑白场景协议。它用于快速验证“电脑改文件 → 设备接受 → 设备 RAM 帧可读回”，不替代完整固件刷写。

场景文件只允许两个顶层字段：`version` 和 `rects`。每个矩形包含 `x`、`y`、`w`、`h`、`black`；最多 24 个矩形，文件大小不超过 3600 字节。设备端按 MSB-first、行优先生成 48,000 字节帧，并返回 SHA-256。

```sh
cd /Users/zheliu/Documents/Codex/2026-09-15/ni-h/outputs/metalio-sdk-1.0
/usr/bin/python3 host/metalio.py \
  --port /dev/cu.usbmodem11301 \
  --device-id 1020ba6e0be0 \
  --seconds 60 \
  --log artifacts/dev4/watch.jsonl \
  watch examples/watch-scene.json
```

保存文件后，工具会在同一 USB 会话中发送 `scene.set`，随后调用 `scene.get` 校验版本和帧哈希。需要读回完整帧时，使用 `readback --scene` 让工具在同一设备会话内先下发场景再分块读回：

```sh
/usr/bin/python3 host/metalio.py \
  --port /dev/cu.usbmodem11301 \
  --device-id 1020ba6e0be0 \
  readback artifacts/dev4/watch-scene-readback.pbm \
  --scene examples/watch-scene.json
```

输出 PBM 只是 RAM 帧证据，屏幕上的实际刷新仍需人眼确认。

设备重启或 USB 重新枚举后，工具会重新握手；场景不会假定跨重启保留，必须重新发送。`scene.set` 设有 1.5 秒最小刷新间隔，避免墨水屏被高频刷新拖住。这个工具目前是持续运行的用户进程，不是 macOS `launchd` 系统服务。
