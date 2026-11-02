/* SPDX-License-Identifier: GPL-3.0-only */
/* LAX-32: a multi-timbral LA-style sound module for the M-VAVE FM-1 (README.md has the map). The screen is the
 * module's front: its 20-character backlit dot-matrix LCD, the MIDI MESSAGE light, and the parts.
 *
 *   MIDI in      parts 1-8 on channels 2-9 (or 1-8), the rhythm part on 10: notes, program change, volume (7),
 *                pan (10), modulation (1), sustain (64), pitch bend (12 semitones), all notes off, reset
 *   SELECT       the part 1..8, R          ALGORITHM  its sound 1..128      PRESETS  the sound group
 *   keys         play the part (R: the rhythm sounds)                     OCT- OCT+  the keys' octave
 *   HOME  PART: VOLUME, PAN, KEY SHIFT, FINE TUNE      FX  REVERB: MODE, TIME, LEVEL, PART ON/OFF
 *   ENV   SYSTEM: BEND RANGE, MASTER TUNE, CHANNELS
 *   PLAY  the demo song        REC  all notes off        SAVE  keep the setup        MASTER  volume */
#include "fm1.h"
#include "la.h"
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
static char *put_pad(char *d, const char *s, int w) { /* s, cut or padded to w */
  int i = 0;
  for (; s[i] && i < w; i++) *d++ = s[i];
  for (; i < w; i++) *d++ = ' ';
  *d = 0;
  return d;
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
  rect(4, 22, SCREEN_W - 8, 40, RGB(10, 10, 10));      /* the window */
  rect(6, 24, SCREEN_W - 12, 36, C_LCD_BG);            /* lit */
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

enum { PG_PART, PG_REVERB, PG_SYSTEM };
static const char *const PAGE_NAME[3] = {"PART", "REVERB", "SYSTEM"};
static uint8_t part, page;
static int8_t oct;
static uint8_t key_note[KEY_COUNT];
static char msg[24];
static uint32_t msg_until, saved_t, midi_light_t, midi_seen_last;

static int part_channel(int p) { return p == RHY ? 9 : (SYS.chan_low ? p : p + 1); }
static char part_char(int p) { return p == RHY ? 'R' : (char)('1' + p); }

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

static void knob(int k, int e, uint32_t now) {
  volatile part_t *P = &PART[part];
  int d = e > 0 ? 1 : -1, a = e < 0 ? -e : e, ch = part_channel(part);
  char b[24], *s = b;
  *s++ = part_char(part), *s++ = '|';
  switch (page * 4 + k) {
    case 0: {
      int v = clampi(P->vol + d * (a > 1 ? 4 * a : 1), 0, 127);
      la_midi((uint8_t)(0xB0 | ch), 7, (uint8_t)v);
      put_int(put(s, "vol:"), v * 100 / 127);
      break;
    }
    case 1: {
      int v = clampi(P->pan + d * (a > 1 ? 4 * a : 2), 0, 127);
      la_midi((uint8_t)(0xB0 | ch), 10, (uint8_t)v);
      s = put(s, "pan:");
      if (v == 64) put(s, "center");
      else put_int(put(s, v < 64 ? "L" : "R"), v < 64 ? (64 - v) * 7 / 64 : (v - 64) * 7 / 63);
      break;
    }
    case 2: P->keyshift = (int8_t)clampi(P->keyshift + d, -24, 24), put_int(put(s, P->keyshift > 0 ? "key shift:+" : "key shift:"), P->keyshift); break;
    case 3: P->fine = (int8_t)clampi(P->fine + d * (a > 1 ? 5 * a : 1), -50, 50), put_int(put(s, P->fine > 0 ? "fine tune:+" : "fine tune:"), P->fine); break;
    case 4: SYS.rev_mode = (uint8_t)clampi(SYS.rev_mode + d, 0, 3), s = b, put(put(s, "Reverb mode:"), REV_NAME[SYS.rev_mode]); break;
    case 5: SYS.rev_time = (uint8_t)clampi(SYS.rev_time + d, 0, 7), s = b, put_int(put(s, "Reverb time:"), SYS.rev_time + 1); break;
    case 6: SYS.rev_level = (uint8_t)clampi(SYS.rev_level + d, 0, 7), s = b, put_int(put(s, "Reverb level:"), SYS.rev_level); break;
    case 7: P->revsw = e > 0, put(s, P->revsw ? "reverb:on" : "reverb:off"); break;
    case 8: SYS.bend_range = (uint8_t)clampi(SYS.bend_range + d, 0, 24), s = b, put_int(put(s, "Bend range:"), SYS.bend_range); break;
    case 9: SYS.master_tune = (int8_t)clampi(SYS.master_tune + d, -50, 50), s = b, put_int(put(s, "Master tune:"), 4400 + SYS.master_tune * 44 / 17); break;
    case 10:
      la_panic();
      SYS.chan_low = e > 0 ? 0 : 1, s = b, put(s, SYS.chan_low ? "Channels 1-8, R:10" : "Channels 2-9, R:10");
      break;
    default: return;
  }
  if (page * 4 + k == 9) { /* 440.0 Hz style */
    char t[24];
    int hz = 4400 + SYS.master_tune * 44 / 17; /* a cent is about 0.26 Hz at 440 */
    put(put_int(put(put_int(put(t, "Master tune:"), hz / 10), "."), hz % 10), "Hz");
    lcd_msg(t, now);
    return;
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
  if (SYS.rev_level && PART[p].revsw) text(206, y, "rv", C_FAINT, FONT_SMALL);
}
static void draw_foot(uint32_t now) {
  rect(0, FOOT_Y, SCREEN_W, SCREEN_H - FOOT_Y, C_CASE);
  char b[32], *d = put(put(b, PAGE_NAME[page]), "  ");
  d = put(d, demo_playing ? "DEMO" : "");
  text(4, FOOT_Y + 4, b, C_SEL, FONT_SMALL);
  if (now - saved_t < 1500 && saved_t) text(110, FOOT_Y + 4, "SAVED", C_TEXT, FONT_SMALL);
  text(SCREEN_W - 6 - text_width("MIDI", FONT_SMALL) - 16, FOOT_Y + 4, "MIDI", C_DIM, FONT_SMALL);
}
static void draw_midi_led(int on) { rect(SCREEN_W - 16, FOOT_Y + 8, 10, 8, on ? C_LED : C_LED_OFF); }

/* ---------------------------------------------------------------- setup */

#define STORE_MAGIC 0x4C415831u /* "LAX1" */
static struct {
  uint32_t magic;
  uint8_t prog[8], vol[PARTS], pan[PARTS], revsw[PARTS];
  int8_t keyshift[PARTS], fine[PARTS];
  uint8_t rev_mode, rev_time, rev_level, chan_low, bend_range;
  int8_t master_tune;
} store;

void setup(void) {
  la_init();
  if (!settings_load(&store, sizeof store) && store.magic == STORE_MAGIC) {
    for (int p = 0; p < PARTS; p++) {
      if (p < 8) PART[p].prog = store.prog[p] & 127;
      PART[p].vol = store.vol[p] & 127, PART[p].pan = store.pan[p] & 127, PART[p].revsw = store.revsw[p] & 1;
      PART[p].keyshift = (int8_t)clampi(store.keyshift[p], -24, 24), PART[p].fine = (int8_t)clampi(store.fine[p], -50, 50);
    }
    SYS.rev_mode = store.rev_mode & 3, SYS.rev_time = store.rev_time & 7, SYS.rev_level = store.rev_level & 7;
    SYS.chan_low = store.chan_low & 1, SYS.bend_range = (uint8_t)clampi(store.bend_range, 0, 24);
    SYS.master_tune = (int8_t)clampi(store.master_tune, -50, 50);
  }
  led_backlight(1);
  clear(C_CASE);
  draw_title();
  lcd_frame();
  lcd_msg(" LAX-32  Ver 0.1    ", 0);
  msg_until = 1800;
}

static void save_all(void) {
  store.magic = STORE_MAGIC;
  for (int p = 0; p < PARTS; p++) {
    if (p < 8) store.prog[p] = PART[p].prog;
    store.vol[p] = PART[p].vol, store.pan[p] = PART[p].pan, store.revsw[p] = PART[p].revsw;
    store.keyshift[p] = PART[p].keyshift, store.fine[p] = PART[p].fine;
  }
  store.rev_mode = SYS.rev_mode, store.rev_time = SYS.rev_time, store.rev_level = SYS.rev_level;
  store.chan_low = SYS.chan_low, store.bend_range = SYS.bend_range, store.master_tune = SYS.master_tune;
  settings_save(&store, sizeof store);
}

/* ---------------------------------------------------------------- loop */

static uint8_t row_shown[PARTS], row_prog[PARTS], row_vol[PARTS], row_lit[PARTS];
static uint32_t sh_foot = ~0u, led_b = ~0u, led_k = ~0u;
static int8_t led_shown = -1;

void loop(void) {
  uint32_t now = millis();
  int32_t e;

  /* ---- buttons */
  if (button_pressed(BTN_PLAY)) demo_start(!demo_playing), lcd_msg(demo_playing ? "Demo stop" : "Demo play", now);
  if (button_pressed(BTN_REC)) la_panic(), lcd_msg("All notes off", now);
  if (button_pressed(BTN_SAVE)) save_all(), saved_t = now ? now : 1, lcd_msg("Setup saved", now);
  static const uint8_t PAGE_BTN[3] = {BTN_HOME, BTN_FX, BTN_ENV};
  for (int i = 0; i < 3; i++)
    if (button_pressed(PAGE_BTN[i])) {
      page = (uint8_t)i;
      char b[24];
      put(put(b, PAGE_NAME[i]), i == 0 ? ": vol pan key fine" : i == 1 ? ": mode time level" : ": bend tune chan");
      lcd_msg(b, now);
    }
  if (button_pressed(BTN_OCT_DOWN) || button_pressed(BTN_OCT_UP)) {
    if (button_held(BTN_OCT_DOWN) && button_held(BTN_OCT_UP)) oct = 0;
    else oct = (int8_t)clampi(oct + (button_pressed(BTN_OCT_UP) ? 1 : -1), -3, 3);
    char b[16];
    put_int(put(b, "Octave "), oct);
    lcd_msg(b, now);
  }

  /* ---- encoders: PART, SOUND, SOUND GROUP */
  if ((e = encoder(ENC_SELECT))) part = (uint8_t)((part + PARTS * 8 + e) % PARTS), show_sound(now);
  if ((e = encoder(ENC_ALGORITHM)) && part != RHY) {
    la_midi((uint8_t)(0xC0 | part_channel(part)), (uint8_t)((PART[part].prog + 128 * 8 + e) & 127), 0);
    PART[part].prog = (uint8_t)((PART[part].prog + 128 * 8 + e) & 127); /* shown at once; the module agrees */
    show_sound(now);
  }
  if ((e = encoder(ENC_PRESETS)) && part != RHY) {
    int g = clampi(timbre_group(PART[part].prog) + (e > 0 ? 1 : -1), 0, GROUPS - 1);
    la_midi((uint8_t)(0xC0 | part_channel(part)), GROUP_FIRST[g], 0);
    PART[part].prog = GROUP_FIRST[g];
    show_sound(now);
  }
  for (int k = 0; k < 4; k++)
    if ((e = encoder(ENC_KNOB1 + k))) knob(k, e, now);

  /* ---- keys: play the part */
  int ch = part_channel(part);
  for (int k = 0; k < KEY_COUNT; k++) {
    if (key_pressed(k)) {
      int n = part == RHY ? 35 + k + (oct > 0 ? oct * 12 : 0) : KEY_FIRST_NOTE + k + oct * 12;
      n = clampi(n, 0, 127);
      key_note[k] = (uint8_t)(n + 1);
      la_midi((uint8_t)(0x90 | ch), (uint8_t)n, 100);
    }
    if (key_released(k) && key_note[k]) la_midi((uint8_t)(0x80 | ch), (uint8_t)(key_note[k] - 1), 0), key_note[k] = 0;
  }
  /* ---- MIDI in */
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2))
    if (st >= 0x80 && st < 0xF0) la_midi(st, d1, d2);

  /* ---- the MIDI MESSAGE light */
  if (la_midi_seen != midi_seen_last) midi_seen_last = la_midi_seen, midi_light_t = now;
  int led = now - midi_light_t < 60;

  /* ---- LEDs: keys held; PLAY while the demo plays */
  uint32_t lk = 0, lb = 1u << PAGE_BTN[page];
  for (int k = 0; k < KEY_COUNT; k++)
    if (key_held(k)) lk |= 1u << k;
  if (demo_playing) lb |= 1u << BTN_PLAY;
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

  /* ---- the LCD: a message for a while, else the parts that play and the volume */
  if (now < msg_until) lcd_show(msg);
  else {
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
  /* ---- the parts, the footer, the light */
  for (int p = 0; p < PARTS; p++) {
    int lit = PART[p].active > 0;
    uint8_t sel = (uint8_t)(p == part);
    if (row_shown[p] != (uint8_t)(sel + 2) || row_prog[p] != PART[p].prog || row_vol[p] != PART[p].vol || row_lit[p] != lit)
      row_shown[p] = (uint8_t)(sel + 2), row_prog[p] = PART[p].prog, row_vol[p] = PART[p].vol, row_lit[p] = (uint8_t)lit, draw_row(p, lit);
  }
  uint32_t h = (uint32_t)page | (uint32_t)demo_playing << 2 | (uint32_t)(now - saved_t < 1500) << 3 | (uint32_t)SYS.chan_low << 4 | (uint32_t)SYS.rev_level << 5;
  if (h != sh_foot) {
    sh_foot = h, draw_foot(now), led_shown = -1;
    for (int p = 0; p < PARTS; p++) row_shown[p] = 0;
  }
  if (led != led_shown) led_shown = (int8_t)led, draw_midi_led(led);
}

/* ---------------------------------------------------------------- the sound */

void audio(int32_t *out, uint32_t frames) {
  demo_tick(frames);
  la_render(out, frames);
}
