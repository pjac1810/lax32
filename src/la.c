/* SPDX-License-Identifier: GPL-3.0-only */
/* LAX-32's sound: 16 voices of one or two partials (32 partials, as the originals had), run at half rate
 * (22.05 kHz, the period's warmth) with polyBLEP oscillators, FM, noise, a resonant state-variable low-pass and
 * envelopes per partial; ring modulation between the two; the rhythm part's 8 drum voices at full rate; a reverb in
 * four modes (room, hall, plate, tap delay). MIDI from loop() is queued and handled here, in audio(). */
#include "fm1.h"
#include "la.h"
#include "tables.h"
#include "timbres.h"

#define Q15 32767
#define ONE30 1073741824
#define CTRL 16u /* envelope steps, in 44.1 kHz frames (8 half-rate samples) */
#define VOICES 16
#define DRUM_VOICES 8

volatile part_t PART[PARTS];
volatile sys_t SYS = {1, 5, 3, 0, 12, 0};
const char *const REV_NAME[4] = {"ROOM", "HALL", "PLATE", "TAP DELAY"};
volatile uint32_t la_midi_seen;
volatile uint8_t la_partials_used;

int timbre_group(int prog) {
  int g = 0;
  for (int i = 0; i < GROUPS; i++)
    if (prog >= GROUP_FIRST[i]) g = i;
  return g;
}

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t sat15(int32_t x) {
  if (x >= 49152) return Q15;
  if (x <= -49152) return -Q15;
  int32_t x2 = (int32_t)(((int64_t)x * x) >> 15);
  int32_t x3 = (int32_t)(((int64_t)x2 * x) >> 15);
  return x - ((x3 * 4855) >> 15);
}
static uint32_t nz = 77777u;
static inline int32_t noise(void) {
  nz ^= nz << 13, nz ^= nz >> 17, nz ^= nz << 5;
  return (int32_t)nz >> 16;
}
static inline uint32_t hz_inc(int32_t hz) { return (uint32_t)clampi(hz, 0, 20000) * 97391u; }
static uint32_t coef(int32_t ms) { /* per sample, Q16 */
  if (ms < 1) ms = 1;
  return 65536u - (uint32_t)(655360 / (ms * 441));
}
static uint32_t coef_block(int32_t ms) { /* per CTRL frames, Q16 */
  int32_t n = ms * 441;
  if (n <= (int32_t)CTRL * 10) return 0;
  return 65536u - (uint32_t)(10485760 / n);
}
static int32_t attack_step(int32_t ms) {
  int32_t blocks = (ms * 441) / ((int32_t)CTRL * 10);
  return blocks < 1 ? ONE30 : ONE30 / blocks;
}
static inline int32_t decay(int32_t e, uint32_t c) { return (int32_t)(((int64_t)e * c) >> 16); }
static inline int32_t tms(int v) { return ENV_MS[clampi(v, 0, 127)]; }

typedef struct {
  int32_t low, band;
} svf_t;
static inline int32_t svf_f(int32_t hz) { return (clampi(hz, 10, 5500) * 4669) >> 10; }
static inline void svf(svf_t *s, int32_t in, int32_t f, int32_t q, int32_t *bp) {
  s->low += (int32_t)(((int64_t)f * s->band) >> 15);
  int32_t h = in - s->low - (int32_t)(((int64_t)q * s->band) >> 15);
  s->band += (int32_t)(((int64_t)f * h) >> 15);
  s->band = clampi(s->band, -262144, 262144), s->low = clampi(s->low, -262144, 262144);
  *bp = s->band;
}
static inline int32_t onepole_a(int32_t hz) {
  int32_t w = (clampi(hz, 1, 20000) * 4669) >> 10;
  return (int32_t)(((uint32_t)w << 15) / (uint32_t)(32768 + w));
}

enum { M_KICK, M_SNARE, M_METAL, M_TOM, M_CLAP, M_BELL, M_ZAP, M_NOISE };
enum { ST_IDLE, ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_RELEASE };
typedef struct {
  const rsnd_t *s;
  uint32_t t, ph[6];
  int32_t env, penv, hp1, hp2, hpa, gain;
  uint32_t ac, pc;
  svf_t f1;
  int8_t pad, part;
  int16_t pan, vol;
} dvoice_t;
static dvoice_t DV[DRUM_VOICES];
static uint32_t dage[DRUM_VOICES], dage_n;
static const uint16_t METAL_R[6] = {1024, 1518, 1844, 2607, 2693, 3990};


