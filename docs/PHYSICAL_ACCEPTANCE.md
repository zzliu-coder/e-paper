# dev.9 实机验收

电脑端自动验证已经完成。回来能看到设备时，只需要做下面这一轮物理检查。

## 一次引导测试

在 SDK 输出目录执行：

```sh
cd /Users/zheliu/Documents/Codex/2026-09-15/ni-h/outputs/metalio-sdk-1.0
/usr/bin/python3 host/hardware_acceptance.py \
  --port /dev/cu.usbmodem11301 \
  --device-id 1020ba6e0be0 \
  --input-window 30 \
  --physical \
  --storage-write \
  --log artifacts/dev9/physical-acceptance-YYYYMMDD.jsonl \
  --report artifacts/dev9/physical-acceptance-YYYYMMDD.json
```

终端出现 `Input window` 后，依次完成：

1. 轻触屏幕下沿左、中、右三个区域各一次。
2. 按侧面的上键、下键各一次；按橙色 AI/BOOT 键一次。
3. 观察日志中的 `input.touch` / `input.key` 名称和原始坐标。
4. 输入窗口结束后，设备会请求一次短震动和一次短音调。记录是否真的感觉到震动、听到声音。

这里的 `protocol=PASS` 只表示设备接受并完成了命令；屏幕、声音、震动和按键位置必须由人观察后再标记 `PASS`。

## 还需单独观察的项目

- 屏幕：当前设备已执行 `display.text` 测试任务；确认文字确实显示、方向正确、刷新后没有明显残影。
- IMU：自动读取和芯片身份已经通过；轻晃设备时再运行 `imu.read`，确认数值随姿态变化。
- Wi‑Fi：当前 SDK 已能读取状态和扫描网络，但没有带凭据的连接命令；联网/断线恢复需要使用设备现有联网界面，或后续增加受控 `wifi.connect` 接口。
- 蓝牙：当前 SDK 已确认外置蓝牙 UART 初始化；配对和实际通信需要一台蓝牙设备，现有诊断接口还不执行配对。
- 电池：插拔 USB 电源时观察 `power.status` 的电压、电流和电量变化。

SD 私有文件写入、读回、删除已经由自动探针通过；无需为了这项再手动改卡上的文件。

完整恢复演练另行安排。它会写入整片 16 MiB 备份，影响范围远大于日常 app-only 更新；在执行前应单独确认恢复文件和写入清单。
