# Metalio Personal SDK 1.0 工作记录

## 当前：Font Lab 3.1（2026-09-17）

- PASS：应用 `1.0.0-fontlab3.1` 已按 app-only 方案写入 `0x80000`，5,159,888 字节；写后独立读回与候选镜像 SHA-256 一致（`96c6aac24eb44283afc90cd13fb8bdd4d22dbceb2f958d2689c815e2ab5f4bd7`）。bootloader、分区表、NVS、`font_data`、SD 与 eFuse 未写。
- PASS：主机 22 项测试、LVGL I1 主机渲染 192 种实验组合、6 页/4 方案设备导航、完整帧读回和空闲稳定性。
- PASS：设备状态 `refresh_fault=false`；页面 0–4 的主机渲染与设备帧逐像素一致。位深页为已有 MiSans fontpack 的 2bpp/4bpp 覆盖率观察，面板输出仍是 1-bit。
- NOT_PROVEN：A/B/C/D 在正常阅读距离下的最终清晰度偏好、黑底白字舒适度、反光与残影评价；这些需要用户按 [FONT_LAB_GUIDE.md](docs/FONT_LAB_GUIDE.md) 在设备上观察并记录。
- 证据目录：`artifacts/font-lab3.1/` 仅保留在本机，不随代码仓库提交；完整恢复材料仍在本机的独立备份目录。

## 当前：inkdesk-ui2，两套可切换原生字体（2026-09-17）

- PASS：应用独立读回 4,606,560 字节，SHA256 `14e7a52e6cc42e3052608d3a57cd9738c36fcd31e5724eab81eeb98cd494e17e`。仅写 `[0x80000,0x4e5000)`；安全设置、原分区表、当前 r2.3 应用及完整原厂备份已重新核验。回退应用为 r2.3，同地址。
- PASS：A 思源黑体 Medium、B 霞鹜文楷屏幕阅读版，从原始 OTF/TTF 生成 18/25/28/30 原生字号。完整来源、许可和哈希保留于 `use_font/ui-themes/`。
- PASS：主机 18 项 pytest；C++ ASan/UBSan 原生导航与 20,000 次随机操作、绘制边界检查。
- PASS：实机首页、设置、字体页、阅读示例、书签、字号、翻页、图案选择与说明页导航；A/B 反复切换；原有笔记 2 条、journal_sequence=3 保持不变。最终完整测试在同一 boot_id 内完成，未出现刷新错误。回执 `artifacts/inkdesk-ui2/runtime/ui-acceptance.json`。
- PASS：A/B 字体页和首页、阅读控制页的实际显示缓存读回。PNG 是设备帧缓冲证据，不能代替肉眼清晰度或残影检查。
- 自动测试修正了主机白名单、新应用版本字段检查及 ui.open 后等待新 pixel_revision 的时序。一次抓帧遇到页面变化，按 stale_frame 拒绝；重跑完整检查通过。服务重新打开端口期间曾观察到重启，最终稳定会话检查与此分开记录。
- 限制：仅新 UI 样机采用 A/B；原有真实 EPUB 阅读器字体未改变。主题本次开机内保留，重启 A。时间、电量和示例书目为演示数据，尚未绑定真实配置。握手旧 `version` 标签仍是 r2.3，当前固件请看 `app_version=1.0.0-inkdesk-ui2`。
- NOT_PROVEN：本轮两套字体的物理墨水屏清晰度偏好、手动触摸验收、长期稳定性。历史硬件与完整恢复演练缺口没有因本轮 UI 检查自动通过。

## 历史：r2.3，SD传书与基础EPUB（2026-09-16）

