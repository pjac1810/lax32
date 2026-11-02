/* SPDX-License-Identifier: GPL-3.0-only */
/* LAX-32: a multi-timbral LA-style sound module for the M-VAVE FM-1 (README.md has the map), with an arpeggiator and
 * a six-track step sequencer after Skiff's (sq.h, seq.c). The screen is the module's front: its 20-character backlit
 * dot-matrix LCD, the MIDI MESSAGE light, and below it the parts or, on the sequencer's pages, the six tracks.
 *
 *   MIDI in      parts 1-8 on channels 2-9 (or 1-8), the rhythm part on 10: notes, program change, volume (7),
 *                pan (10), modulation (1), sustain (64), pitch bend (12 semitones), all notes off, reset; start, stop
 *   SELECT       the part 1..8, R (on the sequencer's pages: the track 1..6)
 *   ALGORITHM    the part's sound 1..128      PRESETS  the sound group (sequencer: the step)
 *   keys         play the part (R: the rhythm sounds), through its arpeggiator      OCT- OCT+  the keys' octave
 *   HOME  PART: VOLUME, PAN, KEY SHIFT, FINE TUNE      FX  REVERB: MODE, TIME, LEVEL, PART ON/OFF
 *   ENV   SYSTEM: BEND RANGE, MASTER TUNE, CHANNELS, DEMO
 *   ARP   ARP: MODE, RATE, OCTAVES, GATE; again ARP 2: SWING, CHANCE, HOLD, ORDER
 *   SEQ   STEP: PITCH, OCTAVE, LENGTH, VELOCITY; again FEEL: CHANCE/CONDITION, RATCHET/FLAM, NUDGE, ACCENT;
 *         again TRACK: PART, LENGTH, STEP SIZE, DIRECTION
 *   GLO   GLOBAL: TEMPO, SWING, PATTERN A-D, CLICK
 *   PLAY  the sequencer      REC  record (stopped: step by step; playing: live)      SAVE  keep everything
 *   EDIT  erase the step (held: the track)    SEL  a rest (step recording), FILL (held, playing)    LFO  mute */
#include "fm1.h"
#include "la.h"
#include "sq.h"
#include "font5x8.h"

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static char *put(char *d, const char *s) {
  while (*s) *d++ = *s++;
  *d = 0;
  return d;
}
static char *put_int(char *d, int32_t v) {
  char b[12];
  format_int(b, v);
  return put(d, b);
}
static char *put_2(char *d, int v) { /* 05 */
  *d++ = (char)('0' + v / 10 % 10), *d++ = (char)('0' + v % 10), *d = 0;
  return d;
}
static char *put_pad(char *d, const char *s, int w) { /* s, cut or padded to w */
  int i = 0;
  for (; s[i] && i < w; i++) *d++ = s[i];
  for (; i < w; i++) *d++ = ' ';
  *d = 0;
  return d;
}
static char *put_note(char *d, int n) { /* C#4 */
  static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return put_int(put(d, NN[n % 12]), n / 12 - 1);
}
static int str_eq(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return *a == *b;
}

/* ---------------------------------------------------------------- colours: a black case, a yellow-green LCD */

#define C_CASE RGB(22, 22, 24)
#define C_PANEL RGB(36, 36, 40)
#define C_TEXT RGB(225, 225, 230)
#define C_DIM RGB(120, 120, 128)
#define C_FAINT RGB(60, 60, 66)
#define C_LCD_BG RGB(150, 186, 44)
#define C_LCD_OFF RGB(138, 172, 42)
#define C_LCD_ON RGB(28, 44, 16)
#define C_LED RGB(255, 40, 30)
#define C_LED_OFF RGB(70, 18, 16)
#define C_SEL RGB(240, 150, 40)
#define C_STEP RGB(150, 186, 44)
#define C_STEP_ACC RGB(214, 240, 90)
#define C_STEP_MAYBE RGB(84, 110, 30)
#define C_STEP_MUTE RGB(70, 80, 50)

/* ---------------------------------------------------------------- the LCD: 20 characters of 5 x 8 dots */

#define LCD_X 10
#define LCD_Y 30
#define DOT_W 2
#define DOT_H 3
#define CH_W 11
static char lcd_shown[21];

static void lcd_char(int i, char c) {
  int x0 = LCD_X + i * CH_W;
  unsigned u = (unsigned char)c;
  const uint8_t *g = FONT58[(u < 32 || u > 127 ? '?' : u) - 32];
  for (int r = 0; r < 8; r++)
    for (int k = 0; k < 5; k++) rect(x0 + k * DOT_W, LCD_Y + r * DOT_H, DOT_W, DOT_H - 1, (g[r] >> (4 - k) & 1u) ? C_LCD_ON : C_LCD_OFF);
}
static void lcd_frame(void) {
  rect(0, 18, SCREEN_W, 50, C_CASE);
  rect(4, 22, SCREEN_W - 8, 40, RGB(10, 10, 10)); /* the window */
  rect(6, 24, SCREEN_W - 12, 36, C_LCD_BG);       /* lit */
  for (int i = 0; i < 20; i++) lcd_shown[i] = 0;
}
static void lcd_show(const char *s) {
  int end = 0;
  for (int i = 0; i < 20; i++) {
    if (!s[i]) end = 1;
    char c = end ? ' ' : s[i];
    if (c != lcd_shown[i]) lcd_shown[i] = c, lcd_char(i, c);
  }
}

/* ---------------------------------------------------------------- the panel's state */

enum { PG_PART, PG_REVERB, PG_SYSTEM, PG_ARP, PG_ARP2, PG_STEP, PG_FEEL, PG_TRACK, PG_GLOBAL, PG_COUNT };
static const char *const PAGE_NAME[PG_COUNT] = {"PART", "REVERB", "SYSTEM", "ARP", "ARP 2", "STEP", "FEEL", "TRACK", "GLOBAL"};
static const char *const PAGE_KNOBS[PG_COUNT] = { /* the knobs, on the LCD's 20 characters */
    "PART vol pan key fin", "REVERB mode time lvl", "SYSTEM bnd tun ch dm", "ARP mode rate oct gt", "ARP2 swg chc hld ord",
    "STEP pch oct len vel", "FEEL chc rat nud acc", "TRACK prt len sz dir", "GLOBAL bpm sw ptn ck"};
