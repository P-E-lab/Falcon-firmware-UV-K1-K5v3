# Falcon v4.0.0

[English](README.md) | **Français** | [Español](README.es.md) | [Русский](README.ru.md)

Firmware pour UV-K1 / K5(v3), dérivé de [Sonic](https://github.com/Robby69400/Sonic) et compatible avec [F4HWN](https://github.com/armel/uv-k1-k5v3-firmware-custom) (64 emplacements Apps).

- Falcon fait de l'UV-K1 / K5(v3) un scanner et un récepteur rapides et complets, avec les outils pour trouver, suivre et décoder ce qui passe sur les ondes. Tout reste pratique et lisible.
- Canaux, listes et bandes peuvent être créés et modifiés directement au clavier.
- Un scanner très rapide : le scan Normal balaie environ 300 canaux par seconde et FastScan atteint environ 500 canaux par seconde ; une mémoire entière de plusieurs centaines de canaux défile en quelques secondes.
- Scan directement depuis le VFO : Long * scanne la liste du canal, F + * scanne tous les canaux en FastScan, avec A et B en mode fréquence Long * scanne la plage entre les deux, et le raccourci BAND SCAN scanne une bande enregistrée, en gardant l'affichage du VFO ; le raccourci SPECTRE VFO ouvre le Spectre autour de la fréquence du VFO.
- Full Watch suit les deux VFO et jusqu'à quatre canaux prioritaires ; Multilist Watch scanne vos listes mémoire en arrière-plan.
- Le Spectre et la Sentinelle scannent des listes, des bandes ou des plages, avec un historique de 200 détections et des listes noires ; le Spectre ajoute une cascade (touche 8) et un zoom autour de la dernière réception (touche 4).
- SKIP MEM (touche 7) cache les fréquences déjà en mémoire dans le Spectre et la Sentinelle, pour trouver de nouvelles fréquences.
- Écrans clairs, avec une page Keys Infos sur F (#) qui indique le rôle de chaque touche sur l'écran en cours.
- Apps Falcon (API des apps niveau 4, compatible F4HWN v7) :
  - **FKeys** : réattribue les touches longues et F+ de l'écran VFO. Choisir un preset (FALCON, F4HWN ou SONIC) ou régler chaque touche soi-même ; les pilotes CHIRP 4.0.0 modifient la même table de touches.
  - **FMessage** : messages texte de 64 caractères en FSK entre radios.
    - Les messages sont enregistrés dans la radio et s'ouvrent depuis le VFO (raccourci MESSAGES) ou depuis l'app.
    - Appel avec une mélodie qui indique qui appelle, et affichage des radios qui ont l'app ouverte (mélodie et indicatif réglés dans FMessageOption).
    - Chaque radio qui a accusé réception du dernier message est marquée d'un +.
  - **POCSAG Scan** : messages de pagers en 1200 / 512 bauds, jusqu'à 512 caractères, journal dans l'Historique.
  - **FCatch** : capture Close Call des signaux VHF et UHF.
  - **FirstAid**, **FSurvival** et **FMorse** : pour les situations d'urgence.
  - **FGuide** : guide des touches et raccourcis du firmware.
  - **FSystemInfo** : informations système.
  - **FSnake** : murs ouverts, touches 3/7 pour les deux sens.
- Installation en un fichier, multiboot avec Sonic et F4HWN (apps acceptées), et deux éditions : Falcon 4K (4096 canaux) et Falcon 1K (1024 canaux avec leurs propres réglages).
- Le firmware Falcon est codé avec Claude Code.

**Guide utilisateur (FR) : [docs/Falcon_v4.0.0_User_Guide_FR.pdf](docs/Falcon_v4.0.0_User_Guide_FR.pdf)**. Les guides de chaque version sont aussi joints aux [Releases](../../releases).

**Compatibilité (4.0.0) :** la V4 change l'organisation de la mémoire externe. Sur une même radio, tous les Falcon installés en multiboot doivent passer en V4 ensemble ; un Falcon 3.x dans un autre slot perdrait ses modules. Les apps POCSAG Scan, FCatch, FKeys et FMessage doivent être mises à jour avec la V4. Les canaux et réglages sont conservés ; utiliser les pilotes CHIRP 4.0.0.

## Captures d'écran

Écrans de Falcon 4.0.0, dans l’ordre :

<table>
<tr><td align="center" width="33%"><img src="docs/screenshots/01-single-vfo-rx.png" alt="VFO simple, réception"><br>1. VFO simple, réception</td><td align="center" width="33%"><img src="docs/screenshots/02-dual-vfo-rx.png" alt="Double VFO, réception"><br>2. Double VFO, réception</td><td align="center" width="33%"><img src="docs/screenshots/03-dual-vfo-full-watch.png" alt="Double VFO, Full Watch"><br>3. Double VFO, Full Watch</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/04-menu-g.png" alt="Menu G"><br>4. Menu G</td><td align="center" width="33%"><img src="docs/screenshots/05-menu-all.png" alt="Menu All"><br>5. Menu All</td><td align="center" width="33%"><img src="docs/screenshots/06-spectre-list.png" alt="Spectre, liste"><br>6. Spectre, liste</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/07-sentinel-range.png" alt="Sentinel, plage"><br>7. Sentinel, plage</td><td align="center" width="33%"><img src="docs/screenshots/08-edit-channel-popup.png" alt="Édition de canal, popup"><br>8. Édition de canal, popup</td><td align="center" width="33%"><img src="docs/screenshots/09-edit-channel.png" alt="Édition de canal"><br>9. Édition de canal</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/10-scanlist.png" alt="Listes de scan"><br>10. Listes de scan</td><td align="center" width="33%"><img src="docs/screenshots/11-scanlist-popup.png" alt="Liste de scan, popup"><br>11. Liste de scan, popup</td><td align="center" width="33%"><img src="docs/screenshots/12-scanlist-edit.png" alt="Liste de scan, édition"><br>12. Liste de scan, édition</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/13-radio-fm.png" alt="Radio FM"><br>13. Radio FM</td><td align="center" width="33%"><img src="docs/screenshots/14-apps.png" alt="Apps"><br>14. Apps</td><td align="center" width="33%"><img src="docs/screenshots/15-fmessage.png" alt="FMessage"><br>15. FMessage</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/16-fmessage-inbox.png" alt="FMessage, boîte de réception"><br>16. FMessage, boîte de réception</td><td align="center" width="33%"><img src="docs/screenshots/17-vfo-keys.png" alt="FKeys, touches VFO"><br>17. FKeys, touches VFO</td></tr>
</table>

## Contenu

- `firmware/f4hwn.falcon-4k.v4.0.0.bin` : firmware et installateur automatique tout-en-un (4.0.0 : SKIP MEM, SPECTRE VFO, sommeil du processeur, API des apps niveau 4, page POCSAG, 30 listes ; changelog complet en première page du guide). Le nom du fichier donne « Falcon 4k » dans la liste multiboot d'UV Studio.
- `firmware/f4hwn.falcon-1k.v4.0.0.bin` : Falcon 1K, la même version avec 1024 canaux rangés dans les réglages propres à son slot (format F4HWN). En multiboot avec Sonic 4K, chacun garde ses réglages et ses canaux. La Falcon 4K, elle, partage ses 4096 canaux avec Sonic 4K.
- `Falcon apps/FGuide.app` : mémo des touches de Falcon sur la radio (10 rubriques, en anglais), version 4.0.0 pour Falcon 4.0.0.
- `Falcon apps/FSystemInfo.app` : informations système de Falcon (firmware, mémoire, pile, batterie, identité).
- `Falcon apps/FCatch.app` : capture des émetteurs proches (Close Call) avec tonalité, nom du canal mémoire, choix VHF / UHF / VHF+UHF, blocage des fréquences (SK2) et historique de 17 entrées sauvegardé (Falcon 3.1.1 ou plus).
- `Falcon apps/POCSAGScan.app` : POCSAG Scan 2.1F, messages de bipeurs (1200 / 512 bauds) sur la liste mémoire POCSAG, journal dans l'Historique (Falcon 4.0 uniquement).
- `Falcon apps/FKeys.app` : FKeys 4.0.0, réattribue les touches longues et F+ de l'écran VFO (préréglages FALCON, F4HWN, SONIC ou personnalisé).
- `Falcon apps/FirstAid.app` : gestes de premiers secours (ERC 2025) et métronome de RCP.
- `Falcon apps/FSurvival.app` : conseils de survie et balise SOS à la lampe.
- `Falcon apps/FMorse.app` : envoi de messages en Morse, à la lampe ou par radio (anciennement Morse).
- `Falcon apps/FSnake.app` : jeu Snake (anciennement Snake F).
- `Falcon apps/Translations` : FirstAid et FSurvival en français (FR), espagnol (ES) et polonais (PL).
- `Falcon apps/FMessage.app` et `Falcon apps/FMessageOption.app` : messagerie entre radios (FMessage 3.0) et ses réglages (« FMessOption » dans la liste des apps). Les installer ensemble.
- `F4WHN apps` : les 46 applications officielles F4HWN disponibles (version 6.3.0), optionnelles.
- `chirp/falcon4k.chirp.v4.0.0.py` : pilote CHIRP de la Falcon 4K, avec les quatre canaux prioritaires, SetRxG et les raccourcis REVERSE FQ, MESSAGES, FMESSAGE et BAND SCAN, les 30 listes, les menus SCAN et DISPLAY et la table des touches FKeys.
- `chirp/falcon1k.chirp.v4.0.0.py` : pilote CHIRP de la Falcon 1K (mémoires 995 à 1024 : les bandes). Il n'accepte que Falcon 1K 4.0 ; chaque pilote refuse l'autre édition.
- `sources/runtime` : sources complètes du firmware, pilotes et dépendances embarquées.
- `sources/installer` : sources de l'installateur temporaire (il réutilise les pilotes de `sources/runtime`).
- `sources/fguide`, `sources/fsysteminfo` : sources de FGuide et FSystemInfo.
- `sources/fcatch` : sources de FCatch (capture et tonalités dérivées de FSearch / GOGUFW Search, Apache-2.0).
- `sources/firstaid`, `sources/fsurvival`, `sources/fmorse`, `sources/fsnake` : sources de FirstAid, FSurvival (avec leurs traductions), FMorse et FSnake.
- `sources/fmessage` : sources et tests de FMessage et FMessOption.
- `sources/pocsagrx` : sources de POCSAG Scan (dérivée de POCSAG RX d'Armel F4HWN, Apache-2.0).
- `sources/fkeys` : sources de FKeys.
- `tools`, `validation/tests`, `Dockerfile`, `build-with-docker.sh` : compilation et tests.
- `LICENSE`, `NOTICE` : licences et attributions. Les sources tierces conservent leurs mentions.

## Installation

Utiliser une version d'UVStudio qui gère les 64 emplacements Apps.

Pour conserver le firmware principal : démarrer ce firmware, ouvrir Firmware Slots dans UVStudio, écrire le BIN dans un slot libre 1 à 4, puis redémarrer en maintenant MENU pour choisir ce slot.

Pour remplacer le firmware principal : utiliser Firmware Flash / DFU avec le même BIN.

Au premier lancement, laisser l'écran d'installation « FALCON v4.0.0 » et « Powering Up » terminer son travail jusqu'au redémarrage automatique. Ne pas couper l'alimentation pendant l'installation ; une coupure est toutefois reprise au démarrage suivant, dans le même slot. Aucun clic « Installer les assets » n'est nécessaire.

Si l'écran d'installation affiche `S 04 E 10`, l'installation a été refusée sans rien effacer : la zone de Falcon contient des applications écrites par un autre firmware (emplacements Apps 69 à 98 d'un futur F4HWN). Appuyer sur PTT restaure le firmware principal.

Si Falcon affiche « MODULES MISSING / REINSTALL FALCON », ses modules en mémoire externe ont été effacés ou remplacés : réécrire le même BIN.

Les applications (celles de `Falcon apps` et de `F4WHN apps`) s'installent séparément dans l'onglet Apps d'UVStudio, au choix. Elles se lancent depuis le menu des apps (F+5) dans Falcon.

## Organisation de la mémoire

- Flash interne : le cœur du firmware.
- Mémoire externe, par slot de firmware : les modules de code (chargés à la demande dans 4 Ko de RAM, lecture DMA et contrôle par le CRC matériel ; un module peut écrire la flash ou appeler un autre module, il est rechargé avant d'y revenir) et les ressources (polices, textes des menus). Ils occupent la zone d'assets v7 du slot (0x1CA000 + slot × 16 Ko) et une banque de 48 Ko dans la réserve Apps v7 (0x18C000 + slot × 48 Ko, emplacements Apps 69 à 98), après le journal POCSAG (0x182000, 32 Ko) et 8 Ko gardés pour MESSAGES (0x18A000). Les 64 emplacements Apps, la configuration, la calibration et les canaux ne sont jamais touchés. Voir `sources/runtime/App/store_layout.h`.
- L'écran ne reçoit que les lignes modifiées, avec une image complète au moins une fois par seconde.

## Fonctionnement de cette version

Full Watch (menu RX Mode « FULL WATCH ») surveille les deux VFO et jusqu'à quatre canaux prioritaires, avec un créneau de 100 ms par position. Une position est quittée plus tôt (dès 50 ms) quand deux mesures successives du niveau reçu sont nettement sous le seuil d'ouverture du squelch (6 dB de marge) ; près du seuil ou au-dessus, les 100 ms complètes sont conservées. Le Dual ordinaire garde ses 100 ms, avec une constante distincte de celle de Full Watch. F+1 à F+4 affectent ou retirent la priorité correspondante. F+EXIT ajoute ou retire le canal de la liste Monitor hors saisie active. Le centre de l'écran affiche « << F WATCH >> » (« <<F WATCH>> » quand quatre canaux à 4 chiffres l'imposent) et le bandeau supérieur affiche FW.

Le Multilist Watch conserve son fonctionnement antérieur, avec ses deux priorités (sans bulles de numéros de canaux). Tous les raccourcis F+ y fonctionnent comme en VFO (F+0, F+5, F+6 à F+9, F+SK1/SK2, F+EXIT…), et la surveillance reprend une fois l'écran principal au repos. Les registres radio, les seuils de squelch et le moteur de scan sont conservés.

Après saisie directe d'un numéro de canal sans liste, les flèches parcourent les canaux dans l'ordre numérique depuis cette position, en sautant les emplacements vides.

Historique : l'action « HISTORY » (F1/F2 court ou long, M long) ouvre l'historique depuis le VFO, sans lancer de scan ; le titre passe dans la barre d'état et la liste gagne une ligne. Dans Spectre, Sentinel et depuis le VFO, une entrée écoutée (flèches) utilise sa propre modulation et sa largeur de bande ; appui long sur 1 : la fréquence de l'entrée passe dans le VFO principal (mode fréquence) et l'écran revient au VFO.

Messages : `*` bascule entre la page HISTORY et la page MESSAGES (le scan et la réception sont arrêtés sur MESSAGES). Chaque message reçu par FMessage y est enregistré automatiquement (mémoire externe 0x012000, deux secteurs de 4 Ko, zone libre dans F4HWN) ; les 32 derniers sont listés « 01-AA: DEBUT », du plus récent au plus ancien, avec un numéro d'arrivée de 01 à 32 (il ne change pas après une suppression et repart à 01 après CLEAR ALL) et une ligne pointillée entre deux ouvertures de FMessage. `M` ouvre le message, `6` le supprime (DELETE ?), `6` long efface tout (CLEAR ALL ?). L'action « MESSAGES » (F1/F2, M long) ouvre l'historique directement sur cette page. Écran, lecture et suppression sont dans le module externe « messages ». Le journal RX/TX n'existe plus.

POCSAG : un second `*` ouvre la page POCSAG, qui lit le journal écrit par POCSAG Scan (mémoire externe 0x182000, 8 secteurs, jusqu'à 512 caractères par page, gardé après extinction) ; mêmes touches que MESSAGES.

FMessage : l'action « FMESSAGE » (F1/F2, M long) ouvre l'app FMessage (raccourci d'app 15) ; double bip si elle n'est pas installée.

REVERSE : ligne « REVERSE: » ON/OFF dans l'éditeur de canal, action « REVERSE FQ », indicateur « R » à la place du +/- ; comme F4HWN, réglage enregistré, valable aussi en mode fréquence.

Audio en réception aligné sur F4HWN : valeurs d'origine de la puce en FM, AM et USB, menu SetRxG (porte de bruit RX, ON par défaut, forcée sur ON avec le compander RX ; octet 0x00A0C0 comme F4HWN), compander corrigé pour le BK4829. Puissances d'émission et niveau de la tonalité 1750 Hz comme F4HWN ; la tonalité 1750 s'entend dans le haut-parleur.

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

Les BIN tout-en-un (Falcon 1K et Falcon 4K) sont générés dans `firmware/`. La version est lue dans `sources/runtime/CMakePresets.json` (`VERSION_STRING_2`). `sources/runtime/build/S4K/onefile-measurements.json` (et `build/S1K` pour la 1K) donne l'occupation de la flash interne et de la mémoire externe.

Le style du code est décrit par `sources/.clang-format`.

Pour reconstruire les applications Falcon :

```sh
bash sources/fguide/guide/build.sh
bash sources/fsysteminfo/sysinfo/build.sh
bash sources/fmessage/fmessage/build.sh
bash sources/fmessage/fmsgoption/build.sh
bash sources/fkeys/keys/build.sh
bash sources/pocsagrx/pocsagrx/make.sh
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
