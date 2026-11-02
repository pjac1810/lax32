/* SPDX-License-Identifier: GPL-3.0-only */
/* LAX-32's demo: an original piece played on the module's own parts, from audio() (tools/gen_demo.py writes it). */
#include "fm1.h"
#include "la.h"
#include "demo.h"

volatile uint8_t demo_playing;
static volatile uint8_t want;
static uint32_t at, acc, wait_ticks;

void demo_start(int on) { want = (uint8_t)(on ? 1 : 2); }

static void send(uint8_t pk, uint8_t d1, uint8_t d2) {
  int part = pk >> 4, kind = pk & 15;
  int ch = part == RHY ? 9 : (SYS.chan_low ? part : part + 1);
  static const uint8_t ST[4] = {0x90, 0x80, 0xC0, 0xB0};
  la_direct((uint8_t)(ST[kind & 3] | ch), d1, d2);
}

static void stop(void) {
  for (int p = 0; p < PARTS; p++) send((uint8_t)(p << 4 | 3), 123, 0);
  demo_playing = 0;
}

void demo_tick(uint32_t frames) {
  if (want == 1) want = 0, at = 0, acc = 0, wait_ticks = DEMO[0].dt, demo_playing = 1;
  if (want == 2) want = 0, stop();
  if (!demo_playing) return;
  /* ticks at 96 a beat: frames x BPM x 96 / (60 x 44100), in 1/65536 */
  acc += frames * (uint32_t)(DEMO_BPM * 96u * 65536u / 2646000u);
  while (acc >= 65536u) {
    acc -= 65536u;
    if (wait_ticks) {
      wait_ticks--;
      continue;
    }
    while (at < DEMO_N) {
      send(DEMO[at].pk, DEMO[at].d1, DEMO[at].d2);
      at++;
      if (at < DEMO_N && DEMO[at].dt) {
        wait_ticks = DEMO[at].dt - 1u;
        break;
      }
    }
    if (at >= DEMO_N) {
      stop();
      return;
    }
  }
}