static const uint8_t PAGE_BTN[PG_COUNT] = {BTN_HOME, BTN_FX, BTN_ENV, BTN_ARP, BTN_ARP, BTN_SEQ, BTN_SEQ, BTN_SEQ, BTN_GLO};
static uint8_t part, page, trk, cur, rec;
static int8_t oct;
static uint8_t key_note[KEY_COUNT], key_part[KEY_COUNT], keys_down, srec_gesture, default_note = 60;
static char msg[24];
static uint32_t msg_until, saved_t, midi_light_t, midi_seen_last, cur_moved_t, edit_t;
static uint8_t edit_cleared;

static int seq_view(void) { return page >= PG_STEP; }
static int part_channel(int p) { return p == RHY ? 9 : (SYS.chan_low ? p : p + 1); }
static char part_char(int p) { return p == RHY ? 'R' : (char)('1' + p); }
static int tlen(int t) { return clampi(TLEN[sq_pat][t], 1, STEPS); }

static void lcd_msg(const char *s, uint32_t now) {
  put_pad(msg, s, 20);
  msg_until = now + 1600;
}

static void show_sound(uint32_t now) { /* "1|PIANO  AcouPiano1" */
  char b[24], *d = b;
  *d++ = part_char(part), *d++ = '|';
  if (part == RHY) put(d, "RHYTHM  30 sounds");
  else {
    int pg = PART[part].prog & 127;
    d = put_pad(d, GROUP_NAME[timbre_group(pg)], 6);
    *d++ = ' ';
    put_pad(d, TIMBRE[pg].name, 10);
  }
  lcd_msg(b, now);
}

static char *put_len(char *d, int len) { /* a step's length */
  if (len <= 15) return put(put_int(d, len * 100 / 16), "%");
  if (len == 16) return put(d, "1 step");
  return put(put_int(d, len - 15), " steps");
}
static int chance_index(const step_t *s) { return s->cond ? 99 + s->cond : 100 - clampi(s->chance, 1, 100); }

static void target(void) { sq_cmd(OP_TARGET, trk, cur, 0); }

