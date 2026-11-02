# LAX-32 — module de sons multitimbral « LA » pour M-VAVE FM-1

Firmware alternatif qui transforme le FM-1 en module de sons MIDI sur le modèle des expandeurs à synthèse LA de 1987, comme le Roland MT-32.

On y retrouve :

- **8 parties mélodiques et une partie rythmique**, pilotées en MIDI (parties sur les canaux 2 à 9, rythme sur le canal 10, comme à la sortie de l'appareil) ;
- **128 sons rangés dans le même ordre**, pour que la musique écrite pour ces modules, celle des jeux PC des années 80-90 notamment, tombe sur le bon type d'instrument à chaque numéro de programme ;
- **30 sons de batterie et de percussions**, sur les notes 35 à 75 ;
- **32 partiels de polyphonie** : 16 notes de un ou deux partiels ;
- une **réverbération** en 4 modes : ROOM, HALL, PLATE, TAP DELAY ;
- un **afficheur LCD de 20 caractères** à matrice de points 5 × 8, rétroéclairé en jaune-vert, avec le témoin MIDI MESSAGE.

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
| ENV | SYSTEM : BEND RANGE (0-24), MASTER TUNE, CHANNELS (2-9 ou 1-8, rythme toujours sur 10) |
| PLAY | joue ou arrête le morceau de démonstration (1 min) |
| REC | all notes off |
| SAVE | garde la configuration : sons, volumes, panoramiques, transpositions, réverb, canaux |
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
- `src/main.c` : l'afficheur, les parties, les commandes, la sauvegarde
- `tests/run.sh` : joue la démo, chaque son, chaque percussion, chaque mode de réverb, le bend et la modulation dans des WAV, puis fait un fuzz MIDI (UBSan)
- Identité du paquet : `FM-1_38` (dans `firmware.json`)

## Licence

GPL-3.0-only. Crédits : [LICENSING.md](LICENSING.md).
