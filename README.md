# LAX-32 — module de sons multitimbral « LA » pour M-VAVE FM-1

Firmware alternatif qui transforme le FM-1 en module de sons MIDI sur le modèle des expandeurs à synthèse LA de 1987, comme le Roland MT-32.

On y retrouve :

- **8 parties mélodiques et une partie rythmique**, pilotées en MIDI (parties sur les canaux 2 à 9, rythme sur le canal 10, comme à la sortie de l'appareil) ;
- **128 sons rangés dans le même ordre**, pour que la musique écrite pour ces modules, celle des jeux PC des années 80-90 notamment, tombe sur le bon type d'instrument à chaque numéro de programme ;
- **30 sons de batterie et de percussions**, sur les notes 35 à 75 ;
- **32 partiels de polyphonie** : 16 notes de un ou deux partiels ;
- une **réverbération** en 4 modes : ROOM, HALL, PLATE, TAP DELAY ;
- un **afficheur LCD de 20 caractères** à matrice de points 5 × 8, rétroéclairé en jaune-vert, avec le témoin MIDI MESSAGE ;
- depuis la version 0.2, un **arpégiateur** par partie et un **séquenceur de 6 pistes**, sur le modèle de ceux de [Skiff](https://github.com/illuminus5/skiff).

C'est un projet indépendant, sans lien avec Roland ni M-VAVE :

- Aucun échantillon ni aucune ROM d'origine n'est utilisé. Tous les sons sont synthétisés par LAX-32.
- La police de l'afficheur est dessinée pour ce projet.
- Le morceau de démonstration est une composition originale.

Il est bâti sur [foma](https://github.com/konsumer/foma) (plateforme Felucca), avec installeur web, mode secours et retour au firmware d'origine.

## Le son

Comme en synthèse LA, chaque son combine un ou deux **partiels**. Il y a souvent un partiel d'attaque, une frappe en FM ou un souffle de bruit qui joue le rôle des attaques échantillonnées. S'y ajoute un partiel de tenue : oscillateur en dent de scie, carré, sinus, FM ou orgue, passé dans un filtre passe-bas résonant avec ses enveloppes. Les deux partiels peuvent être mélangés ou multipliés (modulation en anneau).

Le moteur tourne à 22 kHz, ce qui donne la couleur un peu feutrée des modules de l'époque. Les batteries tournent à 44,1 kHz.

| Groupes | Programmes |
|---|---|
| PIANO | 1-8 (pianos acoustiques et électriques, honky-tonk) |
| ORGAN | 9-16 (orgues électriques, orgues d'église, accordéon) |
| KEYBRD | 17-24 (clavecins, clavinets, célesta) |
| S-BRSS / S-BASS | 25-32 (cuivres et basses de synthé) |
| SYNTH1 / SYNTH2 | 33-48 (Fantasy, Chorale, Glasses, Soundtrack, Ice Rain, Square Wave…) |
| STRING | 49-57 (sections, pizzicato, violon, violoncelle, contrebasse) |
| GUITAR | 58-64 (harpes, guitares, sitar) |
| BASS | 65-72 (contrebasses, basses électriques, slap, fretless) |
| WIND | 73-88 (flûtes, piccolo, flûte de Pan, saxophones, clarinettes, hautbois, basson, harmonica) |
| BRASS | 89-97 (trompettes, trombones, cors, tuba, sections) |
| MALLET | 98-105 (vibraphone, glockenspiel, cloches tubulaires, xylophone, marimba) |
| SPECL | 106-112 (koto, sho, shakuhachi, sifflets, souffles) |
| PERCUS | 113-122 (timbales, toms, taiko, cymbale, castagnettes, triangle) |
| EFFECT | 123-128 (orchestra hit, téléphone, oiseau, effets) |

**MIDI reconnu** : notes, changement de programme, volume (CC 7), panoramique (CC 10), modulation (CC 1, vibrato), sustain (CC 64), pitch bend (sur 12 demi-tons, comme à la sortie de l'appareil ; réglable), reset des contrôleurs (CC 121) et all notes off (CC 123).

Les messages système exclusifs (SysEx) ne sont pas reçus : le FM-1 les réserve à sa mise à jour. Les messages que certains jeux écrivaient sur l'afficheur, et l'édition des sons par SysEx, ne sont donc pas possibles.

## L'arpégiateur

Chaque partie a le sien. Il joue les notes tenues au clavier (ou reçues en MIDI sur le canal de la partie), au tempo du séquenceur.

- **ARP** : MODE, RATE (1/4, 1/8, 1/16, 1/32, 8T, 16T, 1/2, 1/1), OCTAVES (1-4), GATE.
- **ARP** une seconde fois, **ARP 2** : SWING, CHANCE (une note sur deux, sur trois…), HOLD (les notes continuent quand on lâche ; un nouvel accord remplace l'ancien), ORDER (de la plus grave à la plus aiguë, ou dans l'ordre joué).
- Les 14 modes de Skiff : UP, DN, UPDN, RND, ORD (ordre joué), REPEAT (la dernière note), DNUP, UP+8 (la première note reprise une octave plus haut), CONV (de l'extérieur vers l'intérieur), DIVG (l'inverse), PINKY (la note aiguë entre chaque), THUMB (la note grave entre chaque), WALK (un pas au hasard vers le haut ou le bas), CHORD (l'accord entier, une octave plus haut à chaque pas).
- Une partie dont l'arpégiateur est en marche porte `ar` dans la liste, et le bouton ARP s'allume.

## Le séquenceur

**6 pistes**, chacune jouant l'une des 9 parties (au départ les parties 1 à 5 et le rythme), de **64 pas** au plus, et **4 patterns** (A à D).

Chaque pas a :

- un accord de 4 notes au plus (sur la piste rythmique : 4 percussions) ;
- sa longueur, de 1/16 de pas à 64 pas, et les notes peuvent se chevaucher ;
- sa vélocité, et un accent ;
- une **chance** (1-100 %) ou une **condition** : FILL, !FILL, 1:2, 2:2, 1:3… 4:4 (au 1er passage sur 2, au 2e sur 2…) ;
- un **ratchet** (×1 à ×4) ou un **flam** ;
- un **nudge** : en avance ou en retard, de −32 à +31 soixante-quatrièmes de pas.

Par piste : la longueur (1-64), la taille du pas (1/4 à 1/32, triolets, 1/2, 1/1), le sens (FWD, RVRS, PING, RAND) et le mute. Pour tout le séquenceur : le tempo (40-240), le swing et le métronome.

Sur les pages du séquenceur, l'écran montre les 6 pistes et 16 pas, avec la tête de lecture et le curseur. Les lignes du dessous détaillent le pas sous le curseur.

| Commande FM-1 | Fonction |
|---|---|
| SEQ | **STEP** : PITCH, OCTAVE, LENGTH, VELOCITY du pas sous le curseur ; une 2e fois **FEEL** : CHANCE/CONDITION, RATCHET/FLAM, NUDGE, ACCENT ; une 3e fois **TRACK** : PART, LENGTH, STEP (taille du pas), DIRECTION |
| GLO | **GLOBAL** : TEMPO, SWING, PATTERN (A-D, change à la mesure suivante), CLICK (off, rec, on) |
| SELECT | la piste 1 à 6 |
| PRESETS | le curseur (le pas) |
| PLAY | lecture / arrêt (le séquenceur répond aussi à Start et Stop en MIDI) |
| REC | à l'arrêt, **enregistrement pas à pas** : les touches tenues ensemble font l'accord du pas, puis le curseur avance ; SEL ajoute un silence. Pendant la lecture, **enregistrement en direct** : la note va sur le pas le plus proche, s'ajoute à ce qui y est, et garde la durée de la touche. L'arpégiateur s'enregistre aussi. |
| EDIT | efface le pas ; tenu 1 s, efface la piste |
| LFO | mute de la piste |
| SEL tenu | FILL (pendant la lecture) |

Le pattern A est livré avec un petit groove (accords, basse, batterie) pour essayer tout de suite : SEQ puis PLAY.

## L'afficheur et les commandes

L'**afficheur** montre, comme l'original :

- au repos, `1 2 3 4 5 R |vol: 78`. Le numéro d'une partie se change en pavé plein quand elle joue, et `vol` est le volume MASTER ;
- quand on change de son, `1|PIANO  ElecPiano1` ;
- les réglages en cours : `3|vol:69`, `Reverb mode:PLATE`…

Sous l'afficheur, les 9 parties sont listées avec leur canal, leur son, leur volume et leur activité. Le témoin MIDI clignote à chaque message reçu.

| Commande FM-1 | Fonction |
|---|---|
| SELECT | partie 1 à 8, R (PART) |
| ALGORITHM | son 1 à 128 de la partie (SOUND) |
| PRESETS | groupe de sons (SOUND GROUP) |
| Clavier | joue la partie sélectionnée. Sur R, il joue les batteries depuis la touche la plus grave (grosse caisse). |
| OCT− / OCT+ | octave du clavier |
| HOME | PART : VOLUME, PAN, KEY SHIFT (±24), FINE TUNE (±50 cents) |
| FX | REVERB : MODE (ROOM, HALL, PLATE, TAP DELAY), TIME (1-8), LEVEL (0-7), réverb de la partie on/off |
| ENV | SYSTEM : BEND RANGE (0-24), MASTER TUNE, CHANNELS (2-9 ou 1-8, rythme toujours sur 10), DEMO (le morceau de démonstration, 1 min) |
| ARP, SEQ, GLO | l'arpégiateur et le séquenceur (ci-dessus) |
| SAVE | garde tout : sons, volumes, panoramiques, transpositions, réverb, canaux, arpégiateurs, pistes et les 4 patterns |
| MASTER | volume |

## Utilisation avec un ordinateur

Branche le FM-1 en USB. Il apparaît comme interface MIDI. Envoie-lui :

- un fichier MIDI écrit pour ces modules ;
- ou la sortie MIDI d'un émulateur de jeux DOS. Dans DOSBox, `mididevice=` vers le FM-1, et dans le jeu, la carte « Roland MT-32 ».

Les sons sont des approximations synthétisées : la musique sonnera dans le bon esprit, pas à l'identique.

## Obtenir et installer le firmware

- **GitHub** : pousse ce dossier sur la branche `main` d'un dépôt. Le workflow compile tout.
  - Le `.fwsc` est dans l'artefact `lax32-fwsc` et sur la branche `firmware`.
  - Avec Settings → Pages → Source : GitHub Actions, l'émulateur et l'installeur sont publiés sur GitHub Pages.
- **Docker Desktop** : `./fm1 build` ou `./fm1 dev` (http://localhost:8000).

L'installation se fait depuis la page Install (Chrome ou Edge, FM-1 branché en USB).

**Secours** : maintiens OCT− à l'allumage ; l'onglet Rescue remet le firmware M-VAVE V15. Installer un firmware non officiel se fait à tes risques.

## Développement

- `tools/gen_timbres.py` : les 128 sons (familles de partiels) et les sons de batterie → `src/timbres.h`
- `tools/gen_demo.py` : le morceau de démonstration → `src/demo.h`
- `tools/gen_font.py` : la police 5 × 8 de l'afficheur → `src/font5x8.h`
- `src/la.c` : voix et partiels, batteries, réverbération, MIDI (traité dans `audio()`)
- `src/seq.c` : l'arpégiateur et le séquenceur, dans `audio()` par blocs de 64 échantillons (1,5 ms)
- `src/main.c` : l'afficheur, les parties, les commandes, la sauvegarde
- `tests/run.sh` : joue la démo, chaque son, chaque percussion, chaque mode de réverb, le bend et la modulation dans des WAV, puis fait un fuzz MIDI (UBSan)
- Identité du paquet : `FM-1_38` + le dernier chiffre de la version (0.2 : `FM-1_382`)

## Licence

GPL-3.0-only. Crédits : [LICENSING.md](LICENSING.md).