static void knob(int k, int e, uint32_t now) {
  volatile part_t *P = &PART[part];
  volatile arp_t *R = &ARP[part];
  const step_t *st = &STEP[sq_pat][trk][cur];
  int d = e > 0 ? 1 : -1, a = e < 0 ? -e : e, ch = part_channel(part);
  char b[24], *s = b;
  *s++ = part_char(part), *s++ = '|';
  if (seq_view()) b[0] = 'T', b[1] = (char)('1' + trk), b[2] = '|', s = b + 3;
  switch (page * 4 + k) {
    /* ---- PART */
    case PG_PART * 4 + 0: {
      int v = clampi(P->vol + d * (a > 1 ? 4 * a : 1), 0, 127);
      la_midi((uint8_t)(0xB0 | ch), 7, (uint8_t)v);
      put_int(put(s, "vol:"), v * 100 / 127);
      break;
    }
    case PG_PART * 4 + 1: {
      int v = clampi(P->pan + d * (a > 1 ? 4 * a : 2), 0, 127);
      la_midi((uint8_t)(0xB0 | ch), 10, (uint8_t)v);
      s = put(s, "pan:");
      if (v == 64) put(s, "center");
      else put_int(put(s, v < 64 ? "L" : "R"), v < 64 ? (64 - v) * 7 / 64 : (v - 64) * 7 / 63);
      break;
    }
    case PG_PART * 4 + 2: P->keyshift = (int8_t)clampi(P->keyshift + d, -24, 24), put_int(put(s, P->keyshift > 0 ? "key shift:+" : "key shift:"), P->keyshift); break;
    case PG_PART * 4 + 3: P->fine = (int8_t)clampi(P->fine + d * (a > 1 ? 5 * a : 1), -50, 50), put_int(put(s, P->fine > 0 ? "fine tune:+" : "fine tune:"), P->fine); break;
    /* ---- REVERB */
    case PG_REVERB * 4 + 0: SYS.rev_mode = (uint8_t)clampi(SYS.rev_mode + d, 0, 3), put(put(b, "Reverb mode:"), REV_NAME[SYS.rev_mode]); break;
    case PG_REVERB * 4 + 1: SYS.rev_time = (uint8_t)clampi(SYS.rev_time + d, 0, 7), put_int(put(b, "Reverb time:"), SYS.rev_time + 1); break;
    case PG_REVERB * 4 + 2: SYS.rev_level = (uint8_t)clampi(SYS.rev_level + d, 0, 7), put_int(put(b, "Reverb level:"), SYS.rev_level); break;
    case PG_REVERB * 4 + 3: P->revsw = e > 0, put(s, P->revsw ? "reverb:on" : "reverb:off"); break;
    /* ---- SYSTEM */
    case PG_SYSTEM * 4 + 0: SYS.bend_range = (uint8_t)clampi(SYS.bend_range + d, 0, 24), put_int(put(b, "Bend range:"), SYS.bend_range); break;
    case PG_SYSTEM * 4 + 1: {
      SYS.master_tune = (int8_t)clampi(SYS.master_tune + d, -50, 50);
      int hz = 4400 + SYS.master_tune * 44 / 17; /* a cent is about 0.26 Hz at 440 */
      put(put_int(put(put_int(put(b, "Master tune:"), hz / 10), "."), hz % 10), "Hz");
      break;
    }
    case PG_SYSTEM * 4 + 2:
      la_panic();
      SYS.chan_low = e > 0 ? 0 : 1, put(b, SYS.chan_low ? "Channels 1-8, R:10" : "Channels 2-9, R:10");
      break;
    case PG_SYSTEM * 4 + 3:
      demo_start(e > 0);
      if (e < 0) la_panic();
      put(b, e > 0 ? "Demo play" : "Demo stop");
      break;
    /* ---- ARP, ARP 2: the part's arpeggiator */
    case PG_ARP * 4 + 0:
      R->mode = (uint8_t)clampi(R->mode + d, 0, AM_COUNT - 1);
      if (!R->mode) sq_cmd(OP_ARP_STOP, part, 0, 0);
      put(put(s, "Arp mode:"), AM_NAME[R->mode]);
      break;
    case PG_ARP * 4 + 1: R->rate = (uint8_t)clampi(R->rate + d, 0, DIVS - 1), put(put(s, "Arp rate:"), DIV_NAME[R->rate]); break;
    case PG_ARP * 4 + 2: R->oct = (uint8_t)clampi(R->oct + d, 1, 4), put_int(put(s, "Arp octaves:"), R->oct); break;
    case PG_ARP * 4 + 3: R->gate = (uint8_t)clampi(R->gate + d * (a > 1 ? 4 * a : 2), 4, 127), put(put_int(put(s, "Arp gate:"), R->gate * 100 / 128), "%"); break;
    case PG_ARP2 * 4 + 0: R->swing = (uint8_t)clampi(R->swing + d * (a > 1 ? 3 * a : 1), 0, 100), put_int(put(s, "Arp swing:"), R->swing); break;
    case PG_ARP2 * 4 + 1: R->chance = (uint8_t)clampi(R->chance + d * (a > 1 ? 3 * a : 1), 1, 100), put(put_int(put(s, "Arp chance:"), R->chance), "%"); break;
    case PG_ARP2 * 4 + 2: R->hold = e > 0, put(s, R->hold ? "Arp hold:on" : "Arp hold:off"); break;
    case PG_ARP2 * 4 + 3: R->order = e > 0, put(s, R->order ? "Arp order:played" : "Arp order:low-hi"); break;
    /* ---- STEP: the step under the cursor */
    case PG_STEP * 4 + 0:
    case PG_STEP * 4 + 1:
      target();
      sq_cmd(OP_SET, k ? SET_OCT : SET_PITCH, (uint8_t)(64 + d), default_note);
      put(put_2(put(s, "step "), cur + 1), k ? " octave" : " pitch");
      break;
    case PG_STEP * 4 + 2: {
      int v = clampi(st->len + d, 1, LEN_MAX);
      target(), sq_cmd(OP_SET, SET_LEN, (uint8_t)v, 0);
      put_len(put(s, "length:"), v);
      break;
    }
    case PG_STEP * 4 + 3: {
      int v = clampi(st->vel + d * (a > 1 ? 4 * a : 1), 1, 127);
      target(), sq_cmd(OP_SET, SET_VEL, (uint8_t)v, 0);
      put_int(put(s, "velocity:"), v);
      break;
    }
    /* ---- FEEL */
    case PG_FEEL * 4 + 0: {
      int i = clampi(chance_index(st) + d * (a > 1 ? 3 * a : 1), 0, 99 + TC_COUNT - 1);
      int chance = i < 100 ? 100 - i : 100, cond = i < 100 ? 0 : i - 99;
      target(), sq_cmd(OP_SET, SET_CHANCE, (uint8_t)chance, (uint8_t)cond);
      if (cond) put(put(s, "condition:"), TC_NAME[cond]);
      else put(put_int(put(s, "chance:"), chance), "%");
      break;
    }
    case PG_FEEL * 4 + 1: {
      int v = clampi(((st->flags & SF_FLAM) ? 5 : (st->flags & SF_RATCH) + 1) + d, 1, 5);
      target(), sq_cmd(OP_SET, SET_RATCH, (uint8_t)v, 0);
      if (v == 5) put(s, "flam");
      else put_int(put(s, "ratchet:x"), v);
      break;
    }
    case PG_FEEL * 4 + 2: {
      int v = clampi(st->nudge + d, -32, 31);
      target(), sq_cmd(OP_SET, SET_NUDGE, (uint8_t)(v + 32), 0);
      put(put_int(put(s, v > 0 ? "nudge:+" : "nudge:"), v), "/64");
      break;
    }
    case PG_FEEL * 4 + 3: target(), sq_cmd(OP_SET, SET_ACCENT, e > 0, 0), put(s, e > 0 ? "accent:on" : "accent:off"); break;
    /* ---- TRACK */
    case PG_TRACK * 4 + 0: {
      int p = clampi(TRK[trk].part + d, 0, PARTS - 1);
      TRK[trk].part = (uint8_t)p, part = (uint8_t)p;
      s = put(put(s, "part "), p == RHY ? "R " : "");
      if (p == RHY) put(s, "Rhythm");
      else *s++ = part_char(p), *s++ = ' ', put(s, TIMBRE[PART[p].prog & 127].name);
      break;
    }
    case PG_TRACK * 4 + 1: {
      int v = clampi(tlen(trk) + d, 1, STEPS);
      TLEN[sq_pat][trk] = (uint8_t)v;
      if (cur >= v) cur = (uint8_t)(v - 1);
      put_int(put(s, "length:"), v);
      break;
    }
    case PG_TRACK * 4 + 2: TRK[trk].div = (uint8_t)clampi(TRK[trk].div + d, 0, DIVS - 1), put(put(s, "step:"), DIV_NAME[TRK[trk].div]); break;
    case PG_TRACK * 4 + 3: TRK[trk].dir = (uint8_t)clampi(TRK[trk].dir + d, 0, 3), put(put(s, "direction:"), DIR_NAME[TRK[trk].dir]); break;
    /* ---- GLOBAL */
    case PG_GLOBAL * 4 + 0: GLOB.bpm = (uint8_t)clampi(GLOB.bpm + d * (a > 1 ? 4 * a : 1), 40, 240), put_int(put(b, "Tempo:"), GLOB.bpm); break;
    case PG_GLOBAL * 4 + 1: GLOB.swing = (uint8_t)clampi(GLOB.swing + d * (a > 1 ? 3 * a : 1), 0, 100), put_int(put(b, "Swing:"), GLOB.swing); break;
    case PG_GLOBAL * 4 + 2:
      GLOB.pattern = (uint8_t)clampi(GLOB.pattern + d, 0, PATTERNS - 1);
      s = put(b, "Pattern ");
      *s++ = (char)('A' + GLOB.pattern), *s = 0;
      if (sq_playing && GLOB.pattern != sq_pat) put(s, " next bar");
      break;
    case PG_GLOBAL * 4 + 3: {
      static const char *const CN[3] = {"off", "rec", "on"};
      GLOB.click = (uint8_t)clampi(GLOB.click + d, 0, 2), put(put(b, "Click:"), CN[GLOB.click]);
      break;
    }
    default: return;
  }
  lcd_msg(b, now);
}

