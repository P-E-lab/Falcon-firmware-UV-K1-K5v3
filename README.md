# Falcon 4K v3.1.1-OA

Firmware pour UV-K1 / Sonic 4K, dérivé de Sonic et compatible avec les applications F4HWN v7 (64 emplacements Apps).

## Contenu

- `firmware/Falcon4K_v3.1.1-OA.bin` : firmware et installateur automatique tout-en-un (3.1.1 : une app peut sauvegarder un bloc de ses propres données, utilisé par FCatch).
- `Falcon apps/FGuide.app` : mémo des touches de Falcon sur la radio (10 rubriques, en anglais).
- `Falcon apps/FSystemInfo.app` : informations système de Falcon (firmware, mémoire, pile, batterie, identité).
- `Falcon apps/FCatch.app` : capture des émetteurs proches (Close Call) avec tonalité, nom du canal mémoire, choix VHF / UHF / VHF+UHF, blocage des fréquences (SK2) et historique de 17 entrées sauvegardé (Falcon 3.1.1 ou plus).
- `Falcon apps/FirstAid.app` : gestes de premiers secours (ERC 2025) et métronome de RCP.
- `Falcon apps/FSurvival.app` : conseils de survie et balise SOS à la lampe.
- `Falcon apps/FMorse.app` : envoi de messages en Morse, à la lampe ou par radio (anciennement Morse).
- `Falcon apps/FSnake.app` : jeu Snake (anciennement Snake F).
- `Falcon apps/Translations` : FirstAid et FSurvival en français (FR), espagnol (ES) et polonais (PL).
- `Falcon apps/FMessage.app` et `Falcon apps/FMessageOption.app` : messagerie entre radios (FMessage 3.0) et ses réglages (« FMessOption » dans la liste des apps). Les installer ensemble.
- `F4WHN apps` : les 46 applications officielles F4HWN disponibles (version 6.3.0), optionnelles.
- `chirp/falcon.chirp.v3.1.0.py` : pilote CHIRP avec les quatre canaux prioritaires, SetRxG et les raccourcis REVERSE FQ, MESSAGES et FMESSAGE.
- `sources/runtime` : sources complètes du firmware, pilotes et dépendances embarquées.
- `sources/installer` : sources de l'installateur temporaire (il réutilise les pilotes de `sources/runtime`).
- `sources/fguide`, `sources/fsysteminfo` : sources de FGuide et FSystemInfo.
- `sources/fcatch` : sources de FCatch (capture et tonalités dérivées de FSearch / GOGUFW Search, Apache-2.0).
- `sources/firstaid`, `sources/fsurvival`, `sources/fmorse`, `sources/fsnake` : sources de FirstAid, FSurvival (avec leurs traductions), FMorse et FSnake.
- `sources/fmessage` : sources et tests de FMessage et FMessOption.
- `tools`, `validation/tests`, `Dockerfile`, `build-with-docker.sh` : compilation et tests.
- `LICENSE`, `NOTICE` : licences et attributions. Les sources tierces conservent leurs mentions.

## Installation

Utiliser UVStudio compatible avec le format v7 et les 64 emplacements Apps.

Pour conserver le firmware principal : démarrer ce firmware, ouvrir Firmware Slots dans UVStudio, écrire le BIN dans un slot libre 1 à 4, puis redémarrer en maintenant MENU pour choisir ce slot.

Pour remplacer le firmware principal : utiliser Firmware Flash / DFU avec le même BIN.

Au premier lancement, laisser l'écran d'installation « FALCON v3.1.1 » et « Powering Up » terminer son travail jusqu'au redémarrage automatique. Ne pas couper l'alimentation pendant l'installation ; une coupure est toutefois reprise au démarrage suivant, dans le même slot. Aucun clic « Installer les assets » n'est nécessaire.

Si l'écran d'installation affiche `S 04 E 10`, l'installation a été refusée sans rien effacer : la zone de Falcon contient des applications écrites par un autre firmware (emplacements Apps 80 à 99 d'un futur F4HWN). Appuyer sur PTT restaure le firmware principal.

Si Falcon affiche « MODULES MISSING / REINSTALL FALCON », ses modules en mémoire externe ont été effacés ou remplacés : réécrire le même BIN.

Les applications (celles de `Falcon apps` et de `F4WHN apps`) s'installent séparément dans l'onglet Apps d'UVStudio, au choix. Elles se lancent depuis le menu des apps (F+5) dans Falcon.

## Organisation de la mémoire

- Flash interne : le cœur du firmware.
- Mémoire externe, par slot de firmware : les modules de code (chargés à la demande dans 4 Ko de RAM, lecture DMA et contrôle par le CRC matériel ; un module peut écrire la flash ou appeler un autre module, il est rechargé avant d'y revenir) et les ressources (polices, textes des menus). Ils occupent la zone d'assets v7 du slot (0x1CA000 + slot × 16 Ko) et une banque de 32 Ko en fin de réserve Apps v7 (0x1A2000 + slot × 32 Ko, emplacements Apps 80 à 99). Les 64 emplacements Apps, la configuration, la calibration et les canaux ne sont jamais touchés. Voir `sources/runtime/App/store_layout.h`.
- L'écran ne reçoit que les lignes modifiées, avec une image complète au moins une fois par seconde.

## Fonctionnement de cette version

Full Watch (menu RX Mode « FULL WATCH ») surveille les deux VFO et jusqu'à quatre canaux prioritaires, avec un créneau de 100 ms par position. Une position est quittée plus tôt (dès 50 ms) quand deux mesures successives du niveau reçu sont nettement sous le seuil d'ouverture du squelch (6 dB de marge) ; près du seuil ou au-dessus, les 100 ms complètes sont conservées. Le Dual ordinaire garde ses 100 ms, avec une constante distincte de celle de Full Watch. F+1 à F+4 affectent ou retirent la priorité correspondante. F+EXIT ajoute ou retire le canal de la liste Monitor hors saisie active. Le centre de l'écran affiche « << F WATCH >> » (« <<F WATCH>> » quand quatre canaux à 4 chiffres l'imposent) et le bandeau supérieur affiche FW.