static void drum_hit(int n, int vel) {
  if (n < 35 || n > 75 || !RHYTHM[n - 35].name) return;
  const rsnd_t *s = &RHYTHM[n - 35];
  int best = -1;
  for (int i = 0; i < DRUM_VOICES; i++) {
    if (DV[i].s == s) best = i;
    if (s->choke && DV[i].s && DV[i].s->choke == s->choke && DV[i].s != s) DV[i].ac = coef(4);
  }
  if (best < 0)
    for (int i = 0; i < DRUM_VOICES; i++)
      if (!DV[i].s) {
        best = i;
        break;
      }
  if (best < 0) {
    best = 0;
    for (int i = 1; i < DRUM_VOICES; i++)
      if (dage[i] < dage[best]) best = i;
  }
  dvoice_t *v = &DV[best];
  v->s = s, v->pad = (int8_t)(n - 35), v->t = 0, v->env = ONE30, v->penv = ONE30;
  v->ac = coef(s->dec), v->pc = coef(s->pdec ? s->pdec : 1);
  for (int i = 0; i < 6; i++) v->ph[i] = (uint32_t)i * 715827883u;
  v->hp1 = v->hp2 = 0, v->hpa = onepole_a(s->hp ? s->hp : 1), v->f1 = (svf_t){0, 0};
  v->gain = (s->lvl * clampi(vel, 1, 127)) / 127;
  dage[best] = ++dage_n;
}

static int32_t drum_sample(dvoice_t *v) {
  const rsnd_t *s = v->s;
  int32_t e15 = v->env >> 15, p15 = v->penv >> 15, out = 0;
  switch (s->model) {
    case M_KICK:
    case M_TOM:
    case M_ZAP: {
      v->ph[0] += hz_inc(s->f2 + (((s->f1 - s->f2) * p15) >> 15));
      out = sine(v->ph[0]);
      if (s->model == M_KICK && v->t < 130) out += (noise() * s->noise) >> 10;
      else if (s->model == M_TOM) out += (noise() * s->noise) >> 13;
      if (s->model == M_KICK) out = sat15(out + (out >> 1));
      break;
    }
    case M_SNARE: {
      v->ph[0] += hz_inc(s->f1), v->ph[1] += hz_inc(s->f2);
      int32_t tone = ((sine(v->ph[0]) + (sine(v->ph[1]) >> 1)) * p15) >> 15, n = noise();
      v->hp1 += ((n - v->hp1) * v->hpa) >> 15;
      n -= v->hp1;
      out = ((tone * (1024 - s->noise)) >> 10) + ((n * s->noise) >> 10);
      break;
    }
    case M_METAL: {
      int32_t sum = 0;
      for (int i = 0; i < 6; i++) {
        v->ph[i] += hz_inc((s->f1 * METAL_R[i]) >> 10);
        sum += v->ph[i] < 2147483648u ? 5461 : -5461;
      }
      sum = ((sum * (1024 - s->noise)) >> 10) + ((noise() * s->noise) >> 10);
      v->hp1 += ((sum - v->hp1) * v->hpa) >> 15, sum -= v->hp1;
      v->hp2 += ((sum - v->hp2) * v->hpa) >> 15;
      out = sat15((sum - v->hp2) * 4);
      break;
    }
    case M_CLAP: {
      int32_t bp;
      svf(&v->f1, noise(), svf_f(s->f1), 9000, &bp);
      out = bp * 2;
      if (v->t < 3 * 441) e15 = clampi((441 - (int32_t)(v->t % 441)) * 74, 0, Q15), v->env = ONE30;
      break;
    }
    case M_BELL: {
      v->ph[0] += hz_inc(s->f1), v->ph[1] += hz_inc(s->f2 ? s->f2 : s->f1);
      int32_t sq = (v->ph[0] < 2147483648u ? 12000 : -12000) + (v->ph[1] < 2147483648u ? 12000 : -12000), bp;
      svf(&v->f1, sq + ((noise() * s->noise) >> 10), svf_f(2600), 16000, &bp);
      out = bp * 2;
      break;
    }
    case M_NOISE: {
      int32_t bp;
      svf(&v->f1, noise(), svf_f(s->f2 + (((s->f1 - s->f2) * p15) >> 15)), 6000, &bp);
      int32_t sw = clampi((int32_t)(v->t / 7), 0, Q15);
      if (sw < e15) e15 = sw;
      out = bp * 2;
      break;
    }
  }
  v->env = decay(v->env, v->ac), v->penv = decay(v->penv, v->pc), v->t++;
  if (v->env < 16384) v->s = 0;
  return (int32_t)(((int64_t)out * e15 >> 15) * v->gain >> 10);
}

