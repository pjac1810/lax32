/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for LAX-32: the demo song, every timbre, the rhythm part and the reverb modes into build/render-out,
 * the CPU it takes, then a fuzz of random MIDI and settings, under UBSan.   tests/run.sh */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "la.h"

int32_t sine(uint32_t phase) {
  int32_t x = (int32_t)phase >> 16, ax = x < 0 ? -x : x, y, ay;
  y = (x * (32768 - ax)) >> 13;
  ay = y < 0 ? -y : y;
  y += ((((y * ay) >> 15) - y) * 7373) >> 15;
  return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static const uint32_t INC_C9[12] = {815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
                                    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
uint32_t note_inc(int note) {
  if (note < 0) note = 0;
  if (note > 127) note = 127;
  return INC_C9[note % 12] >> (10 - note / 12);
}
static uint32_t rs = 0x1234567u;
uint32_t random32(void) {
  rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5;
  return rs;
}

#define MAXF (44100 * 90)
static int16_t buf[MAXF * 2];
static uint32_t pos;
static int fail, maxpart;

static void render(uint32_t frames) {
  static int32_t blk[512];
  while (frames) {
    uint32_t n = frames > 256 ? 256 : frames;
    memset(blk, 0, sizeof blk);
    demo_tick(n);
    la_render(blk, n);
    if (la_partials_used > maxpart) maxpart = la_partials_used;
    for (uint32_t i = 0; i < 2 * n; i++) {
      if (blk[i] > 32767 || blk[i] < -32767) fail = 1;
      if (pos * 2 + i < MAXF * 2) buf[pos * 2 + i] = (int16_t)blk[i];
    }
    pos += n, frames -= n;
  }
}
static void save(const char *dir, const char *name) {
  char path[600];
  snprintf(path, sizeof path, "%s/%s.wav", dir, name);
  FILE *f = fopen(path, "wb");
  uint32_t frames = pos < MAXF ? pos : MAXF, data = frames * 4, riff = 36 + data, rate = 44100, br = rate * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, al = 4, bits = 16;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&br, 4, 1, f);
  fwrite(&al, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
  fwrite(buf, 2, frames * 2, f);
  fclose(f);
  double sq = 0;
  int peak = 0, clip = 0;
  for (uint32_t i = 0; i < frames * 2; i++) {
    int v = abs(buf[i]);
    sq += (double)v * v, peak = v > peak ? v : peak, clip += v >= 32700;
  }
  printf("%-12s %5.1f s  peak %6d rms %6.0f clip %.3f%%\n", name, frames / 44100.0, peak, sqrt(sq / (frames * 2)), 100.0 * clip / (frames * 2));
  pos = 0;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  la_init();
  /* the demo, timed */
  demo_start(1);
  clock_t t0 = clock();
  render(64);
  while (demo_playing && pos < MAXF) render(256);
  render(44100 * 2);
  double ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
  printf("demo: %.0f ms CPU for %.1f s, up to %d partials  ", ms, pos / 44100.0, maxpart);
  save(dir, "1-demo");
  /* every timbre: a C major arpeggio on part 1 (channel 2) */
  for (int p = 0; p < 128; p++) {
    la_midi(0xC1, (uint8_t)p, 0);
    static const uint8_t N[3] = {60, 64, 67};
    for (int i = 0; i < 3; i++) la_midi(0x91, N[i], 100), render(4410);
    render(13230);
    for (int i = 0; i < 3; i++) la_midi(0x81, N[i], 0);
    render(4410);
    if (p == 63) save(dir, "2-timbres-1-64");
  }
  save(dir, "3-timbres-65-128");
  /* the rhythm part, every key */
  for (int n = 35; n <= 75; n++) la_midi(0x99, (uint8_t)n, 110), render(9000);
  save(dir, "4-rhythm");
  /* the reverb modes: a staccato chord in each */
  la_midi(0xC1, 0, 0);
  for (int m = 0; m < 4; m++) {
    SYS.rev_mode = (uint8_t)m, SYS.rev_time = 6, SYS.rev_level = 6;
    la_midi(0x91, 60, 110), la_midi(0x91, 67, 110), render(4410), la_midi(0x81, 60, 0), la_midi(0x81, 67, 0), render(88200);
  }
  save(dir, "5-reverbs");
  /* pitch bend (12 semitones up) and the mod wheel */
  la_midi(0xC1, 72, 0), la_midi(0x91, 69, 100), render(22050);
  for (int b = 8192; b <= 16383; b += 512) la_midi(0xE1, (uint8_t)(b & 127), (uint8_t)(b >> 7)), render(1500);
  la_midi(0xB1, 1, 127), render(44100), la_midi(0x81, 69, 0), la_midi(0xE1, 0, 64), la_midi(0xB1, 1, 0), render(22050);
  save(dir, "6-bend-mod");
  /* fuzz */
  srand(11);
  for (int b = 0; b < 40000; b++) {
    int r = rand() % 12;
    uint8_t st = (uint8_t)((0x80 + 16 * (rand() % 7)) | (rand() % 16));
    if (r < 8) la_midi(st, (uint8_t)(rand() % 128), (uint8_t)(rand() % 128));
    else if (r == 8) SYS.rev_mode = (uint8_t)(rand() % 4), SYS.rev_time = (uint8_t)(rand() % 8), SYS.rev_level = (uint8_t)(rand() % 8);
    else if (r == 9) SYS.chan_low = (uint8_t)(rand() & 1), SYS.bend_range = (uint8_t)(rand() % 25), SYS.master_tune = (int8_t)(rand() % 101 - 50);
    else if (r == 10) PART[rand() % PARTS].keyshift = (int8_t)(rand() % 49 - 24), PART[rand() % PARTS].fine = (int8_t)(rand() % 101 - 50);
    else if (rand() % 50 == 0) demo_start(rand() & 1);
    if (rand() % 500 == 0) la_panic();
    render(1 + rand() % 256);
    pos = 0;
  }
  printf("fuzz: 40000 blocks, %s\n", fail ? "FAILED" : "ok");
  return fail;
}
