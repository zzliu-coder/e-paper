# 统一字体试验台（当前入口）

当前操作只使用一个参数面板：字号、字体、算法、墨色直接选择。新入口、源码和 SD 资源操作见 [FONT_BENCH.md](FONT_BENCH.md)。下方内容保留为 Lab3 / Lab4 历史记录，原页码流程不再用于当前实验。

---

# Font Lab 4 实现说明

**扩展实验采用离线原生像素页面 + SD 加载。现有 LVGL I1、旋转、刷新调度和默认阅读器保持。**

基于 `zzliu-coder/e-paper@7ce967e28188ce28e9c4c0a3b2ae1b7c9e7896ea`，固件版本改为 `1.0.0-fontlab4`。本轮源实现没有在交付环境完成 ESP-IDF 整体编译或真机测试。

## 文件职责

| 文件 | 实现 |
|---|---|
| `host/fontlab4/raster.py` | FreeType精确px、显式load/render flags、未量化覆盖率与共用宽度度量 |
| `host/fontlab4/build.py` | 六组页面、完整16—40px、正文连续分页、字体/字重/阈值/同源位深、字形边界检查 |
| `host/fontlab4/packet.py` | 定长I1页面容器、CRC/SHA检查、拒绝有中间灰的I1输入 |
| `main/ui/fontlab4/core.*` | 有界文件解析、样本身份、导航、评分、刷新序列；与主机共用真实C++代码 |
| `main/ui/fontlab4/device.h` | 复用现有LVGL I1 image方式；稳定图片生命周期；SD评价写入 |
| `main/inkdesk_app.cc` | 接入原worker/队列/epoch/刷新流程、状态发布、新命令；旧字号精确请求 |
| `host/font_lab4_acceptance.py` | 六组实机冒烟、按样本ROI核对像素、正确处理嵌套状态/过期帧 |
| `host/font_lab4_list.py` / `font_lab4_open.py` / `font_lab4_export.py` | 查找页码、USB打开指定页、导出评价 |

## 生成

在独立Python环境安装 `host/fontlab4/requirements.txt`，再运行：

```sh
python3 host/build_font_lab4.py --out /新的目录/font-lab4
```

默认读取仓库已有的 SourceHanSansCN-Medium.otf、LXGWNeoXiHeiScreen.ttf、wqy-microhei.ttc。`--sources` 支持JSON来源配置（路径相对配置文件），最多五个来源，字体文件和全部输出都有来源身份。构建清单记录原字体sha256、face、weight、axes、实际工具版本、算法参数、生成器hash及缺字。MiSans要从真实轮廓来源生成，不能用不同规格旧fontpack冒充同源对照。

注意：当前设备上安装的 `font-lab4` SD 包是随实现包预生成的 Noto Sans CJK SC Regular/Bold（页面标签为 `NotoSC-R` / `NotoSC-B`）。它用于先验证 FontLab4 的页面容器、FreeType 算法、字重、阈值、刷新和读回链路；它不包含仓库里的思源黑体、霞鹜新晰黑、文泉驿微米黑。三套字体需要在本仓库用默认来源重新生成一份资源包后，才会出现在设备页面中。

每个样本同时记录母版哈希和最终区域哈希。阈值比较发生在原始A8，先量化为2bpp后微改阈值无效的问题已避开。M-A/M-N使用MONO输出，其余使用A8再阈值化。位深页单独从相同母版量化为2/4bpp，其最终I1结果可以一致。

| ID | 加载参数（另加NO_BITMAP） | 渲染 |
|---|---|---|
| M-A | FORCE_AUTOHINT + TARGET_MONO | MONO |
| M-N | NO_AUTOHINT + TARGET_MONO | MONO |
| N-A | FORCE_AUTOHINT + TARGET_NORMAL | NORMAL |
| L-A | FORCE_AUTOHINT + TARGET_LIGHT | NORMAL |
| N-N | NO_AUTOHINT + TARGET_NORMAL | NORMAL |

原生微调路径不保证字体本身具有有效的hint。字体相关表是否存在会记录，不能把表存在当作效果优劣结论。参考 FreeType 官方文档：https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html 。

