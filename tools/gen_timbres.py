#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LAX-32's 128 timbres and 30 rhythm sounds -> src/timbres.h

The programs follow the order of the 1987 LA modules' preset bank, so music written for them finds the right
kind of instrument on each program number; every sound here is our own synthesis (no samples). A timbre has one or
two partials, as in LA synthesis: often an attack partial (FM or noise, standing in for a sampled attack) and a
sustained partial (an oscillator through a resonant filter), mixed or ring-modulated.

  python3 tools/gen_timbres.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "src" / "timbres.h"
W = {"SAW": 0, "SQR": 1, "SINE": 2, "FM": 3, "NOISE": 4, "ORGAN": 5}
FIELDS = "wave coarse fine pw fmr fmi fmd cut res fenv fa fd a d s r penv pdec level velsens".split()
BASE = dict(wave="SAW", coarse=0, fine=0, pw=64, fmr=2, fmi=0, fmd=64, cut=100, res=0, fenv=0, fa=0, fd=64, a=0, d=64, s=127, r=50,
            penv=0, pdec=0, level=100, velsens=64)


def P(**kw):
  p = dict(BASE)
  p.update(kw)
  return p


# ---- families: functions returning (partials, ring, vibrato depth, vibrato rate)
def piano(bright=0, det=0):
  return [P(wave="FM", fmr=2, fmi=30 + bright, fmd=82, cut=127, d=108, s=0, r=70, level=96),
          P(wave="SAW", fine=det, cut=46 + bright // 2, fenv=50, fd=92, d=104, s=0, r=66, level=60)], 0, 0, 0


def epiano(ratio=2, idx=38, dec=76):
  return [P(wave="FM", fmr=ratio, fmi=idx, fmd=dec, cut=127, d=102, s=0, r=76, level=100),
          P(wave="SINE", d=104, s=0, r=76, level=50)], 0, 0, 0


def organ(click=40, vib=0, octave=0):
  return [P(wave="ORGAN", coarse=12 * octave, cut=120, a=2, s=127, r=30, level=90, velsens=0),
          P(wave="NOISE", cut=110, res=40, d=30, s=0, r=10, level=click, velsens=0)], 0, vib, 70


def pipe(octave=0, slow=40):
  return [P(wave="ORGAN", cut=110, a=slow, s=127, r=60, level=80, velsens=0),
          P(wave="SQR", coarse=12 + 12 * octave, cut=80, a=slow, s=127, r=60, level=40, velsens=0)], 0, 0, 0


def pluck(wave="SAW", pw=64, cut=40, res=20, dec=90, fenv=60, attack_fm=0, level=100):
  ps = [P(wave=wave, pw=pw, cut=cut, res=res, fenv=fenv, fd=dec - 20, d=dec, s=0, r=60, level=level)]
  if attack_fm:
    ps.append(P(wave="FM", fmr=attack_fm, fmi=40, fmd=36, d=48, s=0, r=30, level=50))
  return ps, 0, 0, 0


def brass(cut=46, fenv=40, att=30, det=8, dark=0):
  return [P(wave="SAW", cut=cut - dark, res=20, fenv=fenv, fa=att + 30, fd=84, a=att, d=84, s=100, r=60, level=90),
          P(wave="SAW", fine=det, cut=cut - dark, res=20, fenv=fenv, fa=att + 30, fd=84, a=att, d=84, s=100, r=60, level=80)], 0, 16, 74


def synbass(wave="SAW", res=70, fenv=70, cut=30, dec=72, sub=0):
  ps = [P(wave=wave, cut=cut, res=res, fenv=fenv, fd=dec - 6, d=dec, s=60, r=40, level=110)]
  if sub:
    ps.append(P(wave="SQR", coarse=-12, cut=60, d=dec, s=80, r=40, level=sub))
  return ps, 0, 0, 0


def strings(cut=64, att=86, rel=88, det=10, vib=20, octave=0):
  return [P(wave="SAW", coarse=12 * octave, cut=cut, res=10, fenv=10, fa=att, fd=90, a=att, s=127, r=rel, level=80, velsens=40),
          P(wave="SAW", coarse=12 * octave, fine=det, cut=cut, res=10, a=att, s=127, r=rel, level=74, velsens=40)], 0, vib, 72


def wind(wave="SQR", pw=40, cut=60, res=30, att=30, breath=30, vib=24, coarse=0):
  return [P(wave=wave, pw=pw, coarse=coarse, cut=cut, res=res, fenv=24, fa=att + 20, fd=80, a=att, s=120, r=44, level=96),
          P(wave="NOISE", coarse=coarse + 24, cut=90, res=50, a=att, d=70, s=40, r=40, level=breath)], 0, vib, 78


def flute(coarse=0, breath=46, vib=30):
  return [P(wave="SINE", coarse=coarse, a=36, s=127, r=46, level=100),
          P(wave="NOISE", coarse=coarse + 12, cut=96, res=60, a=20, d=60, s=50, r=40, level=breath)], 0, vib, 80


def bell(ratio=7, idx=50, dec=104, att=0, body=0, coarse=0):
  ps = [P(wave="FM", coarse=coarse, fmr=ratio, fmi=idx, fmd=dec - 6, cut=127, a=att, d=dec, s=0, r=dec - 8, level=96)]
  if body:
    ps.append(P(wave="SINE", coarse=coarse, a=att, d=dec, s=0, r=dec - 10, level=body))
  return ps, 0, 0, 0


def pad(wave="SAW", att=96, cut=56, bell_ratio=0, vib=10, det=12):
  ps = [P(wave=wave, cut=cut, res=20, fenv=20, fa=att, fd=96, a=att, s=127, r=94, level=80, velsens=30)]
  ps.append(P(wave="FM", fmr=bell_ratio, fmi=40, fmd=96, d=104, s=0, r=96, level=60) if bell_ratio else
            P(wave=wave, fine=det, cut=cut, a=att, s=127, r=94, level=70, velsens=30))
  return ps, 0, vib, 70


def perc(wave="SINE", penv=12, pdec=40, dec=80, noise=0, cut=127, coarse=0, res=0):
  ps = [P(wave=wave, coarse=coarse, cut=cut, res=res, d=dec, s=0, r=dec - 10, penv=penv, pdec=pdec, level=110, velsens=90)]
  if noise:
    ps.append(P(wave="NOISE", cut=100, d=dec - 20, s=0, r=40, level=noise, velsens=90))
  return ps, 0, 0, 0


T = []  # (name, (partials, ring, vib, rate))
def add(name, spec):
  assert len(name) <= 10, name
  T.append((name, spec))


add("AcouPiano1", piano())
add("AcouPiano2", piano(bright=12))
add("AcouPiano3", piano(bright=6, det=8))
add("ElecPiano1", epiano())
add("ElecPiano2", epiano(2, 50, 70))
add("ElecPiano3", epiano(14, 20, 66))
add("ElecPiano4", epiano(6, 30, 80))
add("Honkytonk", piano(bright=8, det=22))
add("Elec Org 1", organ())
add("Elec Org 2", organ(click=60, vib=20))
add("Elec Org 3", organ(click=20, octave=1))
add("Elec Org 4", organ(click=70, vib=30))
add("Pipe Org 1", pipe())
add("Pipe Org 2", pipe(octave=1))
add("Pipe Org 3", pipe(slow=60))
add("Accordion", ([P(wave="SQR", pw=40, cut=80, a=10, s=127, r=40, level=80), P(wave="SAW", fine=10, cut=70, a=10, s=127, r=40, level=60)], 0, 14, 84))
add("Harpsi 1", pluck("SAW", cut=80, res=0, dec=92, fenv=30))
add("Harpsi 2", pluck("SQR", pw=30, cut=90, res=0, dec=94, fenv=20))
add("Harpsi 3", pluck("SAW", cut=90, res=10, dec=96, fenv=20, attack_fm=4))
add("Clavi 1", pluck("SQR", pw=18, cut=60, res=50, dec=80, fenv=40))
add("Clavi 2", pluck("SQR", pw=24, cut=70, res=60, dec=76, fenv=50))
add("Clavi 3", pluck("SAW", cut=50, res=70, dec=82, fenv=60))
add("Celesta 1", bell(8, 20, 96))
add("Celesta 2", bell(8, 30, 98, coarse=12))
add("Syn Brass1", brass())
add("Syn Brass2", brass(cut=56, fenv=60, att=10))
add("Syn Brass3", brass(cut=40, fenv=50, att=40, det=14))
add("Syn Brass4", brass(cut=60, fenv=30, att=20, det=4))
add("Syn Bass 1", synbass())
add("Syn Bass 2", synbass("SQR", res=50, fenv=50, cut=40, dec=76, sub=60))
add("Syn Bass 3", synbass("SAW", res=90, fenv=90, cut=20, dec=66))
add("Syn Bass 4", synbass("SQR", res=20, fenv=30, cut=50, dec=84, sub=80))
add("Fantasy", pad("SAW", att=70, cut=60, bell_ratio=9))
add("Harmo Pan", flute(0, 60, 26))
add("Chorale", ([P(wave="FM", fmr=2, fmi=18, fmd=127, cut=74, res=30, a=96, s=127, r=94, level=86), P(wave="SAW", cut=50, a=96, s=127, r=94, level=50)], 0, 20, 70))
add("Glasses", bell(7, 40, 104, att=60, body=40))
add("Soundtrack", pad("SAW", att=90, cut=64, bell_ratio=5))
add("Atmosphere", pad("SQR", att=100, cut=50, vib=20))
add("Warm Bell", bell(5, 36, 106, body=60))
add("Funny Vox", ([P(wave="SQR", pw=30, cut=50, res=100, fenv=40, fa=60, fd=80, a=30, s=110, r=50, penv=2, pdec=60, level=96)], 0, 30, 76))
add("Echo Bell", bell(9, 40, 108))
add("Ice Rain", bell(15, 56, 100, body=30))
add("Oboe 2001", wind("SQR", pw=26, cut=56, res=60, breath=20, vib=30))
add("Echo Pan", flute(0, 80, 20))
add("DoctorSolo", ([P(wave="SAW", cut=70, res=60, fenv=30, fd=80, a=4, s=127, r=50, level=96)], 0, 40, 76))
add("SchoolDaze", ([P(wave="SQR", pw=50, cut=90, d=70, s=40, r=40, level=96), P(wave="FM", fmr=4, fmi=30, fmd=40, d=50, s=0, r=30, level=40)], 0, 0, 0))
add("Bellsinger", ([P(wave="FM", fmr=2, fmi=18, fmd=127, cut=80, a=80, s=127, r=90, level=80), P(wave="FM", fmr=7, fmi=40, fmd=96, d=104, s=0, r=96, level=70)], 0, 20, 70))
add("SquareWave", ([P(wave="SQR", cut=110, a=2, s=127, r=40, level=86)], 0, 0, 0))
add("Str Sect 1", strings())
add("Str Sect 2", strings(cut=70, att=70, det=14))
add("Str Sect 3", strings(cut=58, att=96, rel=96, det=8))
add("Pizzicato", pluck("SAW", cut=40, res=10, dec=70, fenv=40))
add("Violin 1", strings(cut=70, att=60, det=4, vib=36))
add("Violin 2", strings(cut=76, att=50, det=6, vib=40))
add("Cello 1", strings(cut=54, att=60, det=4, vib=30))
add("Cello 2", strings(cut=60, att=50, det=6, vib=36))
add("Contrabass", strings(cut=44, att=50, det=4, vib=20))
add("Harp 1", pluck("SQR", pw=40, cut=46, res=0, dec=96, fenv=60))
add("Harp 2", pluck("SAW", cut=50, res=10, dec=98, fenv=50, attack_fm=2))
add("Guitar 1", pluck("SAW", cut=40, res=20, dec=94, fenv=60, attack_fm=3))
add("Guitar 2", pluck("SAW", cut=56, res=30, dec=96, fenv=60, attack_fm=5))
add("Elec Gtr 1", ([P(wave="SQR", pw=36, cut=60, res=30, fenv=30, fd=90, d=104, s=60, r=56, level=90)], 0, 10, 74))
add("Elec Gtr 2", ([P(wave="SAW", cut=70, res=40, fenv=40, fd=86, d=100, s=40, r=56, level=90), P(wave="SAW", fine=8, cut=70, d=100, s=40, r=56, level=60)], 0, 0, 0))
add("Sitar", ([P(wave="SAW", cut=60, res=90, fenv=60, fd=96, d=106, s=0, r=70, penv=1, pdec=50, level=96), P(wave="FM", fmr=6, fmi=50, fmd=80, d=100, s=0, r=60, level=40)], 1, 0, 0))
add("Acou Bass1", ([P(wave="SINE", d=86, s=40, r=40, level=110), P(wave="SAW", cut=30, fenv=40, fd=60, d=70, s=0, r=30, level=60)], 0, 0, 0))
add("Acou Bass2", ([P(wave="SINE", d=90, s=50, r=40, level=110), P(wave="FM", fmr=2, fmi=40, fmd=50, d=70, s=0, r=30, level=50)], 0, 0, 0))
add("Elec Bass1", synbass("SAW", res=10, fenv=40, cut=30, dec=86))
add("Elec Bass2", synbass("SQR", res=10, fenv=30, cut=36, dec=88))
add("Slap Bass1", ([P(wave="SAW", cut=50, res=30, fenv=80, fd=60, d=84, s=50, r=40, level=110), P(wave="FM", fmr=4, fmi=60, fmd=40, d=50, s=0, r=30, level=60)], 0, 0, 0))
add("Slap Bass2", ([P(wave="SQR", pw=30, cut=56, res=40, fenv=80, fd=60, d=84, s=40, r=40, level=110), P(wave="FM", fmr=6, fmi=60, fmd=40, d=50, s=0, r=30, level=60)], 0, 0, 0))
add("Fretless 1", ([P(wave="SINE", a=10, d=90, s=90, r=50, level=110), P(wave="SQR", cut=40, a=10, d=90, s=60, r=50, level=40)], 0, 16, 70))
add("Fretless 2", ([P(wave="SAW", cut=48, res=30, a=10, d=90, s=90, r=50, level=127), P(wave="SINE", a=10, d=90, s=90, r=50, level=80)], 0, 20, 70))
add("Flute 1", flute())
add("Flute 2", flute(0, 30, 36))
add("Piccolo 1", flute(12, 40, 30))
add("Piccolo 2", flute(12, 24, 36))
add("Recorder", wind("SQR", pw=60, cut=50, res=0, breath=40, vib=10))
add("Pan Pipes", flute(0, 90, 20))
add("Sax 1", wind("SAW", cut=60, res=30, breath=20, vib=30))
add("Sax 2", wind("SAW", cut=70, res=40, breath=30, vib=36))
add("Sax 3", wind("SAW", cut=50, res=20, breath=24, vib=26, coarse=-12))
add("Sax 4", wind("SAW", cut=56, res=30, breath=20, vib=30, coarse=-12))
add("Clarinet 1", wind("SQR", pw=64, cut=56, res=10, breath=16, vib=16))
add("Clarinet 2", wind("SQR", pw=60, cut=64, res=20, breath=20, vib=20))
add("Oboe", wind("SQR", pw=24, cut=60, res=50, breath=16, vib=30))
add("Engl Horn", wind("SQR", pw=28, cut=50, res=40, breath=16, vib=26))
add("Bassoon", wind("SQR", pw=30, cut=40, res=30, breath=10, vib=20, coarse=-12))
add("Harmonica", wind("SQR", pw=44, cut=70, res=30, breath=30, vib=40))
add("Trumpet 1", brass(cut=60, fenv=50, att=14, det=3))
add("Trumpet 2", brass(cut=66, fenv=60, att=10, det=5))
add("Trombone 1", brass(cut=46, fenv=40, att=20, det=3))
add("Trombone 2", brass(cut=50, fenv=50, att=18, det=5))
add("Fr Horn 1", brass(cut=36, fenv=30, att=40, det=3))
add("Fr Horn 2", brass(cut=40, fenv=36, att=36, det=6))
add("Tuba", brass(cut=30, fenv=30, att=24, det=2))
add("Brs Sect 1", brass(cut=56, fenv=50, att=20, det=12))
add("Brs Sect 2", brass(cut=60, fenv=60, att=16, det=16))
add("Vibe 1", bell(8, 14, 104, body=40))
add("Vibe 2", bell(8, 20, 100, body=50))
add("Syn Mallet", bell(6, 40, 80, body=40))
add("Windbell", bell(15, 40, 110, att=40))
add("Glock", bell(7, 30, 100, coarse=12))
add("Tube Bell", bell(7, 60, 110, body=30))
add("Xylophone", bell(6, 40, 70, coarse=12))
add("Marimba", bell(8, 44, 76, body=40))
add("Koto", ([P(wave="SAW", cut=40, res=50, fenv=80, fd=56, d=86, s=0, r=60, penv=1, pdec=30, level=96)], 0, 0, 0))
add("Sho", ([P(wave="ORGAN", cut=90, a=80, s=127, r=70, level=70, velsens=0), P(wave="SQR", coarse=7, cut=80, a=80, s=127, r=70, level=50, velsens=0)], 0, 10, 80))
add("Shakuhachi", flute(0, 100, 30))
add("Whistle 1", ([P(wave="SINE", coarse=12, a=20, s=127, r=40, level=96)], 0, 40, 84))
add("Whistle 2", ([P(wave="SINE", coarse=24, a=10, s=127, r=40, level=90)], 0, 50, 86))
add("Bottleblow", flute(-12, 70, 14))
add("Breathpipe", ([P(wave="NOISE", cut=70, res=110, a=40, s=127, r=50, level=110), P(wave="SINE", a=60, s=127, r=50, level=40)], 0, 10, 70))
add("Timpani", perc("SINE", penv=2, pdec=40, dec=100, noise=40, coarse=-12))
add("MelodicTom", perc("SINE", penv=5, pdec=50, dec=82, noise=30))
add("Deep Snare", perc("SINE", penv=4, pdec=30, dec=74, noise=90))
add("Elec Perc1", perc("SINE", penv=-12, pdec=50, dec=70))
add("Elec Perc2", perc("SQR", penv=12, pdec=40, dec=70, cut=80, res=40))
add("Taiko", perc("SINE", penv=6, pdec=60, dec=96, noise=50, coarse=-24))
add("Taiko Rim", perc("FM", penv=0, dec=50, noise=60))
add("Cymbal", ([P(wave="NOISE", coarse=36, cut=120, res=30, d=110, s=0, r=100, level=90), P(wave="FM", fmr=15, fmi=80, fmd=100, d=108, s=0, r=100, level=40)], 0, 0, 0))
add("Castanets", perc("FM", dec=40, noise=60, coarse=24))
add("Triangle", bell(14, 30, 106, coarse=24))
add("Orche Hit", ([P(wave="SAW", cut=70, res=20, fenv=40, fd=70, d=80, s=0, r=60, level=110), P(wave="SAW", coarse=12, fine=10, cut=70, d=80, s=0, r=60, level=90)], 0, 0, 0))
add("Telephone", ([P(wave="SQR", coarse=24, cut=100, a=2, s=127, r=10, level=70, velsens=0)], 1, 127, 120))
add("Bird Tweet", ([P(wave="SINE", coarse=36, a=10, d=60, s=60, r=30, penv=-12, pdec=40, level=80)], 0, 90, 118))
add("OneNoteJam", ([P(wave="SAW", cut=60, res=60, fenv=80, fd=60, d=70, s=30, r=40, level=100), P(wave="NOISE", cut=80, d=40, s=0, r=20, level=50)], 0, 0, 0))
add("Water Bell", ([P(wave="FM", fmr=5, fmi=50, fmd=90, d=100, s=0, r=90, penv=-3, pdec=50, level=96)], 0, 30, 100))
add("JungleTune", ([P(wave="FM", fmr=11, fmi=50, fmd=80, d=96, s=0, r=80, penv=4, pdec=30, level=90)], 0, 60, 96))
assert len(T) == 128, len(T)

GROUPS = [("PIANO", 0), ("ORGAN", 8), ("KEYBRD", 16), ("S-BRSS", 24), ("S-BASS", 28), ("SYNTH1", 32), ("SYNTH2", 40), ("STRING", 48),
          ("GUITAR", 57), ("BASS", 64), ("WIND", 72), ("BRASS", 88), ("MALLET", 97), ("SPECL", 105), ("PERCUS", 112), ("EFFECT", 122)]

# rhythm part: note -> (name, model, f1, f2, pdec, dec, noise, hp, lvl, choke, pan); models as snd.c's drums
K, S, M, TO, C, B, Z, N = range(8)  # kick snare metal tom clap bell zap noise
R = {
  35: ("Acou BD", K, 70, 48, 40, 380, 60, 0, 1000, 0, 0), 36: ("Acou BD", K, 80, 50, 45, 340, 90, 0, 1020, 0, 0),
  37: ("Rim Shot", B, 1800, 600, 0, 22, 260, 0, 760, 0, 6), 38: ("Acou SD", S, 190, 330, 40, 170, 760, 1400, 900, 0, 4),
  39: ("Hand Clap", C, 1150, 0, 0, 200, 1024, 0, 880, 0, -8), 40: ("Elec SD", S, 240, 420, 25, 120, 860, 2400, 860, 0, 4),
  41: ("Acou Lo Tom", TO, 95, 75, 120, 360, 40, 0, 860, 0, -24), 42: ("Closed HH", M, 420, 0, 0, 40, 380, 7500, 760, 1, 18),
  43: ("Acou Lo Tom", TO, 110, 85, 110, 330, 40, 0, 860, 0, -16), 44: ("Open HH 2", M, 420, 0, 0, 120, 380, 7500, 700, 1, 18),
  45: ("Acou Md Tom", TO, 135, 105, 100, 300, 40, 0, 840, 0, -6), 46: ("Open HH 1", M, 420, 0, 0, 320, 380, 7500, 700, 1, 18),
  47: ("Acou Md Tom", TO, 155, 120, 95, 280, 40, 0, 840, 0, 4), 48: ("Acou Hi Tom", TO, 185, 150, 90, 260, 40, 0, 820, 0, 14),
  49: ("Crash Cym", M, 360, 0, 0, 1300, 560, 4300, 560, 0, -20), 50: ("Acou Hi Tom", TO, 210, 170, 85, 240, 40, 0, 820, 0, 24),
  51: ("Ride Cym", M, 470, 0, 0, 800, 260, 5200, 520, 0, 20), 54: ("Tambourine", M, 1100, 0, 0, 140, 760, 8000, 560, 0, -14),
  56: ("Cowbell", B, 560, 830, 0, 180, 0, 0, 560, 0, 10), 60: ("High Bongo", TO, 430, 410, 15, 100, 0, 0, 720, 0, 20),
  61: ("Low Bongo", TO, 320, 300, 15, 120, 0, 0, 740, 0, 14), 62: ("Mt Hi Conga", TO, 330, 320, 10, 70, 60, 0, 740, 0, -10),
  63: ("High Conga", TO, 320, 300, 20, 150, 0, 0, 760, 0, -10), 64: ("Low Conga", TO, 220, 205, 20, 170, 0, 0, 780, 0, -16),
  65: ("Hi Timbale", B, 760, 1140, 0, 160, 300, 0, 600, 0, 10), 66: ("Lo Timbale", B, 520, 780, 0, 180, 300, 0, 620, 0, -10),
  67: ("High Agogo", B, 1180, 1770, 0, 160, 0, 0, 520, 0, 16), 68: ("Low Agogo", B, 880, 1320, 0, 180, 0, 0, 520, 0, 8),
  69: ("Cabasa", M, 1000, 0, 0, 70, 1024, 9000, 520, 0, -18), 70: ("Maracas", M, 1100, 0, 0, 40, 1024, 9500, 520, 0, 18),
  71: ("Smb Whis S", Z, 2600, 2400, 20, 80, 0, 0, 420, 2, 0), 72: ("Smb Whis L", Z, 2600, 2300, 30, 300, 0, 0, 420, 2, 0),
  73: ("Quijada", N, 2600, 1800, 200, 260, 1024, 0, 520, 0, 0), 75: ("Claves", B, 2500, 0, 0, 30, 0, 0, 640, 0, 12),
}
assert len(R) == 34  # 30 sounds, some on two keys (bass drum, toms)


def main():
  L = ["/* SPDX-License-Identifier: GPL-3.0-only */", "/* Generated by tools/gen_timbres.py (do not edit) */", "#ifndef TIMBRES_H", "#define TIMBRES_H",
       '#include "la.h"', ""]
  L.append("const timbre_t TIMBRE[128] = {")
  for name, (ps, ring, vib, rate) in T:
    parts = []
    for p in ps + [P(level=0)] * (2 - len(ps)):
      vals = [str(W[p["wave"]]) if f == "wave" else str(int(p[f])) for f in FIELDS]
      parts.append("{" + ", ".join(vals) + "}")
    L.append(f'    {{"{name}", {len(ps)}, {ring}, {vib}, {rate}, {{{", ".join(parts)}}}}},')
  L.append("};")
  L.append(f"#define GROUPS {len(GROUPS)}")
  L.append("const char *const GROUP_NAME[GROUPS] = {" + ", ".join(f'"{g}"' for g, _ in GROUPS) + "};")
  L.append("const uint8_t GROUP_FIRST[GROUPS] = {" + ", ".join(str(i) for _, i in GROUPS) + "};")
  L.append("\n/* the rhythm part: MIDI notes 35..75 */")
  L.append("const rsnd_t RHYTHM[41] = {")
  for n in range(35, 76):
    if n in R:
      r = R[n]
      L.append(f'    {{"{r[0]}", {", ".join(str(v) for v in r[1:])}}}, /* {n} */')
    else:
      L.append(f'    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, /* {n} */')
  L.append("};\n#endif")
  OUT.write_text("\n".join(L) + "\n")
  print(f"{OUT}: 128 timbres, rhythm on {len(R)} keys")


if __name__ == "__main__":
  main()