/* ---------------------------------------------------------------- the rest of the front: the parts */

#define LIST_Y 74
#define ROW_H 16
#define FOOT_Y (LIST_Y + 9 * ROW_H + 2)

static void draw_title(void) {
  rect(0, 0, SCREEN_W, 18, C_CASE);
  text(6, 1, "LAX-32", C_TEXT, FONT_SMALL);
  text(SCREEN_W - 6 - text_width("LA SOUND MODULE", FONT_SMALL), 1, "LA SOUND MODULE", C_DIM, FONT_SMALL);
}
static void draw_row(int p, int lit) {
  int y = LIST_Y + p * ROW_H;
  int sel = p == part;
  rect(0, y, SCREEN_W, ROW_H, sel ? C_PANEL : C_CASE);
  char b[24];
  b[0] = part_char(p), b[1] = 0;
  text(4, y, b, sel ? C_SEL : C_DIM, FONT_SMALL);
  int ch = part_channel(p) + 1;
  put_int(b, ch);
  text(18, y, b, C_FAINT, FONT_SMALL);
  text(42, y, p == RHY ? "Rhythm" : TIMBRE[PART[p].prog & 127].name, sel ? C_TEXT : C_DIM, FONT_SMALL);
  int v = PART[p].vol * 50 / 127;
  rect(130, y + 6, 50, 4, C_FAINT), rect(130, y + 6, v, 4, sel ? C_SEL : C_DIM);
  rect(190, y + 3, 10, 10, lit ? RGB(120, 230, 90) : C_FAINT);
  if (ARP[p].mode) text(206, y, "ar", sel ? C_SEL : C_DIM, FONT_SMALL);
  else if (SYS.rev_level && PART[p].revsw) text(206, y, "rv", C_FAINT, FONT_SMALL);
}
static void draw_foot(uint32_t now) {
  rect(0, FOOT_Y, SCREEN_W, SCREEN_H - FOOT_Y, C_CASE);
  char b[40], *d = put(put(b, PAGE_NAME[page]), " ");
  text(4, FOOT_Y + 4, b, C_SEL, FONT_SMALL);
  int x = 4 + text_width(b, FONT_SMALL);
  if (sq_playing) x = text(x, FOOT_Y + 4, "PLAY ", RGB(120, 230, 90), FONT_SMALL);
  if (rec) x = text(x, FOOT_Y + 4, "REC ", C_LED, FONT_SMALL);
  d = b;
  *d++ = (char)('A' + sq_pat);
  if (GLOB.pattern != sq_pat) *d++ = '>', *d++ = (char)('A' + GLOB.pattern);
  *d++ = ' ', *d = 0;
  d = put(put_int(d, GLOB.bpm), demo_playing ? " DEMO" : "");
  x = text(x, FOOT_Y + 4, b, C_DIM, FONT_SMALL);
  if (now - saved_t < 1500 && saved_t) text(x + 4, FOOT_Y + 4, "SAVED", C_TEXT, FONT_SMALL);
  text(SCREEN_W - 6 - text_width("MIDI", FONT_SMALL) - 16, FOOT_Y + 4, "MIDI", C_DIM, FONT_SMALL);
}
static void draw_midi_led(int on) { rect(SCREEN_W - 16, FOOT_Y + 8, 10, 8, on ? C_LED : C_LED_OFF); }

/* ---------------------------------------------------------------- the sequencer's front: six tracks of 16 steps */

#define GRID_X 24
#define CELL 13
#define INFO_Y (LIST_Y + TRACKS * ROW_H + 2)
static uint8_t cell_shown[TRACKS][16], label_shown[TRACKS];
static char info_shown[3][32];

