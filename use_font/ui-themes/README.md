# UI 字体主题与字体实验来源

- A：Source Han Sans CN Medium / 思源黑体 Medium。
  源文件来自 Adobe 官方仓库 release commit `a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2`，
  `SubsetOTF/CN/SourceHanSansCN-Medium.otf`。
  https://github.com/adobe-fonts/source-han-sans/tree/a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2
- 历史 B：LXGW WenKai GB Screen / 霞鹜文楷屏幕阅读版（陆标），v1.522。它只保留作历史回滚输入，当前活动字体实验不再展示楷体。
  https://github.com/lxgw/LxgwWenKai-Screen/releases/tag/v1.522

## 字体实验室 2 候选

- C：LXGW Neo XiHei Screen / 霞鹜新晰黑屏幕版，来源和屏幕版调整说明：
  https://github.com/lxgw/LxgwNeoXiZhi-Screen
- D：WenQuanYi Micro Hei / 文泉驿微米黑，紧凑 CJK 无衬线字体：
  https://github.com/anthonyfok/fonts-wqy-microhei

候选原文件、许可和抽取后的 WQY Regular 字体位于 `candidates/`。它们只作为可复现构建输入；应用嵌入的是固定测试语料的 32 个原生字号字库，不是完整字体重新发行。

原字体的 OFL、IPA 和 Apache 许可均保存在同目录。原始字体仅作为可复现构建输入；应用只嵌入
UI 演示所需字形，使用内部 `ui_font_a*` / `ui_font_b*` 与 `lab_p*_*` 标识。
不是完整字体重新发行。每次生成的源文件哈希见 `build-manifest.json` 和 `font-lab-manifest.json`。

生成：`/usr/bin/python3 host/build_ui_fonts.py`。依赖 Pillow、fontTools。
直接在目标 18 / 25 / 28 / 30px 栅格化，保留 2bpp 覆盖率；不缩放旧位图，
不修改设备 font_data 分区。固件实际黑白转换和墨水屏效果需分别验收。
