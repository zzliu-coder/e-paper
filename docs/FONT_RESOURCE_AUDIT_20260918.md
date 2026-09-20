# FontBench 字体资源审计（2026-09-18）

## 结论

`NOT LOADED` 的直接原因是旧 SD 包的 `manifest.json` 只有 Noto 的 400/700 样本，其他字体只是目录入口，没有对应 `.t5` 样本。

本轮已重新扫描本机与官方来源，并生成新的 SD 资源包：

- 字体家族：8 个（思源、MiSans、HarmonyOS Sans SC、霞鹜新晰黑屏幕版、文泉驿微米黑、苹方 SC、黑体 SC、Noto Sans CJK SC）
- 真实字重：每个家族至少 1 个；合计 17 个字重面
- 样本：12000 个
- 生成失败：0
- 资源包大小：约 97 MB
- bundle tag：`1734743097`
- manifest SHA-256：`d7ee3bcb608839eb7be9d4808f404146a08b5e194cf9eebfa60bab76b4051f24`

## 资源位置与占用

设备使用的是预渲染 A8 样本图，不在设备端解析 TTF/OTF。原始字体留在 Mac 的本地缓存，设备只接收：

`/sdcard/inkdesk/fontbench/manifest.json`

`/sdcard/inkdesk/fontbench/controls.i1`

`/sdcard/inkdesk/fontbench/tiles/*.t5`

因此不会占用固件 Flash，也不需要改 font_data 分区；它会占用 SD 约 97 MB。以后更换字体只需重新生成该目录并替换 SD 资源，保留目录外的评分日志。

## 字重行为

不同家族不一定有同样的 400/500/700 字重。网页只显示当前字体实际存在的字重；设备端遇到请求的字重不存在时，自动选择同一字体最近的真实字重，并在状态栏提示 `WEIGHT RESOLVED TO AVAILABLE FACE`。没有跨字体回退。

## 验收方法

1. 将 `tools/fontbench/sdcard/inkdesk/fontbench` 复制到 SD 的 `/inkdesk/fontbench`，覆盖旧同名目录。
2. 安全弹出 SD，等待设备回到 USB Serial/JTAG。
3. 打开「字体试验台」，依次选择 8 个字体；选择字号 22、算法 `N-A`、正文 `常用字`。
4. 选择「比字体」，四格都应显示样本，不能出现 `NOT LOADED`。
5. 再逐个选择当前字体提供的字重；不提供的字重不会出现在网页按钮中，真机请求会自动解析到最近真实字重。
6. 运行电脑端校验，确认 manifest 的 bundle tag 与设备状态一致；再做一次真机全屏观察。

## 边界

这次修复覆盖 FontBench 的样本加载链路。它不把原始字体文件变成设备端通用字体引擎；阅读器正文和系统 UI 仍使用各自已有的固件字库路径。MiSans 官方下载包约 379 MB，仅用于 Mac 端生成栅格样本，未复制到设备。

## 第二次校验与自动化防护

首次复制后发现有一个与源文件大小相同、内容不同的 SD tile；只按大小同步会漏掉这类损坏。随后使用逐文件内容校验同步，确认问题 tile 已修复：

`tiles/7-400-30-5-0-0.t5` 的源/SD SHA-256 均为 `26a0717200024745047eff6a50a6d238aba4ab7e`。

源与 SD 的 `manifest.json`、`tiles-sha256.json` SHA-1 也分别一致；以后更新资源必须使用内容校验同步，不能只使用 `--size-only`。

真机自动验收还记录到设备在无人操作时产生随机 `input.touch` 坐标，导致页面帧变化。已在下一应用构建中增加 `inkdesk.fontbench.lock`：自动验收期间屏蔽实体触摸/按键，电脑端配置命令仍可用；测试结束在 `finally` 中自动解锁。

## 真机复验结果

新应用已按 app-only 方式写入 `0x00080000` 并读回比对。自动锁生效后，关键验收目录 `artifacts/fontbench-all-20260918/device-acceptance-20260918h` 返回 `PASS`：26 个场景（6 种算法、关键字号 16/18/22/25/30/36/40，以及官方 UI 18/20/22/25/28/30）均解析到真实样本，未出现 `NOT LOADED`。`physical_quality` 仍为 `NOT_PROVEN`，因为锐度、残影、灰阶和阅读舒适度需要人眼在真机上确认。

随后又针对异常提示文案做了一个不改变资源解析逻辑的 app-only 增量：最终镜像 `78c2b80b75e233c92e58ddc73c84289dc18ccac76a3a87b087c7914b155ed58d` 已读回并 `cmp=PASS`；重启后重新打开字体试验台，bundle tag `1734743097`、默认卡片 `resolved_ok=true`。
