#pragma once
/* Experimental Metalio-only display backend. No heap allocation; no hardware globals.
 * The caller serializes all access and provides stable 48,000-byte buffers.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FB_FRAME_BYTES 48000u
#define FB_GRAY_PROFILE "GDEM0397T81-isolated-black-pulse-v4"
#define FB_GRAY_TIMEOUT_MS 10000u
#define FB_GRAY_MAX_FAST 8u
#define FB_GRAY_PHASES 12u
/* Error codes deliberately independent of ESP-IDF; the port translates errors. */
enum { FB_OK=0, FB_ARGUMENT=-1, FB_IO=-2, FB_TIMEOUT=-3, FB_FAULT=-4, FB_LOCKED=-5 };
typedef struct {
 void* context;
 int (*write)(void*,int command,const uint8_t*,size_t);
 int (*wait_idle)(void*,uint32_t timeout_ms);
 uint32_t (*millis)(void*);
 /* Optional board MCU-LUT baseline. With this hook every frame cleans via C4;
  * no OTP F7/FC or differential baseline reuse is allowed. */
 int (*prepare_bw)(void*);
 /* Optional isolated controller entry; called only after BUSY is idle.
  * This resets the display controller, never the MCU, and owns the entire
  * gray transaction. Board B/W state must be restored before normal drawing. */
 int (*begin_gray)(void*);
} fb_bus;
typedef struct {uint8_t sequence;uint32_t elapsed_ms;bool complete;} fb_phase;
typedef struct {
 bool success,grayscale,cleaned,baseline_synced,physical_quality_verified;
 int error;unsigned phases;uint32_t total_ms;fb_phase phase[FB_GRAY_PHASES];
} fb_receipt;
typedef struct {
 bool active,fault,gray_residue,baseline_synced,powered;
 unsigned fast_since_clean;
 fb_receipt last;
} fb_gray_state;
void fb_gray_init(fb_gray_state*);
/* 0=black,85=dark,170=light,255=white; bits: base, LSB/BW, MSB/RED. */
int fb_selector(uint8_t luma,uint8_t* base,uint8_t* lsb,uint8_t* msb);
int fb_encode_planes(const uint8_t* native_luma,size_t n,uint8_t* base,uint8_t* lsb,uint8_t* msb);
/* full=true establishes an absolute quality baseline; fast requests may be promoted
 * to physical cleaning. A BUSY or SPI failure never commits a valid baseline. */
int fb_gray_present(fb_gray_state*,const fb_bus*,const uint8_t* base,
                    const uint8_t* lsb,const uint8_t* msb,bool gray,bool full);
/* Must be called BEFORE normal-driver VRAM writes or sleep after a gray session. */
int fb_gray_restore(fb_gray_state*,const fb_bus*,const uint8_t* last_base);
const uint8_t* fb_gray_lut(size_t* size);
#ifdef __cplusplus
}
#endif
