# InkDesk R2 + Metalio 开发底座

版本：`1.0.0-inkdesk-r2.3`。基于当前官方 ESP-IDF 5.5.4 工程，复用原厂 Metalio 板级驱动、现有字体分区、USB ML1 协议、自检和 Mac 服务；不是 CrossMux 全量固件。新增官方EPUB正文阅读、USB传书和SD全文件读回，详见 [BOOKS.md](BOOKS.md)。

来源：用户提供 `InkDesk_Metalio_R2.zip`，SHA256 `bb613c23be91f7479f0f8138a13cd49d0b6449c8da1b71588a2a8eaea2a80ef5`。原始应用核心及许可证保存在 `main/inkdesk_r2/`；适配层 `main/inkdesk_app.cc`。

## 设备上怎么用

- 开机进入“纸间”：记事本、待办、基础拼音/英文输入、显示测试、Markdown导出。
- 首页“设备自检”进入原来的13项硬件检查；自检首页“返回纸间”回到应用。
- 底部Home返回应用首页，左右/上下键映射上一页/下一页；AI/BOOT键进入更多工具。
- 笔记/待办使用SD `/inkdesk/state.a`、`state.b` 双存档，CRC与写回核验。未知/损坏存档保护沿用R2。正在编辑的拼音和未保存待办草稿仍不保证硬断电恢复。
- 导出目录为SD `/inkdesk/export/`，不会自动联网或上传内容。
- 电子书入口读取SD根目录、`books/`、`inkdesk/`中的TXT/EPUB；默认传书到`books/`。TXT支持UTF-8/GBK；EPUB支持正文、跨章节前后翻页和本次会话进度。首次进入会排他创建 `inkdesk/SDK-reader-demo.txt`，不覆盖已有文件。

## 保留的开发能力

USB服务与前台应用独立任务运行。`hello`、`ping`、状态、输入事件、日志和原有诊断接口保留。硬件自检期间暂停应用画面，避免互相抢屏。

```sh
python3 host/service.py serve --port /dev/cu.usbmodem11301
# 在另一个终端使用（串口名以实际枚举为准）
python3 host/service.py call inkdesk.status
python3 host/service.py call selftest.open
python3 host/service.py call inkdesk.open
python3 host/service.py call inkdesk.tap --args '{"x":100,"y":230}'
```

`inkdesk.tap/key` 是有界输入队列：接收成功不等于画面已经完成，需查询 `inkdesk.status` 的页面、revision、frames和refresh_fault。Mac不会伪造设备物理验收确认。`display.text/scene.set`接管画面时暂停应用绘制，再用 `inkdesk.open` 返回。

同一进程持续连接能力保留；服务可独立后台运行，但未配置Mac登录自启。固件仍是编译更新，不是任意原生代码热加载，也不是完整Android ADB。

## 适配边界

- CrossMux完整阅读器没有搬入；已复用编码、段落和页偏移模块，源码及MIT许可见 `main/crossmux_txt/UPSTREAM.md`。EPUB使用官方解析器，不是CrossMux完整引擎。复杂CSS/图片、书签、断电阅读进度、CrossMux网络服务尚未接入。日历/消息/AI沿用R2预留状态。
- TXT书库最多32本，扫描不递归，单本最多64MiB；索引最多4096页，使用固定字号基础排版。每次只读2KiB，最多8KiB转码缓冲，不整本加载；不承诺CrossMux完整排版效果。
- 设备设置入口在本工程接到SDK自检，不假称已移植CrossMux设置。
- 屏幕使用官方LVGL/SSD1677通路，R2决定何时快刷/清屏；快刷仍是整帧传输，不宣称硬件局部窗口更新。
- 物理字体使用现有18/25/28/30号字体映射，与电脑预览不保证像素一致，触摸/字形需实机确认。
- 完整恢复演练与8小时稳定性未因此自动通过。主机测试、写入读回和真实画面效果分别记证据。

## 验证与恢复

构建、写入清单、读回及实机运行日志位于 `artifacts/inkdesk-r2.3/`；前版r2.2/r2.1及其证据保留。仅更新app `0x80000`，不改分区、启动程序、字体、NVS或eFuse。失败时可按 `docs/FLASH_WORKFLOW.md` 回退已核验r2.2/dev.10/官方应用；原厂16MiB备份保持不变。

电脑端可重复验证：`python3 host/test_reader_native.py --idf ../../work/esp-idf-v5.5.4`，使用临时模拟SD目录和ASan/UBSan，不操作真机。