- 应用 `1.0.0-inkdesk-r2.3` 已写入并独立完整读回一致：4,144,800字节，SHA256 `f5699ba8abe7894c119d5b86da44d9967b42f1e5d7c1131bda2660f2dff1b64a`。只写 `[0x80000,0x474000)`，分区/OTA/原厂备份哈希重新核对，Secure Boot与Flash Encryption为Disabled。
- 原有纸间、笔记双存档、自检与USB服务保留；应用往返自检和心跳检查PASS。现存笔记2条、journal_sequence=3，未自动新建演示笔记。
- 新增 `book.begin/chunk/commit/abort/read`：仅SD `/books/`，16MiB上限，1024字节分块、偏移核对、SHA256、临时文件发布、拒绝覆盖、整本读回。11个设备负向检查通过，包括错序/错误token/路径越界/截断/校验失败/abort。
- 主机pytest 18项PASS。真实《系统之美》EPUB在官方解析器＋新适配器的ASan/UBSan原生测试中，26个章节、758页全部正向到EOF并回到首页通过。不是对所有EPUB的兼容承诺。
- 设备 `boot_id=bc9764cd153335e7` 的EPUB打开、连续8次前翻（跨章节）、8次回翻、40次心跳PASS；无refresh_fault、无丢包。后续真实触摸事件与阅读页变化已记录；屏幕清晰度仍由用户确认，不由程序代判。
- 《系统之美》已传到SD `/books/systems-thinking.epub`：1,203,410字节，SHA256 `1899405b52eb921c1c78772440b0820c998229f91bbc985d42f8840aef56b786`，SD全文件读回哈希一致PASS。原书只读保留，Apple Books目录重新封装为EPUB副本，没有转TXT。回执 `artifacts/inkdesk-r2.3/books/systems-thinking.receipt.json`；实机阅读回执 `epub-acceptance.json`。
- EPUB基于官方 `EpubDocument` 而不是完整CrossMux。当前正文基础排版、图片占位；复杂CSS/图片/DRM/书签/跨重启续读未实现。其余硬件、8小时稳定性、完整恢复演练没有因此自动通过。

操作见 `BOOKS.md`；r2.2可按安装清单同址回退，原厂16MiB完整备份保留。以下是历史状态，不覆盖当前版本。

## 历史：InkDesk R2＋CrossMux TXT模块＋开发底座（2026-09-16）

- 当前安装 `1.0.0-inkdesk-r2.2`，应用4,137,584字节，`0x80000`，SHA256 `6d9f774dc11ded5ca3c8e32e28161dcb418eec9c9a3411511c3e7ba58365fd88`。独立读回逐字节一致；dev.13中断已修复。
- 最新写入前身份、安全设置、分区表与OTA0选择均重新核对；只写应用区 `[0x80000,0x473000)`，完整原厂备份哈希保持一致。r2.1、dev.10、官方应用保留作回退。
- R2核心测试64、回归24、模拟适配11、脚本22通过；SDK主机15项通过。新增TXT适配的编码、段落、边界、前后翻页与EOF通过原生测试及ASan/UBSan。
- r2.1实机：新建 `sdk r2 check` 演示笔记、输入、保存、自检切换、30次心跳通过；超过10分钟同一启动编号、无丢包。r2.2重新加载该笔记，notes=1、journal_sequence=2、dirty=false。
- r2.2实机应用与TXT测试 PASS：示例书6671字节，第一页消费498字节；下一页偏移吻合，回翻恢复原偏移，返回书库/纸间成功，50次心跳启动编号不变。boot_id=`cf68bd38307c6d90`；物理字形和触控精度仍待用户观察，不能由自动命令代替。
- USB ML1、日志、自检和开发服务保留；开机纸间独立任务运行。USB服务PID为运行时信息，以本机进程检查为准；目前后台持有设备，未设置登录自启。
- CrossMux取自核验过的本地提交 `2f6bf017035b1b172cfe5cc77442e0cdb20bd65d`，5个源码文件与提交blob一致，MIT许可保留。移植的是TXT编码/段落/分页模块，**不是整个CrossMux，也没有EPUB/CSS/图片、书签和断电续读**。
- 原有录音/扬声器/传感器等诊断入口保留，不等于所有硬件在本版已重新验收。8小时压力、完整恢复演练仍未执行。
- 初次自动验收请求早于首帧，得到明确 `app_starting`；测试工具已改为对这个只读状态有界等待，失败尝试日志保留。不是设备重启。

