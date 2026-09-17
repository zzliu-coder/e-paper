# USB diagnostic reboot investigation

## Confirmed defect

dev.7 reserves 8192 bytes for sdk_usb. Disassembly of its ELF shows:

- Transport at 0x4200b7d8: `entry a1, 0x1360` (4960 bytes).
- Send part at 0x4200a89c: `entry a1, 0x1020` (4128 bytes).
- Transport calls Send; these frames overlap in lifetime. Their sum is 9088
  bytes, already exceeding the task stack before further callees are counted.
- Source has a 4097-byte receive buffer and a 4096-byte send buffer on stack.

This is a proven stack budget defect. The captured interrupt watchdog panic
at Hardware/xQueueReceive is consistent with memory corruption; its exact
corruption path has not been captured. Moving PSRAM scene allocations or
delaying status requests did not establish a fix. Do not describe those earlier
timing theories as established causes.

## Correction

dev.8 makes both bounded protocol buffers static, owned exclusively by the
single Transport task. Send must remain confined to that task. Status exposes
`usb_stack_free_min_bytes` for the task's minimum remaining stack.

## Recovery and validation

The interrupted UI turn did not interrupt the dev.3 recovery writer: its log
ends with hash verification and hard reset. Live hello subsequently identified
dev.3; six heartbeats through uptime 12007 ms retained one boot ID, becoming
board_ready at 4007 ms. Evidence: artifacts/dev3/recovery-check-20260916.jsonl.

dev.8 requires a successful build, frame-size inspection, app-only write/readback,
the original early-boot status probe, repeated commands, and scene readback before
the reboot regression can be marked resolved. Physical hardware acceptance is
separate.

## Completed dev.8 verification

- Build and 8 host tests PASS. Transport frame reduced to 0x340 (832 bytes).
- Live MAC 10:20:ba:6e:0b:e0; Secure Boot and Flash Encryption disabled.
- App-only write at 0x80000, 4025376 bytes; sector range [0x80000,0x457000).
- Independent readback is byte-identical. SHA-256 for both:
  b1944c3e9a3d4b3c168daff02196178b0e01efd4d474d7edbc072bff5b0977da.
- Three serial sessions passed the original early-boot status polling, with
  120 additional status queries. No watchdog panic or in-session reboot.
- All 14 automatic command checks returned PASS, including microphone sampling,
  Wi-Fi scan, IMU read, RTC and the private SD file roundtrip. These do not prove
  physical button mapping, audible playback, Wi-Fi connection or BT pairing.
- Scene set and complete 48000-byte RAM framebuffer readback PASS.
- Lowest sampled USB stack headroom: 5524 bytes (before scene test).
- Evidence: artifacts/dev8/reboot-regression.jsonl; write.txt; readback.txt.
- Regression runner: host/reboot_regression.py. This bounded regression passes;
  it is not evidence of an eight-hour endurance test.

Recovery remains app-only dev.3 or the verified official app at 0x80000.
Original full-flash backup is preserved; no full restore rehearsal was performed.

## dev.9 protocol boundary verification

The `frame.read` handler now returns `ok=false` for an invalid range or an
unavailable frame. The previous dev.8 response had an error string but
`ok=true`, which could cause a host to accept a failed read. Dev.9 build,
app-only write, independent readback, 8 offline tests, the automatic hardware
probe, three reconnect sessions, negative protocol regression and a two-minute
soak all passed. Evidence is in `artifacts/dev9/` and
`artifacts/dev9-protocol-negative-20260916.jsonl`.
