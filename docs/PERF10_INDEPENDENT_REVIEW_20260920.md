# perf10 独立子代理复核

复核者：本任务子代理 review_perf10_offline。只读源码与隔离的主机 fixture，未访问设备。

发现并修复、复验的 9 项：

1. 图片配置误用占位模式：改用图像模式，缓存身份区分。
2. FAT 不支持覆盖目标 rename：派生缓存受限轮换发布。
3. 按排版行搜索漏掉跨行/跨页原文：按可见原文与统一字符偏移扫描。
4. 字号持久化失败只恢复公开设置：实际分页和位置一并回滚。
5. 预览把全章偏移用于当前页文本：当前页从本地起点预览。
6. 补字选项/字体身份未完整入缓存 key：补齐身份，更新同 key 的绘制规格。
7. 坏图遗留 renderer.failed 使下一次分页误报字体失败：图像错误独立，度量错误按批重置。
8. 已生成页读取取消永久污染文件句柄：取消只作用于当前操作，允许后续 seek/read 重试。
9. 首次加载反馈采样可能复用上一作业的旧帧计数：仅统计活跃作业，并拒绝在已有加载时启动该测试；增加 Python 回归。

独立复跑：

- 第 5 页预览成功且非空。
- 模拟保存失败后字号、render key 与第 4 页位置保持。
- 连续 48 字查询返回 8 条结果。
- 坏图占位后连续翻 21 页，到末尾得到正常 NotFound。
- 取消读取已有下一页后，清取消并在同会话 next 成功。
- 更新后的 crossmux_test 所有断言通过，包括真实 PNG、链接/返回、缓存复用/损坏重建、旧记录保留、取消及 DTD 拒绝。

首次复验临时证据：/private/tmp/perf10_review.cpp，
/private/tmp/perf10-review-test-2110 与 /private/tmp/perf10-review-full-2111。
复现用例已进入项目 tests/paper-maintenance/crossmux.cpp，避免只保留临时脚本。

结论：所审查范围内未再发现未解决 P0/P1/P2。
最后收尾时，子代理又独立复跑了新增真实 JPEG、FAT rename 模拟：
crossmux_test 与 crossmux_images_test PASS，目录为
/private/tmp/perf10-review-full-2131 和 /private/tmp/perf10-review-images-2131。
ASan/UBSan 的七组结果由主代理执行，见主报告，未冒称由子代理亲自执行。

本复核不证明实体 FAT 卡、物理显示、真机速度、蓝牙、恢复及长稳已经通过。
