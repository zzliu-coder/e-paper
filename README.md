# 当前迭代：PAPER / GLYPH 3 中文开发与维护

**当前为开发快照，存在已确认的大尺寸 JPEG 黑块问题，尚未修复。** 构建、测试边界和资源排除说明见 [2026-09-20 开发快照](docs/DEVELOPMENT_SNAPSHOT_20260920.md)，请勿作为稳定版直接部署。

当前产品代码采用 PAPER 页面与阅读内核；USB 调试、SD 管理和受控更新集成到
「系统设置 → 开发与维护」。操作、字体资源分工与恢复边界见
[开发与维护](docs/PAPER_MAINTENANCE.md)。真机验收结果以 SDK_STATUS.md 最新日期为准。
下文 FontBench、FontLab 和 InkDesk 说明保留为历史版本记录，不代表当前入口。

# 历史：统一字体试验台

入口保持：首页 → 设置 → 字体实验室。活动界面已统一为一屏参数按钮和四格对照，电脑端提供敏感性矩阵。当前操作请看 [字体试验台](docs/FONT_BENCH.md)。历史记录和其他日常功能保留。

---

# Font Lab 4 增量实现

新入口沿用：首页 → 设置 → 字体实验室。扩展样本从 SD `/inkdesk/font-lab4` 加载；旧版实验室由右上角 OLD 进入。安装、生成与验收见 [Font Lab 4](docs/FONT_LAB4.md) 及 [实机操作](docs/FONT_LAB_GUIDE.md)。本次为源码实现，完整 ESP-IDF 构建和真机光学效果待本机验证；下文保留 3.1 历史说明。

---

# Metalio Personal SDK 1.0

当前设备已验证运行 `1.0.0-fontlab3.1`。它保留 InkDesk R2 阅读与开发底座，并提供 **首页 → 设置 → 字体实验室**：字号、算法、反白、中英混排、实景和位深六页，用于在真实 480×800 I1 墨水屏上比较四套中文黑体。位深页只观察现有字库的覆盖率输入，真正四灰阶仍需独立的面板驱动项目。组件/API/构建说明见 [UI_COMPONENTS.md](docs/UI_COMPONENTS.md) 和 [FONT_LAB_GUIDE.md](docs/FONT_LAB_GUIDE.md)。设备备份、刷写回执和逐帧证据留在本机 `artifacts/`，默认不进入代码仓库。

字体实验室中的 A/B/C/D 选择只影响实验页；首页和真实 EPUB 阅读器继续使用当前默认字体，等实机评分后再推广。所有共用 LVGL I1 路径的页面使用统一像素混合。操作与限制见 [UI_PROTOTYPE.md](docs/UI_PROTOTYPE.md)。

保留 r2.3 的 R2应用、CrossMux基础TXT模块、官方EPUB解析器和USB传书/开发底座。电子书默认放SD `/books/`。操作见 [INKDESK-R2.md](INKDESK-R2.md)、[BOOKS.md](BOOKS.md)。

开机进入 UI 样机。设置页可打开真实设备诊断，书库可打开真实 SD 书库；原版纸间首页点顶部标题返回样机。自检日志自动保存到 SD 并由 Mac 服务同步。不需要粘贴终端输出。常驻服务占用 USB 时不要同时运行旧直连工具；先用 `python host/service.py --socket /tmp/metalio-<uid>.sock call service.stop` 释放端口。

## 这版已经做了什么

- 复用官方 ESP-IDF v5.5.4、Metalio E-Ink4 板级初始化、SSD1677 屏幕、CST816S 触摸和现有外设驱动。
- USB Serial/JTAG 上提供带身份、`boot_id`、序号和丢包计数的 `ML1` 协议。
- Mac 端统一客户端：握手、状态、日志、文字显示、硬件探针和有限的自动化测试。
- 同一 USB 会话内的场景热更新：`scene.set`、`scene.get`、`frame.read`，主机 `watch` 会监视 JSON 文件并校验设备 RAM 帧哈希。
- 默认探针不做 Flash/SD/RTC 写入、不会擦除或刷写、不会复位控制线、不会连接 Wi-Fi/蓝牙；它可以执行有界的本地 Wi-Fi 扫描，`audio.info`/`audio.sample` 可能初始化并读取本地 codec，但不会自动发声。
- 只有显式传入 `--physical`，才会请求一次 35ms 震动和一次短扬声器音调；物理效果仍由用户观察确认。

`NOT_PROVEN` 的含义是“这条 SDK 路径还没有在当前诊断固件上取得证据”，不是说官方固件的硬件坏了。此前官方固件的屏幕、底部触摸区、AI/BOOT 键和扬声器证据仍保留在工作记录中，但不能自动转移给个人 SDK。

## 使用已有工具链构建

