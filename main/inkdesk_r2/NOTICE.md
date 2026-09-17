# Attribution / resource boundaries

This package contains newly written InkDesk source, integration code and tests.
CrossMux, FreeInk SDK and their dependencies are fetched separately by the
optional preparation script; no complete upstream checkout is bundled.

The pinned CrossMux main LICENSE is MIT and names:
- Copyright (c) 2025 Dave Allie
- Copyright (c) 2026 FreeInk
- Copyright (c) 2026 EEGO A4 Template Firmware contributors

Preserve upstream licenses/notices when preparing or distributing a combined
build. Individual dependencies and assets may carry additional licenses.

The bundled pinyin seed table was authored for this prototype. It is small and
has not been independently proofread as a production dictionary.

The preview uses the user's system fonts. No font files, font bitmap packs,
factory firmware, personal recordings, device backups or credentials are included.

The CloudZao/Metalio-E-INK4 commit recorded in evidence/official-reference.json
was inspected only as a reference. Its firmware, LUTs, LVGL/cloud/audio modules,
font assets and binaries have not been imported. InkDesk keeps its separately
pinned CrossMux/FreeInk integration.
