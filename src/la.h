/* SPDX-License-Identifier: GPL-3.0-only */
/* LAX-32: a multi-timbral LA-style sound module for the M-VAVE FM-1, after the 1987 desktop modules: 8 parts and
 * a rhythm part on MIDI, 128 timbres of one or two partials, 30 rhythm sounds, a reverb in four modes, a 20-character
 * dot-matrix LCD. Shared by the panel (main.c), the sound (la.c) and the demo (demo.c). */
#ifndef LA_H
#define LA_H
#include <stdint.h>

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

#define PARTS 9 /* 0..7 melodic, 8 rhythm */
#define RHY 8

/* ---- a partial: an oscillator (or noise) through a resonant low-pass, with its envelopes */
enum { W_SAW, W_SQR, W_SINE, W_FM, W_NOISE, W_ORGAN };
typedef struct {
  uint8_t wave;
  int8_t coarse, fine;     /* semitones; cents */
  uint8_t pw;              /* SQR: 64 = 50 % */
  uint8_t fmr, fmi, fmd;   /* FM: ratio x 2, index (x 1/16), its decay (time) */
  uint8_t cut, res;        /* 0..127 */
  int8_t fenv;             /* filter envelope amount */
  uint8_t fa, fd;          /* its attack and decay (time 0..127: 1 ms .. 8 s) */
  uint8_t a, d, s, r;      /* loudness */
  int8_t penv;             /* semitones at the start, falling to 0 */
  uint8_t pdec;
  uint8_t level, velsens;  /* 0..127 */
} partial_t;

typedef struct {
  const char *name;
  uint8_t np, ring;        /* partials; 1: the second is ring-modulated by the first */
  uint8_t vib, vibrate;    /* built-in vibrato: depth, rate (0..127) */
  partial_t p[2];
} timbre_t;

typedef struct {
  const char *name;
  uint8_t model;
  uint16_t f1, f2, pdec, dec, noise, hp, lvl;
  uint8_t choke;
  int8_t pan;
} rsnd_t;

extern const timbre_t TIMBRE[128];
#define GROUPS 16
extern const char *const GROUP_NAME[GROUPS];
extern const uint8_t GROUP_FIRST[GROUPS];
extern const rsnd_t RHYTHM[41]; /* notes 35..75 */
int timbre_group(int prog);

/* ---- the module's state (the panel reads it; la.c owns it) */
typedef struct {
  uint8_t prog, vol, pan, mod, sustain, revsw; /* vol/pan/mod as MIDI 0..127 */
  int8_t keyshift, fine;                        /* semitones -24..24, cents -50..50 */
  int16_t bend;                                 /* -8192..8191 */
  uint8_t active;                               /* notes sounding (the LCD's blocks) */
} part_t;
extern volatile part_t PART[PARTS];

typedef struct {
  uint8_t rev_mode, rev_time, rev_level; /* 0 ROOM, 1 HALL, 2 PLATE, 3 TAP DELAY; 0..7; 0..7 */
  uint8_t chan_low;                       /* 0: parts on channels 2-9 (as shipped), 1: 1-8 */
  uint8_t bend_range;                     /* semitones, 12 as shipped */
  int8_t master_tune;                     /* cents */
} sys_t;
extern volatile sys_t SYS;
extern const char *const REV_NAME[4];

/* ---- la.c */
void la_init(void);
void la_midi(uint8_t status, uint8_t d1, uint8_t d2); /* from loop(): queued for audio() */
void la_panic(void);
void la_direct(uint8_t status, uint8_t d1, uint8_t d2); /* audio() only: handled now */
void la_render(int32_t *out, uint32_t frames);
extern volatile uint32_t la_midi_seen;    /* bumped on every message (the MIDI MESSAGE light) */
extern volatile uint8_t la_partials_used; /* partials sounding now, 0..32 */

/* ---- demo.c: a song played on the module's own channels, from audio() */
void demo_start(int on);
void demo_tick(uint32_t frames); /* in audio(), before the sound */
extern volatile uint8_t demo_playing;

#endif
