# CrossMux portable TXT code provenance

Source: https://github.com/0x1abin/crossmux
Pinned local commit: `2f6bf017035b1b172cfe5cc77442e0cdb20bd65d`.
The supplied R2 package pins a newer complete CrossMux integration; this SDK port deliberately uses the already-local verified source below, not an unverified claim to be that newer revision.

Files copied unchanged from `lib/Txt/`; working-file Git blob hashes matched the commit tree before copying:

- TxtEncoding.cpp: `cb50072ac8d697b2f7ffee3f15a7321d8c719a4c`
- TxtEncoding.h: `9043e5ae326bf38ef8466f24cf4081f0f150b15e`
- TxtParagraph.h: `eb1c2b1858a1cbab0f5272cbf73afd5a8c7f84f0`
- TxtPageIndex.h: `091d2441f4de31d812ec2bb51fc7b56f53cd0826`
- GbkToUnicodeTable.inc: `5e975037ff79e2ccd09488210faf5d26e06051de`

LICENSE is retained verbatim. GBK support is enabled for the translation unit through CMake, without altering upstream code. The generated table is copied as provided, not regenerated or edited.

This is a module port, not a complete CrossMux firmware. Used: UTF-8/GBK detection/transcoding, paragraph classification and page-offset/progress helpers. New bounded POSIX adapter in `../inkdesk_reader.cc` provides a basic TXT library and rendering through our official Metalio drivers. CrossMux's Activity/GfxRenderer/HalStorage, EPUB, CSS, bookmarks, network services and full reader settings are not present in this port.

Memory: adapter object and 8KiB decode buffer are embedded in the existing PSRAM application runtime; book list capped at32, page-offset index pre-reserved and capped at4096. Reads are2KiB, no complete book load, no recursive scan. Text/source files are read-only. A private SDK example is created with exclusive-create semantics, never overwriting existing files.