static int cell_x(int i) { return GRID_X + i * CELL + i / 4; }
static int view_page(uint32_t now) { /* the 16 steps shown: the cursor's, or the playhead's while it plays untouched */
  if (sq_playing && now - cur_moved_t > 2500) return sq_pos[trk] / 16;
  return cur / 16;
}
static void draw_label(int t) {
  int y = LIST_Y + t * ROW_H, sel = t == trk;
  char b[4] = {(char)('1' + t), 0, 0, 0};
  rect(0, y, GRID_X - 1, ROW_H, C_CASE);
  text(2, y, b, sel ? C_SEL : TRK[t].mute ? C_FAINT : C_DIM, FONT_SMALL);
  b[0] = TRK[t].mute ? 'M' : part_char(TRK[t].part);
  text(12, y, b, TRK[t].mute ? C_LED : C_FAINT, FONT_SMALL);
}
static void draw_cell(int t, int i, uint8_t code) {
  int x = cell_x(i), y = LIST_Y + t * ROW_H + 2;
  static const uint16_t COL[7] = {C_CASE, C_PANEL, C_STEP, C_STEP_ACC, C_STEP_MAYBE, C_STEP_MUTE, C_TEXT};
  uint16_t c = COL[code & 7];
  if (code & 16) {
    rect(x, y, CELL - 1, ROW_H - 4, C_SEL);
    rect(x + 2, y + 2, CELL - 5, ROW_H - 8, c);
  } else rect(x, y, CELL - 1, ROW_H - 4, c);
  if ((code & 8) && (code & 7) != 6) rect(x + 3, y + ROW_H - 7, CELL - 7, 2, C_TEXT); /* the playhead under a note */
}
static void draw_info(int line, const char *s) {
  if (str_eq(s, info_shown[line])) return;
  put_pad(info_shown[line], s, 30);
  rect(0, INFO_Y + line * 16, SCREEN_W, 16, C_CASE);
  text(4, INFO_Y + line * 16, s, line ? C_DIM : C_TEXT, FONT_SMALL);
  put(info_shown[line], s);
}
static void seq_screen(uint32_t now, int all) {
  int pg = view_page(now);
  if (all)
    for (int t = 0; t < TRACKS; t++) {
      label_shown[t] = 255;
      for (int i = 0; i < 16; i++) cell_shown[t][i] = 255;
      for (int l = 0; l < 3; l++) info_shown[l][0] = 1, info_shown[l][1] = 0;
    }
  for (int t = 0; t < TRACKS; t++) {
    uint8_t lb = (uint8_t)((t == trk) | TRK[t].mute << 1 | TRK[t].part << 2);
    if (lb != label_shown[t]) label_shown[t] = lb, draw_label(t);
    int len = tlen(t);
    for (int i = 0; i < 16; i++) {
      int s = pg * 16 + i;
      const step_t *st = &STEP[sq_pat][t][s];
      uint8_t code;
      if (s >= len) code = 0;
      else if (!st->n) code = 1;
      else if (TRK[t].mute) code = 5;
      else if (st->flags & SF_ACCENT) code = 3;
      else if (st->chance < 100 || st->cond) code = 4;
      else code = 2;
      if (sq_playing && sq_pos[t] == s && s < len) code = (uint8_t)(code >= 2 ? code | 8 : 6);
      if (t == trk && s == cur) code |= 16;
      if (code != cell_shown[t][i]) cell_shown[t][i] = code, draw_cell(t, i, code);
    }
  }
  /* the lines under the grid: the step, or the track, or the whole */
  char l0[40], l1[40], l2[40], *d;
  const step_t *st = &STEP[sq_pat][trk][cur];
  int len = tlen(trk);
  if (page == PG_TRACK) {
    d = put(put(put(l0, "TRACK "), (char[2]){(char)('1' + trk), 0}), "  PART ");
    if (TRK[trk].part == RHY) put(d, "R Rhythm");
    else put(put(put(d, (char[2]){part_char(TRK[trk].part), 0}), " "), TIMBRE[PART[TRK[trk].part].prog & 127].name);
    put(put(put(put(put_int(put(l1, "LENGTH "), len), "  STEP "), DIV_NAME[TRK[trk].div]), "  "), DIR_NAME[TRK[trk].dir]);
    put(put_int(put(put_int(put(l2, "PAGE "), view_page(now) + 1), "/"), (len + 15) / 16), TRK[trk].mute ? "  MUTED (LFO)" : "");
  } else if (page == PG_GLOBAL) {
    put(put_int(put(put_int(put(l0, "TEMPO "), GLOB.bpm), "  SWING "), GLOB.swing), "");
    static const char *const CN[3] = {"OFF", "REC", "ON"};
    d = put(l1, "PATTERN ");
    *d++ = (char)('A' + sq_pat), *d = 0;
    if (GLOB.pattern != sq_pat) *d++ = '>', *d++ = (char)('A' + GLOB.pattern), *d = 0;
    put(put(d, "  CLICK "), CN[GLOB.click]);
    put(l2, sq_fill ? "FILL" : "hold SEL: FILL");
  } else {
    d = put(put_2(put(l0, "STEP "), cur + 1), "  ");
    if (!st->n) put(d, "---");
    else if (TRK[trk].part == RHY) {
      int n = st->note[0];
      d = put(d, n >= 35 && n <= 75 && RHYTHM[n - 35].name ? RHYTHM[n - 35].name : "?");
      if (st->n > 1) put_int(put(d, " +"), st->n - 1);
    } else
      for (int k = 0; k < st->n; k++) d = put(put_note(d, st->note[k]), " ");
    if (!st->n) l1[0] = l2[0] = 0;
    else {
      put(put_int(put(put_len(put(l1, "LEN "), st->len), "  VEL "), st->vel), (st->flags & SF_ACCENT) ? "  ACC" : "");
      d = st->cond ? put(put(l2, "IF "), TC_NAME[st->cond]) : put(put_int(put(l2, ""), st->chance), "%");
      d = (st->flags & SF_FLAM) ? put(d, "  FLAM") : put_int(put(d, "  x"), (st->flags & SF_RATCH) + 1);
      put(put_int(put(d, st->nudge > 0 ? "  NUDGE +" : "  NUDGE "), st->nudge), "");
    }
  }
  draw_info(0, l0), draw_info(1, l1), draw_info(2, l2);
}

/* ---------------------------------------------------------------- keeping it: the setup, then the steps that hold notes */

#define STORE_MAGIC_V1 0x4C415831u /* "LAX1": 0.1, the setup alone */
#define STORE_MAGIC 0x4C415832u    /* "LAX2" */
typedef struct {
  uint32_t magic;
  uint8_t prog[8], vol[PARTS], pan[PARTS], revsw[PARTS];
  int8_t keyshift[PARTS], fine[PARTS];
  uint8_t rev_mode, rev_time, rev_level, chan_low, bend_range;
  int8_t master_tune;
} store1_t;
typedef struct {
  store1_t s;
  uint8_t trk[TRACKS][4], arp[PARTS][8], glob[4], tlen[PATTERNS][TRACKS];
  uint16_t nsteps;
} store_t;
typedef struct {
  uint16_t at; /* pattern << 9 | track << 6 | step */
  uint8_t b[sizeof(step_t)];
} srec_t;
#define SREC_MAX ((SETTINGS_MAX - sizeof(store_t)) / sizeof(srec_t))
static union {
  store_t st;
  uint8_t raw[SETTINGS_MAX];
} sv POOL;

static void load_setup(const store1_t *st) {
  for (int p = 0; p < PARTS; p++) {
    if (p < 8) PART[p].prog = st->prog[p] & 127;
    PART[p].vol = st->vol[p] & 127, PART[p].pan = st->pan[p] & 127, PART[p].revsw = st->revsw[p] & 1;
    PART[p].keyshift = (int8_t)clampi(st->keyshift[p], -24, 24), PART[p].fine = (int8_t)clampi(st->fine[p], -50, 50);
  }
  SYS.rev_mode = st->rev_mode & 3, SYS.rev_time = st->rev_time & 7, SYS.rev_level = st->rev_level & 7;
  SYS.chan_low = st->chan_low & 1, SYS.bend_range = (uint8_t)clampi(st->bend_range, 0, 24);
  SYS.master_tune = (int8_t)clampi(st->master_tune, -50, 50);
}