交付及操作见 `INKDESK-R2.md`；新证据位于 `artifacts/inkdesk-r2.1/`、`artifacts/inkdesk-r2.2/`。

以下均为历史状态，不覆盖以上已恢复并安装的结果。

## 当前阻塞：dev.13 写入途中 USB 消失（2026-09-16）

dev.11/dev.12 应用安装与独立读回均通过。dev.13 修正音频模式切换等待，已编译；最后一次应用写入约35%时USB消失，**未安装成功，当前应用可能不完整**。Mac已无ESP USB串口。须重新连接，核对设备身份/安全设置后完整重写 `artifacts/dev13/xiaozhi.bin` 到 `0x80000`，再独立读回4052144字节核验。不要把构建清单的候选版本当安装回执。启动程序、分区表、OTA、NVS及字体本轮未写。

- 应用SHA256：`1dde744e361e91f5d5a8a546b087bd3b17ffa8389f8fc9b15db001e27dfffa69`；擦写范围 `[0x80000,0x45e000)`。可回退已核验dev.10或官方应用。
- 13项设备端中文测试菜单、用户确认、SD JSONL日志、同版本结果恢复、Mac增量同步已实现。
- 15项电脑端测试通过。dev.12 SD写回、Wi-Fi扫描、恢复预检通过；蓝牙扫描收到UART回执但未发现目标，配对未验收。
- dev.12录音48000样本、WAV保存、回放48000样本通过；后续三声各8000样本输出通过。首次蓝牙扫描后的输出失败保留，dev.13延长模式恢复等待，尚未复测。
- **用户设备端确认已取得**：dev.12 `boot_id=64a657669f18eb2a`，display run10、speaker run12 的 `device_touch_confirmation=PASS`；保存在 `sessions/device-selftest/64a657669f18eb2a.jsonl`。
- RTC芯片可读但time_valid=false，检查FAIL，菜单有手动校时；不能标正常。
- 待用户环境/物理操作：联网凭据、蓝牙目标配对、录音听感、充电拔插、姿态变化及当前版本输入检查。8小时稳定性和完整恢复演练未执行。
- USB服务已为刷写停止，设备重新安装后需重新启动。设备操作说明见 `DEVICE-SELFTEST.md`。

以下是历史记录，不覆盖上述安装中断状态。

## dev.10 最新进展（2026-09-16）

- 编译与安装 PASS：复用 ESP-IDF 5.5.4，应用 4,026,304 字节；仅写
  `[0x80000, 0x457000)`，独立读回逐字节一致，SHA-256
  `10e4a2965059417539ac266586aa006c5ce68510dc3b6780a35537dc8f128ad2`。
- 重新核对设备 MAC、分区表和 OTA 选择；Secure Boot / Flash Encryption
  均关闭，官方恢复应用和 16 MiB 完整备份哈希一致。
- 更正 dev.9 屏幕结论：启动文案写死为 dev.1，物理验收前发生了新会话启动；
  因此不能据此认定显示驱动失败。dev.10 改为真实版本号。
- `display.text` 现在等待完整画面渲染与同步面板刷新，报告驱动错误。
- 测试音可临时设置音量（0–90、不写 NVS），加淡入淡出；实际 PCM 写入量、
  完成错误可查询，底层写入有超时。三声各 500 ms、音量 80 的输出已收到
  每声 8000 样本的成功回执；用户当时不在旁边，听感仍待确认。
- 新增独立常驻 USB 服务 `host/service.py`，本机 Unix socket 限当前用户访问。
  跨进程命令和 `watch` 复用一个 USB 会话；断开后下一请求重新识别设备，
  不自动重放结果不明的命令。尚未配置登录自启，也未验证拔线重连。
- 电脑端测试 13 项通过。dev.10 实机批量验收记录在
  `artifacts/dev10/acceptance.jsonl`，最终状态以末尾 PASS 或错误为准。
