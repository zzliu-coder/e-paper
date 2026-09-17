# Paper UI 1.0：原生组件使用说明

首个实现版本：`1.0.0-inkdesk-ui3`。目标是本项目 480×800 逻辑画布上的内置应用；没有引入网页、应用加载器或另一套操作系统。

## 文件与职责

| 文件 | 职责 |
| --- | --- |
| `main/ui/tokens.h` | 字体角色、间距、圆角、线宽、阅读区域、刷新阈值 |
| `main/ui/components.h` | Header、StatusBar、Panel、Button、SettingRow、Progress、Message、Dots、Texture、Dashed |
| `main/ui/typography.h` | 按实际字形宽度换行、基本标点禁则、连续分页与定位 |
| `main/ui/font_metrics.h` | 自动生成的两套字库 advance 表，禁止手改 |
| `main/ui/gallery.h` | 四组交互式校准与组件展示 |
| `main/ui/mono_blend.h` | 黑白对称的覆盖率混合，主机和设备共用 |
| `main/ui/lvgl_i1_override.h` | LVGL 9.5.0 I1 填充/文字/圆角路径的局部编译钩子 |
| `main/ui/lvgl_patterns.h` | 将静态网点、虚线绘制为原生 I1 小图元，按组件尺寸生成 |

所有新样机页面已调用这些组件；`ui_demo.cc` 中保留的短辅助函数仅转发公共 API。原有笔记、真实 EPUB/TXT 阅读器和诊断页面尚未整体迁移到新组件；它们共享的 LVGL I1 渲染修复会生效。

## 调用示例

```cpp
#include "ui/components.h"
// frame.reset()，设置本页 revision/epoch 后绘制。
paper::Header(frame, "设置", backAction);
paper::SettingRow(frame, {20,80,440,96}, "字体主题", "精密黑体", fontAction);
paper::Button(frame, {20,200,214,60}, "确定", confirmAction, 0, true);
paper::Button(frame, {246,200,214,60}, "处理中", confirmAction,
              0, false, paper::token::Body, paper::State::Busy);
paper::StatusBar(frame, {20,300,440,36}, "网络未连接", -1);
paper::Progress(frame, {20,360,440,20}, -1); // -1 表示进度未知，显示虚线框
paper::Texture(frame, {20,410,440,24}, paper::Tone::Subtle);
```

动作 ID 由页面分配，组件仅登记命中区域。Disabled/Busy 不登记可点击动作；Selected 用反白表现；错误/空状态用 `Message` 提供明确原因，再搭配重试按钮。`State::Error` 不自动生成错误文案，调用方必须给出可理解的标签/说明。

当前测试基线是至少 48px 的触摸目标。组件不自动扩张可能互相重叠的区域；布局作者需遵守此限制。屏幕刷新期间统一暂停输入、拒绝旧 epoch；页面不得绕过底座直接刷新屏幕。

## 字体与中文

- 主 UI 与当前字体实验室以黑体为活动范围：A/B 为思源黑体的栅格对照，C 为霞鹜新晰黑屏幕版，D 为文泉驿微米黑。霞鹜文楷只作为 UI3 历史回滚输入，不再出现在当前实验室。
- 原生字号：18 / 20 / 22 / 25 / 28 / 30。常规小标签20、反白说明22、正文25、标题28/30。
- 只嵌入当前 UI 所需子集，公开组件库并不等同于完整汉字字库。新增文案后必须重新生成字库。
- `host/build_ui_fonts.py` 收集 `main/ui_demo.cc` 和 `main/ui/*.h/*.cc` 中的非 ASCII 字符。新增独立页面目录时，应显式扩展扫描路径，然后构建并检查缺字。
- 自动生成程序同时输出 LVGL 字库与 `font_metrics.h`，保持换行宽度和实际绘制一致。
- 本轮未改变真实 EPUB 阅读器的字体选择和排版引擎；样机分页只针对内置演示文章。
- 阅读样机现在按连续文本实际分页，已去除旧的交替段落与虚构128页。展开和收起工具保持相同正文区域；换字号/主题后定位到包含原阅读位置的页面。

## 黑白、网点和刷新

- 本版本仍为 1-bit 黑白输出。灰色是固定空间网点，25%/50%/75% 黑色覆盖率，无时间抖动、无新 LUT。
- 小字保持实黑/实白；网点用于装饰或校准块，不作为禁用/选中状态的唯一提示。
- 圆角默认12px、线宽1px。校准页可比较0/8/12/16px以及1/2px，最终物理偏好仍可调整 tokens。
- 页面/主题切换全刷；同页更新沿用现有快刷，积累8次后的下一次操作全刷。静止时无定时刷新。
- 校准页有显式“全刷对照 / 局刷对照”，只重绘同一页，便于对比残影；不修改持久配置。
- A/B 主题和校准选择只保存在本次开机 RAM，重启默认 A。物理效果不能通过返回成功或帧缓冲相同来代替确认。

## 编译钩子的维护

`main/CMakeLists.txt` 为 LVGL 的 I1 源文件单独强制包含 `lvgl_i1_override.h`。供应商文件保持原样；钩子覆盖标准色填充、mask、opacity以及组合分支。其它图像混合路径没有声称全部重写。

构建检查 LVGL 9.5.0 版本，升级依赖必须复核钩子、透明度和裁剪测试。遗留 `epd_i1_glyph_thin.h` 当前不参与该钩子；不要把“所有非零覆盖都落黑”再接入。

## 测试和复现

在 SDK 根目录：

```sh
/usr/bin/python3 host/build_ui_fonts.py
/usr/bin/python3 host/test_ui_native.py --output artifacts/inkdesk-ui3/frames.jsonl
/usr/bin/python3 -m pytest -q tests
cmake -S tests/lvgl_host -B ../../work/ui3-lvgl-host -DCMAKE_BUILD_TYPE=Release
cmake --build ../../work/ui3-lvgl-host -j 8
../../work/ui3-lvgl-host/paper_render_test artifacts/inkdesk-ui3/lvgl-frames
```

主机测试包括：覆盖率×透明度穷举、黑白互补、非字节对齐与stride保护、网点密度、禁用/忙碌无动作、连续分页无丢字/重复、工具显隐正文不变，以及20,000次随机导航。

LVGL 主机测试使用相同9.5.0源码、I1钩子和生成字库，比较2套主题×6个字号的真实文字绘制互补，并输出页面。输出用于软件像素检查，不能模拟光学残影。

安装前沿用 `FLASH_WORKFLOW.md` 核验设备；仅 app0 更新。设备端自动验收：

```sh
/usr/bin/python3 host/ui_acceptance.py --version 1.0.0-inkdesk-ui3 --out artifacts/inkdesk-ui3/runtime
```

入口：**首页 → 设置 → 校准与组件**。左右/上下翻页键可切换校准分组；点顶部返回首页。真实物理观感仍由用户集中确认。