static void load_all(void) {
  if (!settings_load(sv.raw, SETTINGS_MAX) && sv.st.s.magic == STORE_MAGIC) {
    load_setup(&sv.st.s);
    for (int t = 0; t < TRACKS; t++)
      TRK[t] = (track_t){(uint8_t)(sv.st.trk[t][0] % PARTS), (uint8_t)(sv.st.trk[t][1] % DIVS), (uint8_t)(sv.st.trk[t][2] & 3), (uint8_t)(sv.st.trk[t][3] & 1)};
    for (int p = 0; p < PARTS; p++) {
      const uint8_t *a = sv.st.arp[p];
      ARP[p] = (arp_t){(uint8_t)(a[0] % AM_COUNT), (uint8_t)(a[1] % DIVS), (uint8_t)clampi(a[2], 1, 4), (uint8_t)clampi(a[3], 4, 127),
                       (uint8_t)clampi(a[4], 0, 100), (uint8_t)clampi(a[5], 1, 100), (uint8_t)(a[6] & 1), (uint8_t)(a[7] & 1)};
    }
    GLOB.bpm = (uint8_t)clampi(sv.st.glob[0], 40, 240), GLOB.swing = (uint8_t)clampi(sv.st.glob[1], 0, 100);
    GLOB.click = (uint8_t)clampi(sv.st.glob[2], 0, 2), GLOB.pattern = sv.st.glob[3] & 3;
    for (int p = 0; p < PATTERNS; p++)
      for (int t = 0; t < TRACKS; t++) {
        TLEN[p][t] = (uint8_t)clampi(sv.st.tlen[p][t], 1, STEPS);
        for (int s = 0; s < STEPS; s++) STEP[p][t][s].n = 0;
      }
    const srec_t *r = (const srec_t *)(sv.raw + sizeof(store_t));
    for (uint32_t i = 0; i < sv.st.nsteps && i < SREC_MAX; i++) {
      step_t *s = &STEP[r[i].at >> 9 & 3][(r[i].at >> 6 & 7) % TRACKS][r[i].at & 63];
      const uint8_t *b = r[i].b;
      *s = (step_t){{b[0] & 127, b[1] & 127, b[2] & 127, b[3] & 127}, (uint8_t)(b[4] > 4 ? 4 : b[4]), (uint8_t)clampi(b[5], 1, 127),
                    (uint8_t)clampi(b[6], 1, LEN_MAX), (uint8_t)clampi(b[7], 1, 100), (uint8_t)(b[8] < TC_COUNT ? b[8] : 0),
                    (uint8_t)(b[9] & 15), (int8_t)clampi((int8_t)b[10], -32, 31), 0};
    }
  } else if (!settings_load(sv.raw, sizeof(store1_t)) && sv.st.s.magic == STORE_MAGIC_V1) {
    load_setup(&sv.st.s);
  }
}

static int save_all(void) { /* the steps that did not fit (0: all kept) */
  store_t *st = &sv.st;
  st->s.magic = STORE_MAGIC;
  for (int p = 0; p < PARTS; p++) {
    if (p < 8) st->s.prog[p] = PART[p].prog;
    st->s.vol[p] = PART[p].vol, st->s.pan[p] = PART[p].pan, st->s.revsw[p] = PART[p].revsw;
    st->s.keyshift[p] = PART[p].keyshift, st->s.fine[p] = PART[p].fine;
    const volatile arp_t *a = &ARP[p];
    uint8_t *b = st->arp[p];
    b[0] = a->mode, b[1] = a->rate, b[2] = a->oct, b[3] = a->gate, b[4] = a->swing, b[5] = a->chance, b[6] = a->hold, b[7] = a->order;
  }
  st->s.rev_mode = SYS.rev_mode, st->s.rev_time = SYS.rev_time, st->s.rev_level = SYS.rev_level;
  st->s.chan_low = SYS.chan_low, st->s.bend_range = SYS.bend_range, st->s.master_tune = SYS.master_tune;
  for (int t = 0; t < TRACKS; t++) st->trk[t][0] = TRK[t].part, st->trk[t][1] = TRK[t].div, st->trk[t][2] = TRK[t].dir, st->trk[t][3] = TRK[t].mute;
  st->glob[0] = GLOB.bpm, st->glob[1] = GLOB.swing, st->glob[2] = GLOB.click, st->glob[3] = GLOB.pattern;
  srec_t *r = (srec_t *)(sv.raw + sizeof(store_t));
  uint32_t n = 0;
  int lost = 0;
  for (int p = 0; p < PATTERNS; p++)
    for (int t = 0; t < TRACKS; t++) {
      st->tlen[p][t] = TLEN[p][t];
      for (int s = 0; s < STEPS; s++) {
        const step_t *x = &STEP[p][t][s];
        if (!x->n) continue;
        if (n >= SREC_MAX) {
          lost++;
          continue;
        }
        r[n].at = (uint16_t)(p << 9 | t << 6 | s);
        const uint8_t *b = (const uint8_t *)x;
        for (uint32_t i = 0; i < sizeof(step_t); i++) r[n].b[i] = b[i];
        n++;
      }
    }
  st->nsteps = (uint16_t)n;
  settings_save(sv.raw, SETTINGS_MAX);
  return lost;
}

/* ---------------------------------------------------------------- setup */

void setup(void) {
  la_init();
  sq_init();
  load_all();
  led_backlight(1);
  clear(C_CASE);
  draw_title();
  lcd_frame();
  lcd_msg(" LAX-32  Ver 0.2    ", 0);
  msg_until = 1800;
}

/* ---------------------------------------------------------------- loop */

static uint8_t row_shown[PARTS], row_prog[PARTS], row_vol[PARTS], row_lit[PARTS], row_arp[PARTS];
static uint32_t sh_foot = ~0u, led_b = ~0u, led_k = ~0u, edits_seen;
static int8_t led_shown = -1, view_shown = -1;

static void set_page(int p, uint32_t now) {
  page = (uint8_t)p;
  if (seq_view()) part = TRK[trk].part;
  lcd_msg(PAGE_KNOBS[p], now);
}