- 自动实机验收最终 PASS：100 次场景更新、逐次版本/哈希核对、完整
  48000 字节读回；`boot_id=4add1e0d44ca8bce` 不变，测试阶段
  `heap_first=heap_last=heap_min=73599`，无丢包，USB 栈余量最低 5524 字节。
- 独立 socket 客户端 10 次查询保持同一启动编号；文件 watch 的一次
  下发已实测，场景版本 `scene-a`、revision 101；最终文字任务 3 完成。
- 设备已接收最终 `Metalio SDK dev.10 / TEST COMPLETE / USB service connected`
  画面，并由常驻服务继续持有连接。显示/听感等待用户在旁确认。

### 后续工作顺序

1. 联合观察屏幕与三声提示音（`host/service.py check`）；无需重做已通过的按键操作。
2. 对照原生 800×480 RAM 帧与 LVGL 旋转后的 480×800 坐标；当前 RAM 哈希
   不是屏幕像素读回，边界/全屏布局仍待专项验证。
3. 补可调用的联网/重连、蓝牙配对、录音文件/回放接口；扫描或 UART 初始化
   均不能替代这些能力。网络凭据与配对目标在用户提供前不自行选择。
4. 服务拔插重连、Mac 睡眠唤醒、小时级稳定性；当前约四分钟回归不能代替八小时验收。
5. 统一示例应用、公共接口和完整恢复演练后再冻结 SDK 1.0。

以下均为历史版本记录，不能覆盖以上更正。

当前设备：`1.0.0-dev.9`（2026-09-16）。已修正 USB 任务栈超额使用，并修正 `frame.read` 边界错误的 `ok` 语义；app-only 写入和独立读回一致。3 次连接、120 次状态查询、14 项自动命令检查、错误输入回归、2 分钟连续运行及 48000 字节场景读回通过。详见 [重启问题调查](docs/REBOOT_INVESTIGATION_20260916.md)。以下 dev.4/dev.3 等内容保留为历史记录，不代表当前状态。

## dev.9（当前实机版本）

- PASS：ESP-IDF v5.5.4、Metalio E-Ink4 专用配置编译；固件 4,025,552 字节，SHA-256 `d3418fb5fd21538c73cf6bb8a33d1abc65e86a3100710aff2f1be8fe7a51d1f8`。
- PASS：只写入 `0x00080000`，独立读回逐字节一致；读回 SHA-256 与候选一致。
- PASS：自动硬件探针 `protocol_failures=0`；IMU、麦克风采样、Wi-Fi 扫描、SD 私有文件 roundtrip、RTC、板级 inventory 均返回协议成功。
- PASS：错误输入回归：`unsupported_command`、`invalid_frame_range`、`invalid_scene`、`invalid_request` 都返回失败语义，之后设备继续响应。
- PASS：3 次重连、120 次状态查询、场景更新和 48000 字节 RAM 帧读回；无看门狗或会话内重启。
- PASS：2 分钟连续运行，54 轮 ping/status，活动会话 118.9 秒，单一 `boot_id`，无超时。
- PASS：`display.text` / `job.get` UI 任务队列回归通过；FAIL（本轮物理观察）：任务完成后用户仍看到旧的 dev.1 画面，dev.9 的实际屏幕刷新未通过，需继续定位显示刷新链路。记录见 [`artifacts/dev9/display-regression-20260916.jsonl`](artifacts/dev9/display-regression-20260916.jsonl) 和 [`artifacts/dev9/physical-observation-20260916.md`](artifacts/dev9/physical-observation-20260916.md)。
- PASS：用户完成两轮物理操作；日志收到 `vk_home`、`vk_next`、`vk_prev` 以及 `boot`、`volume_up`、`volume_down`、`power` 的按下/释放事件。用户确认底部三个触摸区域按下时有震动；详见 [`artifacts/dev9/physical-observation-20260916.md`](artifacts/dev9/physical-observation-20260916.md)。
- FAIL（物理观察）：`audio.tone` 的 `440 Hz、150 ms` 短音命令完成，但用户没有听到；音频命令/麦克风采样不能替代扬声器听感验收。
- NOT_PROVEN：屏幕修复后的方向和残影、AI/BOOT 短按与长按业务语义、Wi-Fi 联网与断线恢复、蓝牙配对/通信、电池充放电变化、RTC 校时、IMU 运动响应、dev.9 小时级稳定性和完整恢复演练。
- 证据：[`artifacts/dev9`](artifacts/dev9/)、[`artifacts/dev9-protocol-negative-20260916.jsonl`](artifacts/dev9-protocol-negative-20260916.jsonl)。
- 物理验收步骤：[`docs/PHYSICAL_ACCEPTANCE.md`](docs/PHYSICAL_ACCEPTANCE.md)。

