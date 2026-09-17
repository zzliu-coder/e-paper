# 字体实验室 3

入口：UI 样机首页 → 设置 → 字体实验室。旧的楷体主题已经从活动入口移除；历史字库仍保留在源码中，便于回滚和追溯。

给实机观察用的简明步骤见 [FONT_LAB_GUIDE.md](FONT_LAB_GUIDE.md)。

## 实验范围

六个页面：字号、算法、反白、中英混排、实景、位深。前五页上方选择 A/B/C/D；减号/加号切换 16、18、20、22、24、28、32、40 px。物理翻页键也可切换页面。
每次改变方案使用完整刷新；空闲不反复刷新。字体是原生字号生成，没有整屏缩放。

| 编号 | 字体与生成方式 |
| --- | --- |
| A | 思源黑体 Medium，Pillow 灰度栅格化 → 原有 2-bit 量化 → 阈值 128 的单色结果 |
| B | 思源黑体 Medium，`lv_font_conv` 1.5.3 MONO 栅格化，1-bit 输出 |
| C | 霞鹜新晰黑屏幕版（LXGW Neo XiHei Screen），`lv_font_conv` 1.5.3 MONO 栅格化，1-bit 输出 |
| D | 文泉驿微米黑（WenQuanYi Micro Hei），`lv_font_conv` 1.5.3 MONO 栅格化，1-bit 输出 |

四种都关闭实验字库的字偶距，避免字号、字重和算法同时变化。A 是 Pillow 阈值结果；B/C/D 当前使用 `lv_font_conv` 的 MONO 路径。它们最终都会进入本机的 1-bit 墨水屏路径，因此 C/D 是字体来源与字面设计的对比，不代表面板具有真实灰阶。
“位深”页调用已经存在于 `font_data` 分区的 MiSans 字库：25/30 px 的 2bpp 覆盖率，以及 30 px 的 4bpp 覆盖率。设备最终仍由 I1 面板路径量化成黑白，这一页用来判断字形轮廓和覆盖率是否值得推广，不会改写 `font_data`，也不等于真正的四灰阶。
当前生成器依赖本机 `build-manifest.json` 记录的 Pillow、fontTools 和固定的 `lv_font_conv` 1.5.3。字库源码哈希与生成文件哈希在 `use_font/ui-themes/font-lab-manifest.json`；原始字体及相应许可保留。
LVGL 工具参考：<https://github.com/lvgl/lv_font_conv>。

## 选型依据

这轮把“像微软雅黑的气质”和“针对小屏/屏幕的字面调整”拆开比较：

- A/B 是同一套思源黑体，专门用来判断栅格化算法是否带来改善；B 是目前观察到“略好一点”的微调基线。
- C 采用霞鹜新晰黑屏幕版。它基于 LXGW Neo XiHei，针对屏幕显示加粗并调整字面与字面框，是本轮最接近“雅黑感 + 屏幕版”的候选。项目说明见 <https://github.com/lxgw/LxgwNeoXiZhi-Screen>。
- D 采用文泉驿微米黑。它是紧凑的 CJK 无衬线轮廓字体，历史说明提到手持/嵌入式用途，用来验证较小字库和较少笔画细节是否更适合资源受限设备。来源说明见 <https://github.com/anthonyfok/fonts-wqy-microhei>。

补充候选暂不放进本次固件：Noto Sans CJK / Source Han Sans 属于同一设计家族的完整 Pan-CJK 字库，覆盖很好，但当前应用分区只剩约 2%，再塞一套全尺寸实验字库会挤压恢复和日常功能空间；官方说明见 <https://github.com/notofonts/noto-cjk> 和 <https://github.com/adobe-fonts/source-han-sans>。Sarasa Gothic 更偏编程等宽场景，本轮不把它当中文阅读默认字体。

像素差异只说明字形或栅格结果不同，不能替代真实屏幕上的清晰度判断；最终优选仍要看正常阅读距离、反光、残影和黑底白字的可读性。

## 怎么比较

先在“算法”页观察同字号四行，再选 A/B/C/D，到“反白”查看白底与黑底。最后到“实景”比较常用标签和按钮。在正常阅读距离观察断笔、糊成一团和细线消失。
选中想评价的方案和字号，点击“清楚/太细/太粗/粘连”。“算法”页的评价归属上方选中方案；“字号”页包含全套字号，其记录中的 size 仍是上方选择的目标字号。