static void key_down(int k, uint32_t now) {
  (void)now;
  int n = part == RHY ? 35 + k + (oct > 0 ? oct * 12 : 0) : KEY_FIRST_NOTE + k + oct * 12;
  n = clampi(n, 0, 127);
  key_note[k] = (uint8_t)(n + 1), key_part[k] = part;
  sq_cmd(OP_KEY_ON, part, (uint8_t)n, 100);
  if (seq_view() && rec && !sq_playing) { /* step recording: the keys held together make the step's chord */
    target();
    sq_cmd(OP_SREC, (uint8_t)n, keys_down == 0, 0);
    srec_gesture = 1;
  }
  default_note = (uint8_t)n;
  keys_down++;
}
static void key_up(int k) {
  if (!key_note[k]) return;
  sq_cmd(OP_KEY_OFF, key_part[k], (uint8_t)(key_note[k] - 1), 0);
  key_note[k] = 0;
  if (keys_down) keys_down--;
  if (!keys_down && srec_gesture) srec_gesture = 0, cur = (uint8_t)((cur + 1) % tlen(trk)), cur_moved_t = millis();
}

void loop(void) {
  uint32_t now = millis();
  int32_t e;

  /* ---- buttons */
  if (button_pressed(BTN_PLAY)) {
    sq_cmd(OP_PLAY, !sq_playing, 0, 0);
    lcd_msg(sq_playing ? "Stop" : "Play", now);
    cur_moved_t = 0;
  }
  if (button_pressed(BTN_REC)) {
    rec = !rec;
    sq_cmd(OP_REC, rec, trk, 0);
    if (rec && !seq_view()) set_page(PG_STEP, now);
    lcd_msg(rec ? (sq_playing ? "Record: live" : "Record: step by step") : "Record off", now);
  }
  if (button_pressed(BTN_SAVE)) {
    int lost = save_all();
    saved_t = now ? now : 1;
    char b[24];
    if (lost) put(put_int(put(b, "Full: "), lost), " steps lost");
    else put(b, "All saved");
    lcd_msg(b, now);
  }
  static const uint8_t PLAIN[3] = {BTN_HOME, BTN_FX, BTN_ENV};
  for (int i = 0; i < 3; i++)
    if (button_pressed(PLAIN[i])) set_page(i, now);
  if (button_pressed(BTN_ARP)) set_page(page == PG_ARP ? PG_ARP2 : PG_ARP, now);
  if (button_pressed(BTN_SEQ)) set_page(page == PG_STEP ? PG_FEEL : page == PG_FEEL ? PG_TRACK : PG_STEP, now);
  if (button_pressed(BTN_GLO)) set_page(PG_GLOBAL, now);
  if (button_pressed(BTN_OCT_DOWN) || button_pressed(BTN_OCT_UP)) {
    if (button_held(BTN_OCT_DOWN) && button_held(BTN_OCT_UP)) oct = 0;
    else oct = (int8_t)clampi(oct + (button_pressed(BTN_OCT_UP) ? 1 : -1), -3, 3);
    char b[16];
    put_int(put(b, "Octave "), oct);
    lcd_msg(b, now);
  }
  if (seq_view()) {
    char b[24];
    if (button_pressed(BTN_EDIT)) {
      target(), sq_cmd(OP_CLEAR, 0, 0, 0);
      put(put_2(put(b, "Step "), cur + 1), " erased");
      lcd_msg(b, now);
      edit_t = now, edit_cleared = 0;
    }
    if (button_held(BTN_EDIT) && !edit_cleared && now - edit_t > 1000) {
      target(), sq_cmd(OP_CLEAR, 1, 0, 0), edit_cleared = 1;
      put(put(put(b, "Track "), (char[2]){(char)('1' + trk), 0}), " cleared");
      lcd_msg(b, now);
    }
    if (button_pressed(BTN_SEL) && rec && !sq_playing) { /* a rest */
      cur = (uint8_t)((cur + 1) % tlen(trk)), cur_moved_t = now;
      lcd_msg("Rest", now);
    }
    if (button_pressed(BTN_LFO)) {
      TRK[trk].mute = !TRK[trk].mute;
      put(put(put(b, "Track "), (char[2]){(char)('1' + trk), 0}), TRK[trk].mute ? " muted" : " on");
      lcd_msg(b, now);
    }
  }
  sq_fill = (uint8_t)(seq_view() && sq_playing && button_held(BTN_SEL));

  /* ---- encoders */
  if ((e = encoder(ENC_SELECT))) {
    if (seq_view()) {
      trk = (uint8_t)((trk + TRACKS * 8 + e) % TRACKS), part = TRK[trk].part;
      if (cur >= tlen(trk)) cur = (uint8_t)(tlen(trk) - 1);
      if (rec) sq_cmd(OP_REC, 1, trk, 0);
      char b[24];
      put(put(put(b, "Track "), (char[2]){(char)('1' + trk), 0}), " ");
      put(b + 8, part == RHY ? "Rhythm" : TIMBRE[PART[part].prog & 127].name);
      lcd_msg(b, now);
    } else part = (uint8_t)((part + PARTS * 8 + e) % PARTS), show_sound(now);
  }
  if ((e = encoder(ENC_ALGORITHM)) && part != RHY) {
    la_midi((uint8_t)(0xC0 | part_channel(part)), (uint8_t)((PART[part].prog + 128 * 8 + e) & 127), 0);
    PART[part].prog = (uint8_t)((PART[part].prog + 128 * 8 + e) & 127); /* shown at once; the module agrees */
    show_sound(now);
  }
  if ((e = encoder(ENC_PRESETS))) {
    if (seq_view()) {
      if (sq_playing && now - cur_moved_t > 2500) cur = (uint8_t)(view_page(now) * 16 + cur % 16); /* from the page shown */
      cur = (uint8_t)clampi(cur + e, 0, tlen(trk) - 1), cur_moved_t = now;
    } else if (part != RHY) {
      int g = clampi(timbre_group(PART[part].prog) + (e > 0 ? 1 : -1), 0, GROUPS - 1);
      la_midi((uint8_t)(0xC0 | part_channel(part)), GROUP_FIRST[g], 0);
      PART[part].prog = GROUP_FIRST[g];
      show_sound(now);
    }
  }
  for (int k = 0; k < 4; k++)
    if ((e = encoder(ENC_KNOB1 + k))) knob(k, e, now);

  /* ---- keys: play the part, through its arpeggiator */
  for (int k = 0; k < KEY_COUNT; k++) {
    if (key_pressed(k)) key_down(k, now);
    if (key_released(k)) key_up(k);
  }
  /* ---- MIDI in: notes to the arpeggiators or the track recording, the rest straight to the module */
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2)) {
    if (st == 0xFA || st == 0xFB) sq_cmd(OP_PLAY, 1, 0, 0);
    else if (st == 0xFC) sq_cmd(OP_PLAY, 0, 0, 0);
    if (st < 0x80 || st >= 0xF0) continue;
    int c = st & 15, kind = st & 0xF0, p = c == 9 ? RHY : SYS.chan_low ? c : c - 1;
    if ((kind == 0x90 || kind == 0x80) && p >= 0 && p < PARTS && (ARP[p].mode || (rec && sq_playing && p == TRK[trk].part))) {
      if (kind == 0x90 && d2) sq_cmd(OP_KEY_ON, (uint8_t)p, d1, d2);
      else sq_cmd(OP_KEY_OFF, (uint8_t)p, d1, 0);
      la_midi_seen = la_midi_seen + 1;
    } else la_midi(st, d1, d2);
  }

  /* ---- the MIDI MESSAGE light */
  if (la_midi_seen != midi_seen_last) midi_seen_last = la_midi_seen, midi_light_t = now;
  int led = now - midi_light_t < 60;

  /* ---- LEDs: keys held; the page's button, PLAY, REC, ARP on, LFO for a muted track */
  uint32_t lk = 0, lb = 1u << PAGE_BTN[page];
  for (int k = 0; k < KEY_COUNT; k++)
    if (key_held(k)) lk |= 1u << k;
  if (sq_playing && (!(sq_pos[0] & 3) || (sq_pos[0] & 3) == 1)) lb |= 1u << BTN_PLAY; /* blinks with the beat */
  if (rec && (sq_playing || (now / 400) & 1)) lb |= 1u << BTN_REC;
  if (ARP[part].mode) lb |= 1u << BTN_ARP;
  if (seq_view() && TRK[trk].mute) lb |= 1u << BTN_LFO;
  if (sq_fill) lb |= 1u << BTN_SEL;
  if (lk != led_k) {
    for (int i = 0; i < KEY_COUNT; i++)
      if ((lk ^ led_k) >> i & 1u) led_key(i, lk >> i & 1u);
    led_k = lk;
  }
  if (lb != led_b) {
    for (int i = 0; i < BTN_COUNT; i++)
      if ((lb ^ led_b) >> i & 1u) led_button(i, lb >> i & 1u);
    led_b = lb;
  }

  /* ---- the LCD: a message for a while, else the parts that play and the volume (sequencer: where it is) */
  if (now < msg_until) lcd_show(msg);
  else if (seq_view()) { /* "A 120 T1 05/16 PLAY" */
    char b[28], *d = b;
    *d++ = (char)('A' + sq_pat), *d++ = ' ';
    d = put(put_int(d, GLOB.bpm), GLOB.bpm < 100 ? "  T" : " T");
    *d++ = (char)('1' + trk), *d++ = ' ';
    int pos = sq_playing ? sq_pos[trk] : cur;
    d = put(put_2(put(put_2(d, pos + 1), "/"), tlen(trk)), " ");
    put(d, rec ? (sq_playing ? "REC" : "STEP") : sq_playing ? "PLAY" : "STOP");
    lcd_show(b);
  } else {
    char b[24];
    int vol = (int)(master_knob() * 100u / 1023u);
    for (int p = 0; p < 5; p++) b[2 * p] = PART[p].active ? (char)127 : (char)('1' + p), b[2 * p + 1] = ' ';
    b[10] = PART[RHY].active ? (char)127 : 'R', b[11] = ' ', b[12] = '|';
    char *d = put(b + 13, "vol:");
    if (vol < 100) *d++ = ' ';
    if (vol < 10) *d++ = ' ';
    put_int(d, vol);
    lcd_show(b);
  }

  /* ---- below it: the parts, or the six tracks */
  int sv_now = seq_view();
  int all = sv_now != view_shown;
  if (all) {
    view_shown = (int8_t)sv_now;
    rect(0, LIST_Y, SCREEN_W, FOOT_Y - LIST_Y, C_CASE);
    for (int p = 0; p < PARTS; p++) row_shown[p] = 0;
  }
  if (sq_edits != edits_seen) edits_seen = sq_edits;
  if (sv_now) seq_screen(now, all);
  else
    for (int p = 0; p < PARTS; p++) {
      int lit = PART[p].active > 0;
      uint8_t sel = (uint8_t)(p == part);
      if (row_shown[p] != (uint8_t)(sel + 2) || row_prog[p] != PART[p].prog || row_vol[p] != PART[p].vol || row_lit[p] != lit || row_arp[p] != ARP[p].mode)
        row_shown[p] = (uint8_t)(sel + 2), row_prog[p] = PART[p].prog, row_vol[p] = PART[p].vol, row_lit[p] = (uint8_t)lit, row_arp[p] = ARP[p].mode,
        draw_row(p, lit);
    }
  uint32_t h = (uint32_t)page | (uint32_t)demo_playing << 4 | (uint32_t)(now - saved_t < 1500) << 5 | (uint32_t)SYS.chan_low << 6 |
               (uint32_t)SYS.rev_level << 7 | (uint32_t)sq_playing << 10 | (uint32_t)rec << 11 | (uint32_t)sq_pat << 12 |
               (uint32_t)GLOB.pattern << 14 | (uint32_t)GLOB.bpm << 16;
  if (h != sh_foot) {
    sh_foot = h, draw_foot(now), led_shown = -1;
    for (int p = 0; p < PARTS; p++) row_shown[p] = 0;
  }
  if (led != led_shown) led_shown = (int8_t)led, draw_midi_led(led);
}

/* ---------------------------------------------------------------- the sound: in chunks of 64 frames, so that the
 * sequencer's and the arpeggiator's notes fall within 1.5 ms of their time */

void audio(int32_t *out, uint32_t frames) {
  for (uint32_t i = 0; i < frames; i += 64) {
    uint32_t n = frames - i < 64 ? frames - i : 64;
    demo_tick(n);
    sq_tick(n);
    la_render(out + 2 * i, n);
  }
}