## dev.4（当前候选）

- PASS：复用本机 ESP-IDF v5.5.4 和 `metalio_eink4` 专用配置构建；`project_version=1.0.0-dev.4`，固件 `artifacts/dev4/xiaozhi.bin`，4,025,760 字节，SHA-256 `63b13ff36842547d6af7767173cd74e4f580b6cb252acfc7855df08596eceae`。
- PASS：生成 [`artifacts/dev4/install-plan.json`](artifacts/dev4/install-plan.json)。完整构建段仅作为清单；计划写入段仍只有 `0x00080000` 的 `ota_0` 应用，擦除范围 `[0x00080000, 0x00457000)`。
- PASS：新增 `scene.set`、`scene.get`、`frame.read`。设备端验证严格限制为 800×480、1bpp、MSB 行优先、最多 24 个矩形；每次更新返回帧 SHA-256 和递增 revision。
- PASS：主机 `watch` 已能在一个 USB 会话中监视场景文件、发送更新并校验设备 RAM 帧；示例见 [`examples/watch-scene.json`](examples/watch-scene.json) 和 [`docs/SCENE_WATCH.md`](docs/SCENE_WATCH.md)。
- PASS：主机 Python 编译检查和离线验收测试：`8 passed`。
- NOT_PROVEN：dev.4 实机写入、独立读回、post-flash hello 和场景 RAM 读回；原因是本轮自动预检时 `/dev/cu.usbmodem11301` 已消失，未执行任何写入。
- NOT_PROVEN：场景在墨水屏上的实际刷新；主机 RAM 哈希通过后仍需要人眼观察。
- 继承的安全边界：不修改 eFuse，不擦除整片 Flash，不写 bootloader、分区表、OTA/NVS、资源或字体。官方应用恢复文件和 16 MiB 原厂备份路径不变。

## dev.3（已完成的实机诊断基线）

- PASS：dev.3 app-only 写入 `0x00080000`，独立读回逐字节一致；候选和读回 SHA-256 均为 `dd85e99d49d0c2bd06c42b0af2e711d19fc9ade2c016f063fae636a54ed9d081`。
- PASS：自动硬件探针 `protocol_failures=0`；板级清单返回 SSD1677、TCA9555、CST816S、SC7A20H、BQ27220、PCF8563、BTAudio I2S、SD 和外部蓝牙 UART。IMU WHO_AM_I/采样、音频 codec/麦克风采样、Wi-Fi 扫描、SD 私有文件 roundtrip、RTC 状态均有协议回复。
- PASS：受控物理测试命令已被设备接受，音调完成状态和震动计数均有日志；声音、手感和按键/触摸物理映射没有用户观察，因此仍为 `NOT_PROVEN`。
- 记录：[`artifacts/dev3/hardware-probe-auto.json`](artifacts/dev3/hardware-probe-auto.json)、[`artifacts/dev3/hardware-probe-physical.json`](artifacts/dev3/hardware-probe-physical.json)。

## dev.2（历史候选）