电脑上已经有 ESP-IDF v5.5.4，不需要重复安装。将 `IDF_PATH` 指向已有目录并执行：

```sh
export IDF_PYTHON_ENV_PATH=/Users/zheliu/.espressif/python_env/idf5.5_py3.13_env
source /Users/zheliu/Documents/Codex/2026-09-15/ni-h/work/esp-idf-v5.5.4/export.sh
idf.py -B ../../work/metalio-sdk-1.0-build build
python prepare_manifest.py ../../work/metalio-sdk-1.0-build \
  --output-dir artifacts/font-lab3.1 --candidate 1.0.0-fontlab3.1
python -m py_compile host/*.py prepare_manifest.py
python -m pytest -q tests
```

构建只生成文件，不刷机；`artifacts/` 是本机交付目录并被仓库忽略。下载模式切换、app-only 刷写和独立读回流程见 [`docs/FLASH_WORKFLOW.md`](docs/FLASH_WORKFLOW.md)。每个候选使用新的版本目录。

## Mac 端硬件探针

设备运行 `1.0.0-dev.9` 后，默认只做一轮有界的只读查询：

```sh
python host/hardware_acceptance.py \
  --port /dev/cu.usbmodem11301 \
  --device-id 1020ba6e0be0
```

它会查询：板级清单、GPIO/TCA9555 输入快照、电池/电源、IMU、音频接口、Wi-Fi 当前状态、SD 当前状态、蓝牙 UART 状态和 RTC。结果同时写入 JSONL 日志和 JSON 报告。

如需集中做一次人工输入观察，可增加 `--input-window 10`。窗口内按底部三个触摸区，或按橙色 AI/BOOT 键；不操作也可以，脚本会自动结束。

如需明确测试扬声器和马达，才增加 `--physical`：

```sh
python host/hardware_acceptance.py \
  --port /dev/cu.usbmodem11301 \
  --device-id 1020ba6e0be0 \
  --input-window 10 --physical
```

脚本不会把“命令已接受”写成“耳朵听到/手感感觉到”。报告会分别列出 `protocol=PASS`、`reported_result` 和 `physical_effect=NOT_PROVEN`。

## 主要协议命令

| 命令 | 默认是否只读 | 作用 |
| --- | --- | --- |
| `hello` / `ping` / `status` | 是 | 身份、心跳、内存和任务状态 |
| `display.text` / `job.get` | 否（改 RAM/UI） | 在诊断屏显示短文字；完成只代表 LVGL 接受 |
| `scene.set` / `scene.get` / `frame.read` | 否（改 RAM/UI；后两者只读） | 发送受限矩形场景，并校验 800×480、48,000 字节 RAM 帧 |
| `inventory` | 是 | 官方板级器件和引脚能力清单 |
| `input.snapshot` | 是 | BOOT/POWER GPIO、TCA9555 音量键和加速度计中断电平 |
| `imu.probe` / `imu.read` | 是 | SC7A20H 设备状态和加速度样本 |
| `power.status` | 是 | BQ27220 电压、电流、电量估计 |
| `audio.info` | 可能初始化 codec | 读取音频接口参数，不自动发声 |
| `audio.tone` | 否（发声） | 显式请求短音调，受 `--physical` 保护 |
| `haptic.pulse` | 否（震动） | 显式请求一次固定 35ms 马达脉冲 |
| `wifi.status` / `wifi.scan` | 是 | 当前状态和有界本地扫描；不连接网络 |
| `sd.status` / `sd.roundtrip` | `sd.roundtrip` 会写私有测试文件 | 挂载状态；写入后读回并删除 SDK 私有文件 |
| `bt.info` / `rtc.status` | 是 | 外部蓝牙 UART、RTC 状态；不配对、不改 RTC |

官方板级代码把 GPIO0 的 BOOT 长按映射为 PTT/AI 入口，因此 SDK 将它标成 `boot_or_ai_ptt_candidate`；这仍需在本机诊断固件上按键取得事件证据。底部 Home/上一页/下一页是 CST816S 触摸坐标热区，报告保留原始坐标和推测区域。

## 刷写闸门和恢复

`artifacts/dev9/install-plan.json` 明确列出完整构建段；当前设备已按最小安装只写应用段 `0x00080000`，影响扇区为 `[0x00080000, 0x00457000)`。不写 bootloader、分区表、NVS、资源、字体或整片 Flash。恢复和扩大到 bootloader、分区表或整片 Flash 时，必须切换到对应的独立流程。

在任何写入前，必须重新核对实时端口、设备 MAC、分区表和 app0 选择；不一致就停止。恢复文件仍是已经核验的官方应用和 16MiB 原厂备份，恢复同样需要单独确认。整个工程没有 eFuse 写入路径。

详细的 PASS / FAIL / NOT_PROVEN 记录见 [`SDK_STATUS.md`](SDK_STATUS.md)。
