# SD 电子书与 USB 传书（InkDesk r2.3）

默认电子书目录是设备 SD 卡 `/books/`（固件挂载路径 `/sdcard/books/`）。书不放进固件分区；app-only 更新不擦 SD。Mac 原书只读，传输副本。

## 阅读

纸间首页 → 阅读 → 点击书名。屏幕底部“上一页 / 回书库 / 下一页”；侧边上下与盖板左右沿用已有上一页/下一页映射。首次进入书库扫描 SD，不递归，最多32本。新传书后退出阅读并重新进入刷新列表。

TXT 使用已移植 CrossMux 编码/段落/偏移模块；EPUB 使用官方 `reader/EpubDocument`，不是整个 CrossMux EPUB 引擎。基础版只显示正文和图片占位符，不加载远程内容，不显示复杂 CSS/插图；不解密 DRM。章节最多512KiB，单会话最多4096页。异常以页面错误提示呈现，不静默跳过章节。前后翻页跨章节可用，阅读进度尚不跨重启保存。

## Mac 传书

在工程根目录且 USB service 已连接时：

```sh
python3 host/book_upload.py /absolute/path/book.epub \
  --name my-book.epub --output artifacts/books/my-book.epub
```

目标文件名采用 ASCII 字母、数字、连字符、下划线，扩展名 `.epub`；书页标题使用 EPUB 内标题。最大16MiB。已有同名文件拒绝覆盖，换一个名字重试。

Apple Books 的 `.epub` 有时是目录：工具只读重新封装 ZIP，mimetype 为首个未压缩条目，保留书本资源，不转成 TXT；输出 ZIP 必须是新路径。`--prepare-only` 仅在 Mac 检查/封装。已封装 ZIP 可直接作为 source，不需要再复制。

### 证据与失败行为

1. Mac 检查 ZIP CRC、mimetype、加密资源和大小。
2. `book.begin` 声明文件名/长度/SHA256，取得随机 token，创建唯一临时文件。
3. `book.chunk` 每块最多1024字节，必须严格递增偏移；无自动重试。
4. `book.commit` 检查总长度和SHA256，关闭文件后改名发布到书库；拒绝已有目标。
5. `book.read` 从SD完整读回，Mac重新算SHA256，匹配后才出传输PASS回执。

读取仅限 `/sdcard/books` 下的安全 `.epub/.txt` 文件名，不提供任意路径读取。错误/主动abort只清除本会话独占的临时文件，不删除已有书籍。120秒未收到传输命令后，下次book命令清理该会话未完成文件。掉电后的隐藏 `.sdk-upload-*.part` 不会出现在书库，目前不自动清除历史残留，也不跨重启续传。

传输PASS只证明SD字节一致，阅读命令测试与屏幕物理效果另记。固件变更仍需编译/受控app-only更新，电子书和资源传输不需要重新刷固件。

## 重复验证

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 host/test_reader_native.py --idf ../../work/esp-idf-v5.5.4 --epub /path/book.epub
python3 host/book_transfer_checks.py
```

原生测试实际使用官方 ZIP/HTML/EPUB 解析器，只有 LVGL 类型、日志、内存分配和未使用的图像解码入口用主机替身；ASan/UBSan 检查整本往返翻页。设备负向测试只创建/终止私有临时传输，不生成正式测试书。
