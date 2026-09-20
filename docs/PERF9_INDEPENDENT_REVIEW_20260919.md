# perf9 独立子代理复核

复核者：本任务子代理 `review_perf9_offline`，只读源码与独立主机 fixture；未操作设备。以下为其最终结果整理，不扩大验收范围。

结论：所检查范围内，未发现仍未解决的 P0/P1/P2。

发现并修复后复验：

1. 同名队列命令可误用旧回执：增加 request_token，主工具及 A/B、加载、运行测试都验证。
2. 改名后最近阅读记录失败会停留失效输入页：先返回书库，保留改名结果并提示；独立双槽记录损坏注入通过。
3. 字形预备状态快照不更新：有变化时只发布状态，不额外刷新屏幕。
4. 字重说明与书籍强调合成冲突：明确真实所选字重与书内合成强调的区别。

独立复跑：performance、reader_content、offline、Bluetooth、refresh 五项原生及五项 ASan 测试 PASS；命令/长稳工具五项单测、恢复工具两项单测 PASS；相关 Python 工具语法检查 PASS。

独立 fixture：`/private/tmp/paper-perf9-review.dmiCxk/`。损坏注入仅针对新 fixture 的记录和字体副本。

不能据此通过：完整 CrossMux Section/Page 与持久排版缓存；字体预备收益排名；真机灰阶/残影/刷新耗时；实际蓝牙 UART 连接通知/声音；恢复演练；八小时真机稳定性。8ms 预备预算为字形之间检查的软预算，不能强制打断单次 SD I/O。