内嵌点阵关闭，字偶距关闭，栅格位置按整数对齐；换行和绘制共用实际advance。渲染到带保护边的局部画布，发现笔墨越过声明区域就停止生成。生成器输出完整页面，不输出TTF/OTF/TTC或可分发字库文件。

## 设备格式和边界

`FLAB4P\0\0` + 6个小端uint32字段（version=4、JSON长度、frame长度、JSON CRC32、frame CRC32、reserved=0）。JSON最长16KiB；frame固定48000字节；480×800、行stride60、MSB先、0黑1白。每页最多5个不重叠样本，页码0—4095。

加载检查元数据、区域CRC、尺寸、导航、样本ID、允许的算法及构建ID。缺失、损坏、错规格和混合包不静默回退。旧图片对象独立持有PSRAM副本，直到LVGL删除事件再释放；读取下一页不会使上一张图的指针失效。

记录在用户点投票后追加到独立JSONL，最大256KiB；日志满、不可写或关闭失败会显示失败。记录含母版和区域身份、实际帧revision、实际全刷/快刷、累计快刷与本次序列。选样本与刷新不自动制造投票。

状态保持紧凑，完整来源放在SD清单与评价里。交付资源中新增lab状态最长1389字节；它仅在当前实验页显示，避免给原ML1 4096字节缓冲塞入全部样本文案。其它页面的协议信息不增加。

## USB 接口

沿用现有服务和ML1命令入口，新命令也加入hello能力表：

| 命令 | 内容 |
|---|---|
| `inkdesk.status` | 当前 `app.ui_demo.fontlab4`；紧凑样本规格及ROI SHA |
| `inkdesk.fontlab4.page` | `page`为0—4095整数；通过原输入队列切换RAM/UI，忙碌时明确拒绝 |
| `inkdesk.fontlab4.log` | `offset`为0—262144，分块读评价；只读固定路径 |
| `inkdesk.ui.frame` | 沿用原revision绑定的物理800×480帧读回 |

主机工具旋转读回帧为逻辑480×800，再按每个ROI逐像素求SHA。按钮高亮变化不能代替字体样本验证。固件报告实际规格，无法解析的样本停止计分。没有改写scene或书籍传输协议。

## 刷新

每组是alternate → target两次重画。1组或4组按钮显式启动，最多8次；暂停输入，保留原调度器对累积次数的全刷升级。所有实际模式都记录，结束在与基准相同的目标画面。全刷按钮清空旧序列后做本页全刷；空闲无定时重画。

## 检查

```sh
python3 -m pytest -q tests/fontlab4
python3 host/test_font_lab4_native.py --resources /资源/font-lab4 --out /新的结果目录
```

native工具需要主机cJSON库，可用 `--cjson-library /实际路径/libcjson.dylib` 等显式指定。实际核心用 `-Wall -Wextra -Werror`、ASan/UBSan检查。`tests/fontlab4/stubs/cJSON.h`只声明测试ABI，真实解析器链接主机cJSON；它不参与固件编译。`adapter_stubs`仅用于图片生命周期测试，没有实现LVGL绘制。

上机后：

```sh
python3 host/font_lab4_acceptance.py --out /新的设备结果目录 --manifest /资源/font-lab4/manifest.json
```

既有 `host/font_lab_acceptance.py` 现在转入新验收入口。软件核对不代表光学效果。准备资源在相同工具环境下独立生成两次完全一致；跨FreeType版本需要新构建身份与新评分。

## 四灰阶

`host/fontlab4/grayscale_probe.py`提供480×800、96000字节的四级测试输入和能力前置检查。它没有SPI发送或波形替换逻辑；当前固件只接受I1扩展页。必须先确定实际面板/FPC与匹配波形，再独立验证灰级稳定性、复位重画、残影与黑白恢复。本轮没有把灰级波形加入SSD1677驱动。

## 推广边界

扩展页用于比较，SD文件不能当作动态电子书字库。正式UI和EPUB/TXT默认字体没有改动。确定入围配置后，使用同一生成参数导出产品字库，并同步其字宽、行高、基线及分页回归。字体分区、显示驱动和读书数据保持可追溯。