static inline int32_t blep(uint32_t t, uint32_t inc, uint32_t rcp) {
  if (t < inc) {
    int32_t x = (int32_t)(((uint64_t)(t >> 16) * rcp) >> 17);
    return 2 * x - ((x * x) >> 15) - 32768;
  }
  uint32_t d = 0u - t;
  if (d < inc) {
    int32_t x = 32768 - (int32_t)(((uint64_t)(d >> 16) * rcp) >> 17);
    return (x * x) >> 15;
  }
  return 0;
}

static uint32_t inc_q8(int32_t p) {
  p = clampi(p, 0, 127 * 256);
  int n = p >> 8, f = p & 255;
  uint32_t a = note_inc(n), b = note_inc(n + 1);
  return a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8);
}

static void adsr(int32_t *lvl, uint8_t *st, int a, int d, int s, int r) {
  int32_t sus = (int32_t)(((int64_t)clampi(s, 0, 127) * ONE30) / 127);
  switch (*st) {
    case ST_ATTACK:
      *lvl += attack_step(tms(a));
      if (*lvl >= ONE30) *lvl = ONE30, *st = ST_DECAY;
      break;
    case ST_DECAY:
      *lvl = sus + (int32_t)(((int64_t)(*lvl - sus) * coef_block(tms(d))) >> 16);
      if (*lvl - sus < 65536 && *lvl - sus > -65536) *lvl = sus, *st = ST_SUSTAIN;
      break;
    case ST_SUSTAIN: *lvl = sus; break;
    case ST_RELEASE:
      *lvl = (int32_t)(((int64_t)*lvl * coef_block(tms(r))) >> 16);
      if (*lvl < 32768) *lvl = 0, *st = ST_IDLE;
      break;
    default: break;
  }
}


/* ---------------------------------------------------------------- voices: a note of one or two partials */

typedef struct {
  uint8_t ast, fst;
  uint32_t ph, phm, rcp;
  int32_t aenv, fenv, penv, fmenv, amp;
  int32_t ic1, ic2;
} pstate_t;
typedef struct {
  uint8_t part, note, gate, held, vel;
  uint32_t age;
  const timbre_t *t;
  int32_t base;       /* pitch x 256, without bend and vibrato */
  uint32_t vph;       /* vibrato */
  int32_t vdelay;     /* blocks before the timbre's vibrato comes in */
  pstate_t p[2];
} voice_t;
static voice_t V[VOICES];
static uint32_t vage;

static int voice_active(const voice_t *v) { return v->p[0].ast != ST_IDLE || (v->t && v->t->np > 1 && v->p[1].ast != ST_IDLE); }

static void note_off_voice(voice_t *v) {
  v->gate = 0, v->held = 0;
  for (int k = 0; k < 2; k++)
    if (v->p[k].ast != ST_IDLE) v->p[k].ast = ST_RELEASE;
}