- PASS：ESP-IDF v5.5.4、ESP32-S3 专用配置编译完成，`project_version=1.0.0-dev.2`。
- PASS：主机 Python 文件通过 `py_compile`；离线验收测试 `6 passed`。
- PASS：生成独立候选清单 [`artifacts/dev2/install-plan.json`](artifacts/dev2/install-plan.json)；原计划文件保留为计划快照，实际执行见 [`artifacts/dev2/install-receipt-20260916.json`](artifacts/dev2/install-receipt-20260916.json)。
- PASS：应用候选 `artifacts/dev2/xiaozhi.bin`，4,015,536 字节；SHA-256 `b91045d1472a0b98135070f2382334de11a5e5aa763cfcd9471d5a95f5ebf98e`。
- PASS：esptool 5.4.0 `image-info` 校验通过；ESP32-S3、16MB、DIO，image checksum 有效，validation hash `922bcda2cb2a24d27a7815c4b747fa27fbc1be1c929544af200911fd919bcad9`。
- PASS：完整构建段已逐段记录地址、长度、扇区影响和 SHA-256；不代表已经全部写入。
- PASS：Mac 端新增 `hardware_acceptance.py`，默认只读查询、保存 JSONL 日志和 JSON 报告；`--physical` 才会请求一次音调和一次震动。
- PASS：固件 hello 能力清单加入 `inventory`、输入快照、电源、IMU、音频、Wi-Fi、SD、蓝牙和 RTC 查询；输入事件通过有界队列送回主机。
- PASS：协议明确区分 `protocol=PASS`、驱动返回的 `reported_result` 和需要人耳/手感确认的 `physical_effect=NOT_PROVEN`。
- PASS：dev.2 已写入 `0x00080000`，写入范围 `[0x00080000, 0x00455000)`；esptool 内部校验通过，独立读回 SHA-256 与候选一致。
- PASS：复位后设备身份为 `1.0.0-dev.2`，设备 ID `1020ba6e0be0`，hello 能力集已更新为硬件探针版本。
- PASS：默认只读硬件探针 10 个命令协议成功，`protocol_failures=0`；状态显示 `board_ready=true`、触摸读取器已看到且错误数为 0，IMU 返回 `who_am_i=0x11`，SD 已挂载，电池计量器已就绪，音频 codec API 和外部蓝牙 UART 已初始化。
- NOT_PROVEN：常驻 USB 会话。当前已观察到每次新启动主机 CLI 可能触发一次应用复位（新的 `boot_id`、`uptime_ms` 从零开始）；同一进程内的连续探针可保持会话，但系统级后台守护进程和真正热更新尚未实现。
- NOT_PROVEN：底部三个触摸区、BOOT/AI 键、上下键的物理映射；声音/麦克风、屏幕实际刷新、Wi-Fi 扫描/联网、蓝牙配对、SD 写回、电池充放电、IMU 运动量、震动和完整恢复演练仍需单独验收。
- 本轮默认探针没有调用音调或震动，也没有要求用户进行实体按键操作；报告见 [`artifacts/dev2/hardware-probe-20260916.json`](artifacts/dev2/hardware-probe-20260916.json)。

### dev.2 最小写入方案（已执行；计划文件保留）

- 候选文件：`artifacts/dev2/xiaozhi.bin`。
- 写入地址：`0x00080000`。
- 文件长度：4,015,536 字节。
- 扇区影响：`[0x00080000, 0x00455000)`，约 3.95 MiB；只覆盖 app0 应用扇区。
- 不触碰：bootloader、分区表、OTA/NVS、唤醒词/资源、字体、整片 Flash 和 eFuse。
- 写入前条件：重新读取实时设备身份、分区表和 app0 选择并与已核验基线匹配；任何不一致都停止。
- 恢复到已核验官方应用：`metalio-official-build-2026-09-15/xiaozhi.bin`，地址 `0x80000`，SHA-256 `695994ce4e109775c99d4eb4cbde583bc579bc5642e908bcec74c89b0ec9f769`；仍需单独确认。
- 原厂完整恢复材料：16 MiB 镜像 SHA-256 `5b7986151b18163f9087611edfedd563c49e37c32c12d999d0e0fa3d82a1cfaf`，写回地址 `0x0`；这是更大范围的独立恢复操作。
- 执行回执：[`artifacts/dev2/install-receipt-20260916.json`](artifacts/dev2/install-receipt-20260916.json)。

