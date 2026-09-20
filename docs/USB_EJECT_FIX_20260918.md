# USB 存储安全弹出修复记录

## 问题

macOS 对 `ESP32-S3 SD Reader` 执行安全弹出后，SD 卷没有回到应用侧，设备仍保持 MSC 枚举，USB Serial/JTAG 不会回来。

## 根因

板级代码使用 ESP-IDF TinyUSB MSC 的 `auto_mount_off=1` 手动挂载模式。该模式下，官方 `tud_msc_start_stop_cb()` 仍会收到 SCSI `START STOP UNIT`，但内部的 `msc_storage_mount_to_app()` 会直接跳过，应用层只能等 `TINYUSB_EVENT_DETACHED`，而 macOS 的安全弹出不会保证产生这个事件。

## 当前修复

保留经过验证的手动 SD/USB 交接顺序，并在本地托管的 `managed_components/espressif__esp_tinyusb/tinyusb_msc.c` 增加一个默认空实现的窄钩子：

`tinyusb_msc_start_stop_cb_hook(lun, start, load_eject)`

收到 `lun=0, start=false, load_eject=true` 时，Metalio 板级层调用 `NotifyHostEject()`，worker 等待 250 ms 让 SCSI 响应返回，再拆除 MSC/PHY 并恢复 USB Serial/JTAG。正常挂载、拔线和应用主动关闭仍走原来的状态机。

由于 ESP-IDF 的 `managed_components/` 默认由 `.gitignore` 排除，同一改动另存为 [`docs/patches/esp-tinyusb-msceject-hook.patch`](patches/esp-tinyusb-msceject-hook.patch)，以后重新解析依赖后可重新应用。

## 证据状态（2026-09-18）

- 代码编译：PASS。最终候选固件 `work/metalio-fontbench6-eject-fix-build/xiaozhi.bin`，SHA-256 `d747eab38819da77c66a5ea26b5503dd37a085f3ec8a9021733acd98b4148ae8`。
- 最终窄钩子版本 app-only 写入：PASS；esptool 写入校验通过。
- 独立读回：PASS；读回 SHA-256 与构建文件一致，`cmp` 逐字节一致。
- 最终验收：PASS。串口握手 → `storage.usb enabled=true` → Mac 挂载 SD → `diskutil eject /dev/disk4` → 约 1 秒内 `/dev/cu.usbmodem11301` 自动回来 → 再次 `hello` 和 `status` 成功；SD 卷已卸载，`sd.status` 报告本机 `/sdcard` 已重新挂载。

## 写入边界与恢复

最终候选只写 `ota_0` app 分区：地址 `0x00080000`，逻辑范围约为 `0x00080000–0x0057313f`；不改 bootloader、分区表、NVS、SD、`ota_1`、字体分区或 eFuse。失败时可用原 app 候选或整片备份按既有恢复流程回退。