## 评价与读回

只在点击评价时，追加 `/sdcard/inkdesk/font-lab.jsonl`；日志限额 256 KiB，满后报保存失败，不会清理旧记录。保存成功与失败在页面底部明确显示。记录固件版本、boot_id、显示帧 revision、运行时间、页面、方案、字号、评价、上一帧是否全刷。
编号：page 0字号/1算法/2反白/3混排/4实景/5位深；profile 0A/1B/2C/3D；设备位深页内部使用 4=fontpack 2bpp、5=fontpack 4bpp；feedback 0清楚/1太细/2太粗/3粘连。
实验选择在 RAM，重启恢复默认。评价日志留在 SD；没有 SD 时仍可比较，但记录失败。

电脑读取 `inkdesk.status` 可查看 `lab_*` 字段。导出评价：

```sh
python3 host/font_lab_export.py artifacts/font-lab3.1/ratings-本轮.jsonl
```

导出使用常驻 USB 服务，不另开串口；目标文件已存在时拒绝覆盖。不会把自动化点击伪装为用户的视觉评价。电脑直接打开实验页：`python3 host/font_lab_open.py`。

## 可复现与自动检查

沿用项目的 ESP-IDF 5.5.4 和专用 Metalio 配置。字体转换器本地位于工作区 `work/font-lab-tools/node_modules/.bin/lv_font_conv`，版本 1.5.3。

```sh
python3 host/build_font_lab.py --converter ../../work/font-lab-tools/node_modules/.bin/lv_font_conv
python3 host/build_ui_fonts.py
python3 host/test_ui_native.py
cmake -S tests/lvgl_host -B ../../work/ui3-lvgl-host
cmake --build ../../work/ui3-lvgl-host -j 6
../../work/ui3-lvgl-host/paper_render_test artifacts/font-lab3.1/host-renders
python3 host/font_lab_preview.py
python3 -m pytest -q tests
python3 host/font_lab_acceptance.py --out artifacts/font-lab3.1/runtime-rerun
```

主机用与固件相同的 LVGL I1 渲染器检查 192 种组合（6 页 × 4 方案 × 8 字号）的布局、缺字和反白对称性；位深页在主机上使用静态字体作布局替身，设备帧不与它做像素相等断言。设备脚本验证版本/启动标识、导航、字号边界、位深页、帧读回和空闲稳定，最后停在算法页。运行期间不要点屏幕，否则帧 revision 改变会使读回检查中止。

本轮第一次设备验收在快速切换方案时遇到一次 `stale_frame`：逻辑 revision 已更新，墨水屏像素 revision 尚未发布。脚本现已等待两个 revision 都推进后再读帧；本轮结果记录在 `artifacts/font-lab3.1/runtime-rerun/acceptance.json`。`font-lab3` 与 Font Lab 2 目录保留为历史证据，不作为本轮结果。

## 已知边界

- 当前是第二轮“算法 + 屏幕向黑体 + 紧凑嵌入式黑体”；Regular/更细字重、专用中文点阵字体、4-bit 网点方案尚未加入。
- 没有切换面板灰阶波形；四种实验都是单色输出，不能标为真实灰阶。
- 位深页的 2bpp/4bpp 是字库覆盖率输入，经过现有 I1 混合后仍是黑白像素；真正的 4 灰阶需要单独的面板驱动、波形和读回验收项目。
- 实景为可操作的实验卡片，实验字体暂不全局替换 Home/真实阅读器。先取得实机偏好再推广。
- 生成的 32 个实验字库仅覆盖固定测试语料及可打印 ASCII，不接受任意文本，避免无提示缺字。
- 字号越小可用像素越少；实验负责找出清晰度边界，不承诺任意字号同样平滑。
- 当前构建的最小应用分区只剩约 `0x16c00`（约 2%）；后续再增加字体必须优先改为 SD/外部资源加载或减少实验字形，不能随意扩分区或改变刷写地址。
- 主机渲染与设备帧读回不代表真实屏幕视觉质量；反光、残影、白字可读性仍需人眼判断。
