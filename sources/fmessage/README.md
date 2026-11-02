# FMessage 3.0 et FMessage Option 3.0

Messages courts entre radios UV-K1 / UV-K5 V3, sous forme d'apps (Falcon, ou F4HWN avec API 3 et proxy de configuration).
La version suit celle de Falcon : FMessage 3.0 va avec Falcon 3.0.0.

## Installation

Installer `FMessage.app` et `FMessageOption.app` (dossier `Falcon apps` de l'archive) dans deux emplacements Apps avec UV Studio.
Remplacer toujours les DEUX apps ensemble. Réinstaller FMessage remet ses réglages par défaut
(indicatif AA, RX SOUND ON, CALL SOUND 01 STWARS).

## FMessage

- Bandeau du haut, en surbrillance : indicatif - READY à gauche (BUSY si la fréquence est occupée, CALL pendant un appel), fréquence à droite.
- HERE : les radios qui ont l'app ouverte (7 au plus) ; `+` = a reçu le dernier message envoyé.
- MSG : 64 caractères sur 4 lignes.
- Dernière ligne : nombre de messages non lus (NEW) et RESEND.

| Touche | Accueil |
|---|---|
| 0-9 | lettres (appuis répétés), appui long : chiffre ; 1 : `, . ? ! ' -` ; 0 : espace |
| `*` | efface ; appui long : efface tout |
| `< >` | déplace le curseur |
| F | envoie ; RESEND tant que le texte n'est pas modifié |
| PTT | appel : 1 s de porteuse puis la mélodie d'appel |
| M | Inbox |
| EXIT | quitte vers la liste des apps (confirmation s'il reste des messages) |

F et PTT sont refusés par un bip grave si la fréquence est occupée (BUSY).
Inbox : 6 messages, `*` = non lu ; M ouvre, `*` supprime (confirmation DELETE?), EXIT revient.
Message : M répond (retour à l'accueil, champ MSG vide), `*` supprime (confirmation), EXIT revient à l'Inbox.
Un nouveau message : deux bips à 1800 Hz (RX SOUND), rétroéclairage selon le réglage TX/RX de la radio, et LED verte qui clignote tant qu'un message reste non lu.
Sous Falcon 3.0.0 (cette version), chaque message reçu est aussi gardé dans l'écran MESSAGES de l'historique
(32 derniers). `*` dans FMessage ne supprime que la copie de l'Inbox. Sous F4HWN : pas de sauvegarde.
Raccourci : FMessage porte le numéro de raccourci d'app 15 ; sous Falcon, l'action FMESSAGE (F1, F2, M long) l'ouvre directement.

## FMessage Option (« FMessOption » dans la liste des apps)

CALLSIGN (2 caractères), RX SOUND ON/OFF, CALL SOUND (01 STWARS, 02 OURO, 03 FALCON, 04 3RDKIND, 05 AMBUL, 06 BK2FUT, 07 CANCAN, 08 ELISE, 09 GHBUST,
10 INDIJO, 11 JPARK, 12 MARIO, 13 NOKIA, 14 SPRING, 15 TELL, 16 XFILES, 17 ZELDA), KEYS INFO.
M change la valeur, F fait entendre le CALL SOUND choisi (haut-parleur seulement, sans émission), EXIT enregistre et quitte.

## Compilation

ARM GNU Toolchain 13.2 et Python 3 : `fmessage/build.sh`, puis `fmsgoption/build.sh`.

## Tests

Après compilation des deux apps :

```sh
python3 tests/test_call_sounds.py   # sons, réglages, paquets .app (ARM)
bash tests/host/run.sh              # logique de FMessage et rendu des écrans (gcc, sur l'ordinateur)
```