static void note_on(int part, int note, int vel) {
  if (part == RHY) {
    drum_hit(note, vel);
    PART[RHY].active = 8; /* a flash on the LCD */
    return;
  }
  volatile part_t *P = &PART[part];
  const timbre_t *t = &TIMBRE[P->prog & 127];
  int key = clampi(note + P->keyshift, 0, 127);
  int best = -1;
  for (int i = 0; i < VOICES; i++) /* the same key again: that voice */
    if (V[i].part == part && V[i].note == note && voice_active(&V[i])) best = i;
  if (best < 0)
    for (int i = 0; i < VOICES; i++)
      if (!voice_active(&V[i])) {
        best = i;
        break;
      }
  if (best < 0) { /* steal: the oldest released, else the oldest */
    uint32_t old = 0xFFFFFFFFu;
    for (int i = 0; i < VOICES; i++)
      if (!V[i].gate && V[i].age < old) old = V[i].age, best = i;
    if (best < 0)
      for (int i = 0; i < VOICES; i++)
        if (V[i].age < old) old = V[i].age, best = i;
  }
  voice_t *v = &V[best];
  int fresh = !voice_active(v) || v->part != part;
  v->part = (uint8_t)part, v->note = (uint8_t)note, v->gate = 1, v->held = 0, v->vel = (uint8_t)clampi(vel, 1, 127), v->t = t;
  v->base = key * 256 + ((P->fine + SYS.master_tune) * 256) / 100;
  v->age = ++vage, v->vdelay = 40; /* ~ 0.6 s */
  for (int k = 0; k < 2; k++) {
    pstate_t *s = &v->p[k];
    if (k >= t->np) {
      s->ast = ST_IDLE, s->amp = 0;
      continue;
    }
    if (fresh) s->ic1 = s->ic2 = 0, s->amp = 0, s->aenv = 0;
    s->ast = ST_ATTACK, s->fst = ST_ATTACK, s->fenv = 0, s->penv = ONE30, s->fmenv = ONE30;
  }
  P->active = (uint8_t)clampi(P->active + 1, 0, 99);
}

static void note_off(int part, int note) {
  if (part == RHY) return;
  for (int i = 0; i < VOICES; i++)
    if (V[i].part == part && V[i].note == note && V[i].gate) {
      if (PART[part].sustain) V[i].held = 1, V[i].gate = 0;
      else note_off_voice(&V[i]);
    }
}

static void part_off(int part) {
  for (int i = 0; i < VOICES; i++)
    if (V[i].part == part && (V[i].gate || V[i].held)) note_off_voice(&V[i]);
}

/* ---------------------------------------------------------------- MIDI */

static int chan_part(int ch) {
  if (ch == 9) return RHY;
  int p = SYS.chan_low ? ch : ch - 1;
  return p >= 0 && p < 8 ? p : -1;
}

void la_direct(uint8_t st, uint8_t d1, uint8_t d2) {
  int part = chan_part(st & 15), t = st & 0xF0;
  la_midi_seen = la_midi_seen + 1;
  if (part < 0) return;
  volatile part_t *P = &PART[part];
  switch (t) {
    case 0x90:
    case 0x80: /* velocity 0 is a note-off too */
      if (t == 0x90 && d2) note_on(part, d1 & 127, d2);
      else note_off(part, d1 & 127);
      break;
    case 0xB0:
      switch (d1) {
        case 1: P->mod = d2 & 127; break;
        case 7: P->vol = d2 & 127; break;
        case 10: P->pan = d2 & 127; break;
        case 64:
          P->sustain = d2 >= 64;
          if (!P->sustain)
            for (int i = 0; i < VOICES; i++)
              if (V[i].part == part && V[i].held) note_off_voice(&V[i]);
          break;
        case 121: P->mod = 0, P->bend = 0, P->sustain = 0; break;
        case 120:
        case 123: part_off(part); break;
        default: break;
      }
      break;
    case 0xC0:
      if (part != RHY) P->prog = d1 & 127;
      break;
    case 0xE0: P->bend = (int16_t)(((d2 & 127) << 7 | (d1 & 127)) - 8192); break;
    default: break;
  }
}

#define QN 256
static volatile uint32_t q[QN];
static volatile uint32_t q_wr, q_rd;
static volatile uint8_t panic_req;
void la_midi(uint8_t st, uint8_t d1, uint8_t d2) {
  if (q_wr - q_rd >= QN) return;
  q[q_wr % QN] = (uint32_t)st << 16 | (uint32_t)d1 << 8 | d2;
  RING_PUBLISH();
  q_wr = q_wr + 1;
}
void la_panic(void) { panic_req = 1; }

/* ---------------------------------------------------------------- the reverb (half rate) */

#define RV_MAX 2213
static const int16_t RV_LEN[4] = {1427, 1693, 1951, 2213};
static int16_t rv[4][RV_MAX] POOL;
static int16_t ap0[142] POOL, ap1[107] POOL;
#define TAP_N 13300 /* 0.6 s at 22.05 kHz */
static int16_t tap_l[TAP_N] POOL, tap_r[TAP_N] POOL;
static int32_t rv_w[4], rv_lp[4], ap_w[2], tap_w;
static uint8_t rv_mode_was = 255;

static inline int16_t s16(int32_t v) { return (int16_t)clampi(v, -32767, 32767); }

