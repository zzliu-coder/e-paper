# CrossMux reading adapters

Source: https://github.com/0x1abin/crossmux
Pinned commit: `7dcd8b19031c9277376c7e0f8cacda4d7b5a43a1`.
Upstream MIT license is preserved under `vendor/LICENSE`.

perf10 imports the actual Section/Page/ParsedText/ChapterHtmlSlimParser pipeline,
CSS parser, CJK line-breaking, ruby, bounded table flow, MiniBidi, English
hyphenation, Expat, image dimension probe and serialization. PAPER provides ZIP,
font metrics/rasterization, PNG/JPEG pixels, SD ownership and the existing shell.
The production Reader calls this engine for EPUB when 电子书排版 is 图文排版.
TXT and the opt-in 兼容排版 retain PAPER's existing reader.

## Port contract

- Paths stay inside .paper/cache/crossmux-1/<book SHA>/<render SHA>/.
  Bookmarks/progress use separate rich-book records; old book records survive.
- Section partial/final files are SHA sealed, verified before load, and rebuilt
  on mismatch. Render key includes resolved fonts, fallback, size, weight,
  viewport, line spacing and rendering mode. Single cache file <=16MiB,
  section <=4096 pages. Cached files accumulate on SD; there is no automatic
  total-size eviction. They may be removed while the reader is closed; books,
  fonts and records must be retained.
- Image extraction is bounded to 1MiB compressed, 512K decoded pixels and
  3MiB decoder allocation budget. Unsupported/oversized/broken images use a
  placeholder without breaking subsequent text. PNG/JPEG are supported;
  SVG/GIF and OCR are outside this module.
- External entities and internal DTD subsets are rejected; XML depth <=128.
  Cancellation propagates through storage/parse/search and is retryable.
- File replacement accounts for FAT no-replace rename. Only derived cache
  or a validated alternating record slot may retire its old copy.
- No OTA/font-slot caching, partition reuse, eFuse writes or waveform import.
  Gray verification gate remains off.

## Deliberate adaptations

Section checks serialize/flush/close failures and caps page count; Serialization
validates bool bytes. Parser adds XML safety limits and initializer ordering.
Expat comment continuations are made warning-clean. LanguageRegistry includes
English patterns only. ImageBlock uses PAPER's existing bounded decoder;
GfxRenderer delegates to the installed SD fonts and existing 1-bit rendering.
Sup/sub, bold and italic use synthesized current-face shapes. This is not
browser CSS, arbitrary embedded font support or complex-script shaping.

Native crossmux_test exercises production source with FAT rename simulation;
crossmux_images_test uses a real EPUB containing PNG and JPEG. Firmware build,
host tests and physical display acceptance are recorded separately in
../../docs/PERF10_OFFLINE_RESULTS_20260920.md.