## dev.1 已完成的实机证据（历史，不转移给 dev.2）

- PASS：候选应用 4,005,776 字节，SHA-256 `a841208492cceea060f092367916ff4a4b38ac38d38195c6f36ac3d1e726f507`。
- PASS：只写入应用 `0x00080000–0x00452000`；没有写 bootloader、分区表、NVS/资源，没有整片擦除或 eFuse 操作。
- PASS：独立读回应用 4,005,776 字节，SHA-256 与候选一致。
- PASS：实机 USB 身份握手、状态、`display.text`/`job.get` 通过；设备 ID `1020ba6e0be0`，诊断版本 `1.0.0-dev.1`。
- PASS：用户确认屏幕出现 “Metalio SDK 1.0 probe”。这只证明 dev.1 文字显示路径有实机效果。
- PASS：30 分钟自动心跳/状态测试 1,800.22 秒、801 轮，`dropped=0`，活动窗口 `boot_id=0da4d639fb71db6f` 未变化；初始化后 `heap_free=119,583`、`psram_free=8,045,068` 稳定。
- 备注：日志保留 1 条启动边界 `sequence_gap`；活动窗口内没有中途重启或序号缺口。

## 设备与恢复基线

- 端口：`/dev/cu.usbmodem11301`；芯片 ESP32-S3 rev0.2；MAC `10:20:ba:6e:0b:e0`。
- 用户已在官方固件上确认：官方界面、底部三个触摸区、AI/BOOT 键和扬声器有反应。这些证据属于官方固件，不能代替个人 SDK 的验收。
- 原系统 16 MiB 备份：`5b7986151b18163f9087611edfedd563c49e37c32c12d999d0e0fa3d82a1cfaf`。
- 刷官方前个人 SDK 状态备份：`f6d00e29d9baf47b70dc81c5df2059cee94639cc3b53c99a69cf85026bb17533`。
- 自动下载入口已经验证；历史安全设置记录为 eFuse 保护关闭。本工程不提供任何 eFuse 写入或绕过命令。

## 当前接口范围（dev.9）

- `hello` / `ping` / `status`：身份、心跳、内存和任务状态。
- `display.text` / `job.get`：短文字 UI 更新；软件完成不等于物理刷新验收。
- `scene.set` / `scene.get` / `frame.read`：受限矩形场景热更新和 800×480 RAM 帧哈希/分块读回；不写 Flash。
- `inventory`：SSD1677、TCA9555、CST816S、SC7A20H、BQ27220、PCF8563、BTAudio I2S、SD、外部蓝牙 UART 及引脚清单。
- `input.snapshot`：BOOT/POWER GPIO、TCA9555 音量键和加速度计中断电平；事件队列记录按下/松开。
- `imu.probe` / `imu.read`、`power.status`、`audio.info`、`audio.sample`、`wifi.status` / `wifi.scan`、`sd.status` / `sd.roundtrip`、`bt.info`、`rtc.status`：有界读取、采样、扫描或私有测试文件 roundtrip。
- `haptic.pulse`、`audio.tone`：只接受显式请求；默认验收脚本不会调用。
- GPIO0 在官方板级代码中承担 BOOT，并由长按路径进入 PTT/AI；SDK 将它标为 `boot_or_ai_ptt_candidate`，需实机事件确认。

## 下一道门

1. 你回来后运行一轮 `--input-window` / `--physical` 实机验收：触摸/按键、屏幕、声音和震动。
2. 提供一个可连接的 Wi-Fi 测试网络，验证联网与断线恢复；准备一个蓝牙设备做配对测试。
3. 观察电池/充电状态，并在确认恢复入口后单独安排完整恢复演练。
4. 汇总每个模块的 `PASS / FAIL / NOT_PROVEN`，不把“芯片存在”与“物理效果”合并。

源：CloudZao/Metalio-E-INK4 官方提交 `6d05a6583c0feaea97b91d9aade3ce6757321dc4`；工具链复用本机 ESP-IDF v5.5.4，不重复安装。