static void reverb(int32_t x, int32_t *ol, int32_t *or_) {
  int mode = SYS.rev_mode & 3, tm = SYS.rev_time & 7;
  if (mode != rv_mode_was) { /* a new mode: the old tail goes */
    rv_mode_was = (uint8_t)mode;
    for (int l = 0; l < 4; l++)
      for (int i = 0; i < RV_MAX; i++) rv[l][i] = 0;
    for (int i = 0; i < TAP_N; i++) tap_l[i] = tap_r[i] = 0;
  }
  if (mode == 3) { /* TAP DELAY: two taps, crossed */
    int32_t len = 1500 + tm * 1680;
    int32_t i0 = tap_w - len, i1 = tap_w - len * 2 / 3;
    if (i0 < 0) i0 += TAP_N;
    if (i1 < 0) i1 += TAP_N;
    int32_t yl = tap_l[i0], yr = tap_r[i1];
    tap_l[tap_w] = s16(x + ((yr * 13000) >> 15)), tap_r[tap_w] = s16(x / 2 + ((yl * 13000) >> 15));
    if (++tap_w >= TAP_N) tap_w = 0;
    *ol = yl, *or_ = yr;
    return;
  }
  /* ROOM short, HALL long, PLATE bright and dense: two allpasses, four lines (Hadamard), damped, fed back */
  static const uint8_t SCALE[3] = {44, 100, 66};                /* % of the lines' lengths */
  static const int16_t DAMP[3] = {12000, 14000, 22000};
  int32_t fb = (mode == 0 ? 17000 : mode == 1 ? 22000 : 20000) + tm * (mode == 0 ? 1300 : 1000);
  int16_t *ab[2] = {ap0, ap1};
  static const int16_t AL[2] = {142, 107};
  for (int k = 0; k < 2; k++) {
    int32_t g = mode == 2 ? 22000 : 18000;
    int32_t v = ab[k][ap_w[k]], y = v - ((g * x) >> 15);
    ab[k][ap_w[k]] = s16(x + ((g * y) >> 15));
    if (++ap_w[k] >= AL[k]) ap_w[k] = 0;
    x = y;
  }
  int32_t r[4];
  for (int l = 0; l < 4; l++) {
    int32_t v = rv[l][rv_w[l]];
    rv_lp[l] += ((v - rv_lp[l]) * DAMP[mode]) >> 15;
    r[l] = rv_lp[l];
  }
  int32_t h[4] = {(r[0] + r[1] + r[2] + r[3]) >> 1, (r[0] - r[1] + r[2] - r[3]) >> 1, (r[0] + r[1] - r[2] - r[3]) >> 1,
                  (r[0] - r[1] - r[2] + r[3]) >> 1};
  for (int l = 0; l < 4; l++) {
    rv[l][rv_w[l]] = s16(sat15(((h[l] * fb) >> 15) + x));
    if (++rv_w[l] >= RV_LEN[l] * SCALE[mode] / 100) rv_w[l] = 0;
  }
  *ol = r[0] + r[2], *or_ = r[1] + r[3];
}

/* ---------------------------------------------------------------- render */

void la_init(void) {
  for (int p = 0; p < PARTS; p++) {
    PART[p].prog = 0, PART[p].vol = 100, PART[p].pan = 64, PART[p].mod = 0, PART[p].sustain = 0, PART[p].revsw = 1;
    PART[p].keyshift = 0, PART[p].fine = 0, PART[p].bend = 0, PART[p].active = 0;
  }
  /* as shipped: a sound in each part */
  static const uint8_t DEF[8] = {0, 68, 48, 95, 41, 44, 26, 34};
  for (int p = 0; p < 8; p++) PART[p].prog = DEF[p];
  for (int i = 0; i < VOICES; i++) V[i] = (voice_t){0};
  for (int i = 0; i < DRUM_VOICES; i++) DV[i].s = 0;
  rv_mode_was = 255;
}

static int32_t lfo_rate(int r) { return 3 + r * 6 / 127; } /* Hz */