Le Multilist Watch conserve son fonctionnement antérieur, avec ses deux priorités (sans bulles de numéros de canaux). Tous les raccourcis F+ y fonctionnent comme en VFO (F+0, F+5, F+6 à F+9, F+SK1/SK2, F+EXIT…), et la surveillance reprend une fois l'écran principal au repos. Les registres radio, les seuils de squelch et le moteur de scan sont conservés.

Après saisie directe d'un numéro de canal sans liste, les flèches parcourent les canaux dans l'ordre numérique depuis cette position, en sautant les emplacements vides.

Historique : l'action « HISTORY » (F1/F2 court ou long, M long) ouvre l'historique depuis le VFO, sans lancer de scan ; le titre passe dans la barre d'état et la liste gagne une ligne. Dans Spectre, Sentinel et depuis le VFO, une entrée écoutée (flèches) utilise sa propre modulation et sa largeur de bande ; appui long sur 1 : la fréquence de l'entrée passe dans le VFO principal (mode fréquence) et l'écran revient au VFO.

Messages : `*` bascule entre la page HISTORY et la page MESSAGES (le scan et la réception sont arrêtés sur MESSAGES). Chaque message reçu par FMessage y est enregistré automatiquement (mémoire externe 0x012000, deux secteurs de 4 Ko, zone libre dans F4HWN v7) ; les 32 derniers sont listés « 01-AA: DEBUT », du plus récent au plus ancien, avec un numéro d'arrivée de 01 à 32 (il ne change pas après une suppression et repart à 01 après CLEAR ALL) et une ligne pointillée entre deux ouvertures de FMessage. `M` ouvre le message, `6` le supprime (DELETE ?), `6` long efface tout (CLEAR ALL ?). L'action « MESSAGES » (F1/F2, M long) ouvre l'historique directement sur cette page. Écran, lecture et suppression sont dans le module externe « messages ». Le journal RX/TX n'existe plus.

FMessage : l'action « FMESSAGE » (F1/F2, M long) ouvre l'app FMessage (raccourci d'app 15) ; double bip si elle n'est pas installée.

REVERSE : ligne « REVERSE: » ON/OFF dans l'éditeur de canal, action « REVERSE FQ », indicateur « R » à la place du +/- ; comme F4HWN, réglage enregistré, valable aussi en mode fréquence.

Audio en réception aligné sur F4HWN v7 : valeurs d'origine de la puce en FM, AM et USB, menu SetRxG (porte de bruit RX, ON par défaut, forcée sur ON avec le compander RX ; octet 0x00A0C0 comme F4HWN), compander corrigé pour le BK4829. Puissances d'émission et niveau de la tonalité 1750 Hz comme F4HWN v7 ; la tonalité 1750 s'entend dans le haut-parleur.

Sauvegarde du VFO : la fréquence et le numéro de canal sont enregistrés 3 s après le dernier changement (moins d'usure de la mémoire), et aussitôt avant une émission, le menu, un scan ou une app.

Raccourcis : une touche maintenue (F1, F2, M long) ne déclenche rien d'autre après son action ; l'historique, le Spectre, la Sentinelle, les scans et FMessage ouverts par un raccourci ignorent la touche encore enfoncée jusqu'à son relâchement.

VOX : repris de F4HWN (menu VOX OFF/1 à 10, action « VOX », icône dans la barre d'état ; mêmes octets de réglage et seuils de calibration que F4HWN).

Scanner : le Spectre, les scans VFO (F+*, appui long sur *, ML Watch) et l'historique ouvert depuis le Spectre ou le VFO déclenchent sur une montée fixe de 5 (2,5 dB) ; leur sensibilité se règle par le squelch dBm. La Sentinelle et son historique gardent leur seuil dynamique DS (touches * et F).

L'application FSystemInfo affiche la pile libre minimale mesurée depuis le démarrage.

64 emplacements Apps. DTMF et Live Viewer non activés.

## Compilation

Avec Docker :

```sh
bash build-with-docker.sh
```

En local : ARM GNU Toolchain 13.2.Rel1 (`arm-none-eabi-gcc` dans PATH), CMake >= 3.22, Ninja et Python 3 avec `zopfli==0.4.3`.

```sh
python3 -m pip install zopfli==0.4.3
bash tools/build-local.sh
```

Le BIN tout-en-un est généré dans `firmware/`. La version est lue dans `sources/runtime/CMakePresets.json` (`VERSION_STRING_2`). `sources/runtime/build/S4K/onefile-measurements.json` donne l'occupation de la flash interne et de la mémoire externe.

Le style du code est décrit par `sources/.clang-format`.

Pour reconstruire les applications Falcon :

```sh
bash sources/fguide/guide/build.sh
bash sources/fsysteminfo/sysinfo/build.sh
bash sources/fmessage/fmessage/build.sh
bash sources/fmessage/fmsgoption/build.sh
```

## Tests

Après compilation, installer les dépendances puis lancer :

```sh
python3 -m pip install -r tools/requirements-tests.txt
bash tools/test-local.sh
```

Les tests ARM exécutent le firmware compilé avec des périphériques simulés ; ils ne remplacent pas l'essai sur radio. Les tests de FMessage et FMessOption sont décrits dans `sources/fmessage/README.md`.

## Intégrité

`SHA256SUMS` contient les empreintes des fichiers de l'archive, hors ce fichier lui-même.
