# Private bounded JPEG decoder

Source: the existing managed LVGL dependency, `src/libs/tjpgd/`, ChaN R0.03
(2021), including LVGL's incremental MCU API and `pool_original` field.
The original license is preserved in LICENSE.txt and tjpgd.c.

PAPER changes: independent grayscale/scaled configuration; `paper_jd_*`
symbol prefix; no LVGL header dependency; reject duplicate/short SOF0,
non-8-bit precision, and short DRI/SOS segments.

Used only for baseline JPEG images above the existing stb pixel cap.
Compressed input <=1 MiB, edges <=4096, output <=512 KiB, work pool 16 KiB.
Progressive large JPEG and oversized PNG return explicit errors. The original
book is never modified. Rendering is committed only after complete decoding.