void la_render(int32_t *out, uint32_t frames) {
  static int32_t hl[132], hr[132], hs[132], fl[512];
  if (frames > 256) frames = 256;
  /* ---- MIDI */
  if (panic_req) {
    panic_req = 0;
    for (int i = 0; i < VOICES; i++) note_off_voice(&V[i]);
    for (int p = 0; p < PARTS; p++) PART[p].sustain = 0, PART[p].active = 0;
  }
  while (q_rd != q_wr) {
    uint32_t m = q[q_rd % QN];
    q_rd = q_rd + 1;
    la_direct((uint8_t)(m >> 16), (uint8_t)(m >> 8), (uint8_t)m);
  }
  uint32_t hn = (frames + 1) / 2;
  for (uint32_t i = 0; i < hn + 1; i++) hl[i] = hr[i] = hs[i] = 0;
  for (uint32_t i = 0; i < 2 * frames; i++) fl[i] = 0;

  /* ---- the voices, at half rate, 8 samples per envelope step */
  int used = 0, act[PARTS] = {0};
  for (int vi = 0; vi < VOICES; vi++) {
    voice_t *v = &V[vi];
    if (!voice_active(v)) continue;
    const timbre_t *t = v->t;
    volatile part_t *P = &PART[v->part];
    act[v->part]++;
    int32_t vol = P->vol, pan = P->pan, mod = P->mod, revsw = P->revsw;
    int32_t gvol = vol * vol / 127;
    int32_t gl = 128 - pan, gr = pan; /* /128 */
    int32_t bend = (int32_t)P->bend * SYS.bend_range * 256 / 8192;
    for (uint32_t b0 = 0; b0 < hn; b0 += 8) {
      uint32_t n = hn - b0 < 8 ? hn - b0 : 8;
      /* vibrato: the timbre's (after a delay) and the mod wheel */
      v->vph += (uint32_t)lfo_rate(t->vibrate) * 97391u * 2u * n;
      if (v->vdelay > 0) v->vdelay--;
      int32_t vdepth = (v->vdelay ? 0 : t->vib) + mod;
      int32_t vib = (sine(v->vph) * vdepth) >> 15; /* up to ~ a semitone */
      int32_t sample1[8] = {0};
      for (int k = 0; k < t->np; k++) {
        pstate_t *s = &v->p[k];
        const partial_t *p = &t->p[k];
        if (s->ast == ST_IDLE) continue;
        used++;
        adsr(&s->aenv, &s->ast, p->a, p->d, p->s, p->r);
        if (s->fst == ST_ATTACK) {
          s->fenv += attack_step(tms(p->fa));
          if (s->fenv >= ONE30) s->fenv = ONE30, s->fst = ST_DECAY;
        } else {
          s->fenv = decay(s->fenv, coef_block(tms(p->fd)));
        }
        s->penv = decay(s->penv, coef_block(tms(p->pdec)));
        s->fmenv = decay(s->fmenv, coef_block(tms(p->fmd)));
        int32_t pitch = v->base + p->coarse * 256 + (p->fine * 256) / 100 + bend + vib * 2 +
                        (int32_t)(((int64_t)p->penv * 256 * (s->penv >> 15)) >> 15);
        uint32_t inc = inc_q8(pitch) * 2u;
        s->rcp = 0xFFFFFFFFu / ((inc >> 16) | 1u);
        uint32_t incm = (uint32_t)(((uint64_t)inc * p->fmr) >> 1);
        int32_t fmi = (int32_t)(((int64_t)p->fmi * 1304 * (s->fmenv >> 15)) >> 15);
        int keyf = (v->note - 60) * (p->wave == W_NOISE ? 6 : 3);
        int32_t ci = p->cut * 8 + ((p->fenv * (s->fenv >> 15)) >> 12) + keyf + (v->vel - 64) * p->velsens / 64;
        uint32_t g = SVF_G[clampi(ci + 106, 0, 1023)]; /* + 106: an octave up the table, the same cutoff at half rate */
        uint32_t kq = 131072u - (uint32_t)clampi(p->res, 0, 124) * 1024u;
        uint32_t den = 65536u + (uint32_t)(((uint64_t)g * (g + kq)) >> 16);
        int32_t a1 = (int32_t)(0xFFFFFFFFu / den), a2 = (int32_t)(((int64_t)g * a1) >> 16), a3 = (int32_t)(((int64_t)g * a2) >> 16);
        int32_t vs = 127 - p->velsens + p->velsens * v->vel / 127;
        int32_t amp = (int32_t)(((int64_t)(s->aenv >> 15) * vs * p->level * gvol) >> 21);
        int32_t a0 = s->amp, da = (amp - a0) / (int32_t)n;
        s->amp = amp;
        uint32_t pw = (uint32_t)p->pw << 25;
        for (uint32_t i = 0; i < n; i++) {
          uint32_t ph = s->ph;
          int32_t x, bb;
          switch (p->wave) {
            case W_SAW: x = (int32_t)(ph >> 16) - 32768 - blep(ph, inc, s->rcp); break;
            case W_SQR:
              bb = blep(ph, inc, s->rcp);
              x = (ph < pw ? 26000 : -26000) + ((bb - blep(ph - pw, inc, s->rcp)) * 26000 >> 15);
              break;
            case W_FM:
              s->phm += incm;
              x = sine(ph + (uint32_t)((int64_t)sine(s->phm) * fmi));
              break;
            case W_NOISE: x = noise(); break;
            case W_ORGAN: x = (sine(ph) >> 1) + (sine(ph << 1) * 11 >> 5) + (sine(ph * 3) >> 2) + (sine(ph << 2) >> 3); break;
            default: x = sine(ph); break;
          }
          s->ph = ph + inc;
          int32_t v0 = x * 64, v3 = v0 - s->ic2;
          int32_t v1 = (int32_t)(((int64_t)a1 * s->ic1 + (int64_t)a2 * v3) >> 16);
          int32_t v2 = s->ic2 + (int32_t)(((int64_t)a2 * s->ic1 + (int64_t)a3 * v3) >> 16);
          s->ic1 = clampi(2 * v1 - s->ic1, -67108864, 67108864), s->ic2 = clampi(2 * v2 - s->ic2, -67108864, 67108864);
          x = sat15(v2 >> 6);
          if (k == 0) sample1[i] = x;
          else if (t->ring) x = (x * sample1[i]) >> 15;
          x = (x * (a0 + da * (int32_t)i)) >> 15;
          uint32_t o = b0 + i;
          hl[o] += (x * gl) >> 7, hr[o] += (x * gr) >> 7;
          if (revsw) hs[o] += x;
        }
      }
    }
    if (!voice_active(v)) v->held = 0;
  }
  la_partials_used = (uint8_t)(used / ((hn + 7) / 8 ? (hn + 7) / 8 : 1));
  for (int p = 0; p < RHY; p++) PART[p].active = (uint8_t)act[p];

  /* ---- the reverb on the sends, at half rate */
  int32_t lvl = SYS.rev_level & 7, wet = lvl * lvl * 220; /* Q15: up to ~ 1/3 */
  for (uint32_t i = 0; i < hn; i++) {
    int32_t l, r;
    reverb(hs[i] >> 2, &l, &r);
    hl[i] += (l * wet) >> 15, hr[i] += (r * wet) >> 15;
  }

  /* ---- the rhythm part, full rate */
  volatile part_t *R = &PART[RHY];
  int32_t rvol = R->vol * R->vol / 127;
  for (int k = 0; k < DRUM_VOICES; k++) {
    dvoice_t *v = &DV[k];
    if (!v->s) continue;
    int32_t gl = 64 - v->s->pan, gr = 64 + v->s->pan;
    for (uint32_t i = 0; i < frames && v->s; i++) {
      int32_t s = (drum_sample(v) * rvol) >> 7;
      fl[2 * i] += (s * gl) >> 7, fl[2 * i + 1] += (s * gr) >> 7;
    }
  }
  if (R->active) R->active--;

  /* ---- up to full rate (linear), the drums, the master */
  static int32_t last_l, last_r;
  for (uint32_t i = 0; i < frames; i++) {
    uint32_t h = i / 2;
    int32_t L, Rr;
    if (i & 1) L = (hl[h] + (h + 1 < hn ? hl[h + 1] : hl[h])) / 2, Rr = (hr[h] + (h + 1 < hn ? hr[h + 1] : hr[h])) / 2;
    else L = h ? (hl[h] + hl[h - 1]) / 2 : (hl[0] + last_l) / 2, Rr = h ? (hr[h] + hr[h - 1]) / 2 : (hr[0] + last_r) / 2;
    L += fl[2 * i], Rr += fl[2 * i + 1];
    out[2 * i] += sat15((L * 5) >> 3);
    out[2 * i + 1] += sat15((Rr * 5) >> 3);
  }
  last_l = hl[hn - 1], last_r = hr[hn - 1];
}
