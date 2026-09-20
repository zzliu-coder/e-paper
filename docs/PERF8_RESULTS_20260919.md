# perf8 / perf8b 阶段实测与剩余工作

## 当前结论

这是阶段交付，总计划尚未完成。保持现有工程、中文PAPER界面、备份与SD书籍，没有修改eFuse或复用OTA槽存字体。

## 可复核证据

- `artifacts/paper-perf8-20260919/loading-acceptance-run2.jsonl`：冷开11.584804秒，加载提示首次被检测到1.443735秒，暖开1.640199秒。取消后重试的章节、偏移、字体设置不变。同机同会话单次样本，不能当P95；提示本身有刷新开销。
- `artifacts/paper-perf8-20260919/serial-reopen-5/summary.json`：5次服务关闭重开保持boot `0edc78595b2d169e`。macOS CLI服务默认采用合并DTR/RTS释放；`--legacy-lines`保留显式旧路径。ROM退出可能需要旧路径，不能把重开通过扩展为所有USB场景通过。
- `artifacts/paper-perf8-20260919/font-install.json`：M-A字体包真实安装210.657秒。perf8该路径缺少完整进度；perf8b已补，尚须新实测。
- `artifacts/paper-perf8-20260919/ui-resource-receipt.json`：75个界面字面与清单校验，旧资源在SD独立备份目录保留。

## 字体怎么比

打开一本书 → 工具 → 正文字体 → 选择“MiSans 黑白对齐”。这是同一MiSans源字形改用MONO hinting与黑白栅格对齐，**不是真四灰阶抗锯齿**。

对照“MiSans（系统默认）”，固定同一段落、字号和字重。候选支持18、22、26、34px及400、500字重；先看22px常规、26px常规。只应用“仅这本书”，暂不改系统或所有书的默认。

观察：横竖粗细是否均匀，“国、清、器”等内框是否堵住，英文a/g/e内部是否清楚，斜笔画是否更自然。再看黑底白字实验页；当前候选只进入正文路径，不能据此认定全部界面已优化。

## 独立复审

子代理发现并复验三个问题：发送任务未结束时忙状态提前结束、蓝牙失败供电引用不释放、字体commit整文件哈希缺少进度取消。已修复；未连接控制会话120秒释放，已连接保持，自检期间延期释放。

独立模拟覆盖包哈希/拷贝/commit哈希/PGF校验四阶段取消、重试、原子发布、单调进度。蓝牙覆盖提前ACK、发送结束、失败、超时、断开、租约、自检交接。模拟结果不能代替真实声音、电源和物理触摸。

## 仍未完成，后续不可遗漏

1. 完整CrossMux Section/Page图文排版仍未移植；目前为CrossMux CSS解析＋本地排版＋PNG/JPEG图片链路。
2. Metalio刷新策略同机A/B与物理残影检查；四灰阶生产开关仍关闭。
3. 冷开11秒继续优化及字体以外EPUB阶段进度；不能用进度提示冒充提速。
4. 蓝牙真实设备配对、声音及模式切换供电实测；Wi-Fi实际联网亦需可用网络。
5. 全片原系统恢复演练及恢复后物理确认。
6. 8小时稳定性。`host/paper_stability_monitor.py`只是只读采样工具，运行时长满足后仍不能替代交互、拔插和异常注入。
7. 重启后首页“继续阅读”所需的最近书籍持久化仍有缺口；每本书内部位置记录已持久化，两者需分别验收。

## 本轮固件

perf8b镜像2774512字节，SHA256 `b63171d11002898e9ede6e6f112710d2f72b8efa2080bd98b99e48eede65d1a7`。仅应用起始0x80000，扇区范围[0x80000,0x326000)，5MiB应用槽余量47%。镜像备份、写入日志和读回位于`artifacts/paper-perf8b-20260919/`。写入与运行验收分别记录。

## perf8b 已部署及 Mac 蓝牙对测

- 应用独立读回SHA与上值一致。hello报告perf8b、device `1020ba6e0be0`、boot `dd404367b1fa6a4b`。新固件可响应；ROM退出仍需显式legacy打开串口，静默路径首次hello超时。
- `font-comparison-smoke.jsonl`：M-A正式正文呈现、前后翻页，再恢复原MiSans和原位置通过。字体的主观清晰度仍未判定。
- `bt-scan-poll.jsonl`：模式二收到ACK，扫描完成，无设备结果。首次模式命令存在超时，复测成功；不可标记为稳定通过。
- Mac 模式三对测：`cloudzao`，蓝牙地址`32-96-32-aa-bb-ea`。blueutil返回paired=true、connected=true，系统新增输出UID`32-96-32-AA-BB-EA:output`。`mac-bluetooth-audio.json`记录afplay退出0及前后连接状态。
- **已发现缺陷**：Mac connected=true时，设备BluetoothState.connected仍false。外部接收连接的UART状态解析未完成；供电会话租约也依赖该状态，需一起修复后做长连接测试。当前仅通过Mac侧配对/音频输出建立及播放调用，不能把实际声音、设备状态一致性或长连接算作通过。
- 测试后Mac输出已显式恢复`BuiltInSpeakerDevice`，blueutil确认cloudzao断开且保持配对。没有修改其他设备配对。音频测试前Mac曾自动切到cloudzao，因此报告中脚本的original_output已是cloudzao；最终恢复依据扫描前的内置扬声器基线，不能仅以脚本finally作为恢复证据。
- GUI控制通道两次超时；使用已安装blueutil与SwitchAudioSource，依据本机帮助和Context7 `/toy/blueutil`文档执行。
