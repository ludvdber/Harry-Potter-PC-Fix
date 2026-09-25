# Harry Potter PC Fix

**Français** · [English](README.en.md)

[![Build](https://github.com/ludvdber/Harry-Potter-PC-Fix/actions/workflows/build.yml/badge.svg)](https://github.com/ludvdber/Harry-Potter-PC-Fix/actions/workflows/build.yml)
[![Licence](https://img.shields.io/badge/licence-PolyForm%20Strict%20%C2%B7%20aucune%20redistribution-8b1a1a)](license)
[![Accio Launcher](https://img.shields.io/badge/Accio%20Launcher-acciolauncher.be-d6a72c)](https://acciolauncher.be/)

<!-- Images à venir : bannière, captures avant / après. -->

Un correctif PC pour les trois jeux Harry Potter qu'Electronic Arts a bâtis sur la même famille de moteur, conçu pour [Accio Launcher](https://acciolauncher.be/). C'est une seule `d3d9.dll` posée à côté de l'exécutable du jeu : le jeu la charge à la place du Direct3D 9 du système, et elle transmet tout au vrai. La même DLL sert les trois jeux et reconnaît celui qui la charge ; chaque jeu a son propre `d3d9.ini`.

| Jeu | Année | Exécutable | Réglages | Fichier de release |
|---|---|---|---|---|
| *Harry Potter et la Coupe de feu* | 2005 | `gof_f.exe` | [`data/HP4/d3d9.ini`](data/HP4/d3d9.ini) | `HP4-Goblet-of-Fire.zip` |
| *Harry Potter et l'Ordre du Phénix* | 2007 | `hp.exe` | [`data/HP5/d3d9.ini`](data/HP5/d3d9.ini) | `HP5-Order-of-the-Phoenix.zip` |
| *Harry Potter et le Prince de sang-mêlé* | 2009 | `hp6.exe` | [`data/HP6/d3d9.ini`](data/HP6/d3d9.ini) | `HP6-Half-Blood-Prince.zip` |

Le plein écran exclusif devient une fenêtre sans bordure qui survit à Alt+Tab, le clavier et la souris répondent dès le retour dans le jeu, chaque touche se change, et la résolution, le format d'image, le champ de vision et les images/s du moteur se règlent. Des effets d'image facultatifs affinent et étalonnent le rendu. Tout se règle dans `d3d9.ini`, lu une fois au lancement du jeu.

**Une base commune.** Les réglages livrés sont les mêmes pour tout le monde, pensés pour l'écran le plus répandu (1920×1080). Une interface dans Accio Launcher permettra ensuite à chacun d'adapter les siens.

---

## Ce que ça corrige

### Fenêtre, premier plan et commandes (les trois jeux)

| | Réglage | |
|---|---|---|
| Fenêtre sans bordure | `Windowed`, `WindowStyle` | À la place du plein écran exclusif, où Alt+Tab fige le jeu. Le style 1 couvre l'écran ; 2 à 4 sont des fenêtres. |
| Le jeu continue en arrière-plan | `KeepRunningInBackground` | Le moteur arrête son horloge au moindre signe qu'un autre programme passe devant. Ces signes lui sont cachés : il continue pendant que vous êtes ailleurs. |
| Clavier et souris dès le retour | `RetakeInputOnReturn` | Les jeux lisent leurs périphériques en DirectInput exclusif et ne sont jamais prévenus de leur retour au premier plan : le clavier restait mort jusqu'à 30 secondes. Le retour est détecté et chaque périphérique repris à sa lecture suivante. |
| Plus de touche bloquée après Alt+Tab | `ReleaseKeysOnReturn` | Une touche relâchée dans une autre fenêtre n'arrivait jamais au jeu, qui la croyait encore enfoncée (Harry qui marche tout seul). Elle est relâchée pour le jeu aussi. |
| Vos propres touches | `[Accio.Keys]` | N'importe quelle touche ou bouton de souris pour n'importe quelle touche du jeu, nommée comme elle est imprimée sur **votre** clavier : ZQSD en AZERTY s'écrit tel quel. *La Coupe de feu* a aussi des actions nommées (`Charm`, `Jinx`, `Accio`…) et un préréglage prêt dans son ini. |

### Dans *Harry Potter et la Coupe de feu* (`gof_f.exe`)

| | Réglage | |
|---|---|---|
| Résolution de départ | `Width` / `Height` | Le moteur démarre en 800×600 avant de lire ses options ; il démarre ici dans la taille choisie. |
| Format d'image | `AspectRatio` | Le jeu dessine en 4:3. 16:9 par défaut ici ; tout format fonctionne (16:10, 21:9, 32:9, 2.37…). |
| Champ de vision | `FOV` | Un facteur sur les 114,6° du jeu : 1.15, 1.25 ou 1.40 élargissent la vue. |
| Animations | `AnimationRate` | Les personnages sont animés à 20 images/s ; 25 ou 30 les rendent plus fluides. |
| Référence d'images/s | `FrameRateCap` | 60 à l'origine, ce qui bride le jeu ; 120 par défaut, pour qu'il suive `FPSLimit` (100). |
| Plantage au-delà de 2048 pixels | `HazeOverlay` | La brume de la Forêt interdite, du lac, du labyrinthe et du cimetière est rangée dans un tableau de 129 colonnes de 16 pixels : un écran plus large le fait déborder et le jeu plante. `1` la dessine avec un nombre de colonnes plafonné (colonnes plus larges, même brume) ; `0`, le défaut pour l'instant, la retire comme l'ancien correctif. |

### Dans *Harry Potter et l'Ordre du Phénix* (`hp.exe`)

| | Réglage | |
|---|---|---|
| Résolution de départ | `Width` / `Height` | Le moteur démarre en 640×480 avant de lire ses options ; il démarre ici dans la taille choisie. |
| Format d'image | `AspectRatio` | Le 16:9 du jeu, remplaçable par n'importe quel format (16:10, 21:9, 32:9, 2.37…). |
| Champ de vision | `FOV` | Un facteur sur la vue du jeu : 1.15, 1.25 ou 1.40 l'élargissent. |
| Limite de 30 images/s levée | `UnlockFrameRate` | Le jeu démarre avec un intervalle de présentation de 2 (30 images/s) ; il passe à 1. |
| Plafond d'images/s | `FrameRateCap` | Le plafond que le moteur garde en mémoire et remet parfois à zéro, tenu à 120. |

### Dans *Harry Potter et le Prince de sang-mêlé* (`hp6.exe`)

| | Réglage | |
|---|---|---|
| Résolution de départ | `Width` / `Height` | Le moteur démarre en 640×480 avant de lire ses options ; il démarre ici dans la taille choisie. |
| Format d'image | `AspectRatio` | Le 16:9 du jeu, remplaçable par n'importe quel format (16:10, 21:9, 32:9, 2.37…). |
| Champ de vision | `FOV` | Un facteur sur la vue du jeu : 1.15, 1.25 ou 1.40 l'élargissent. |
| Limite de 30 images/s levée | `UnlockFrameRate` | Le jeu démarre avec un intervalle de présentation de 2 (30 images/s) ; il passe à 1. |
| Plafond d'images/s | `FrameRateCap` | Le plafond que le moteur garde en mémoire et remet parfois à zéro, tenu à 120 (sauf quand le moteur le met lui-même à 0). |

Chaque modification est faite dans l'exécutable une fois chargé, jamais sur le disque, et seulement là où les octets attendus sont trouvés : une autre version du jeu tourne simplement sans changement, et le journal le dit.

### Image

- *Harry Potter et la Coupe de feu* : tous les effets d'image sont désactivés par défaut : aucun n'a encore été réglé pour ce jeu.
- *Harry Potter et l'Ordre du Phénix* : les effets d'image sont activés par défaut, avec les valeurs livrées depuis la première version du correctif.
- *Harry Potter et le Prince de sang-mêlé* : tous les effets d'image sont désactivés par défaut : aucun n'a encore été réglé pour ce jeu.

| | Réglage | |
|---|---|---|
| Mipmaps pour toutes les textures | `GenerateMipmaps`, `ForceTrilinear` | La plupart des textures sont livrées sans copies réduites pour le lointain, d'où le scintillement et le flou à distance. Elles sont construites au chargement de chaque texture. |
| MSAA | `Antialiasing` | Abaissé pas à pas (16, 8, 4, 2, rien) jusqu'à ce que la carte graphique l'accepte, au lieu de se couper. |
| Filtrage anisotrope, netteté des textures | `AnisotropicFiltering`, `TextureLODBias` | Forcés sur toutes les textures. |
| FXAA et accentuation | `FXAA`, `Sharpness` | Une passe sur l'image finie ; elle porte aussi les effets ci-dessous. |
| Étalonnage des couleurs | `ColorGrading` et les valeurs dessous | Noirs, gain, gamma, balance des blancs, contraste, vibrance, virage partiel, vignettage. |
| Occlusion ambiante | `SSAO` et ses valeurs | Dessinée dès que la scène 3D est finie, avant les menus et sous-titres. |
| Halo et rayons de lumière | `Bloom`, `GodRays` | En demi-résolution, estompés sur les menus et les écrans blancs. |
| Suréchantillonnage | `SSAAFactor` | Rendu 2 à 4 fois plus grand. Très gourmand. |
| Taille de rendu | `RenderWidth`, `RenderHeight` | 0 = la taille choisie dans le jeu ; -1 = celle de l'écran. |

Et aussi : une touche de capture d'écran (`ScreenshotKey`, F12 par défaut, PNG dans `screenshots`), un compteur d'images (`ShowFPS`), une limite d'images/s (`FPSLimit`), une seule image d'avance chez le pilote au lieu de trois (`MaxFrameLatency`), et `DPIAware` pour les écrans à haute densité.

---

## Installation

**Avec [Accio Launcher](https://acciolauncher.be/)** : rien à faire, chaque jeu arrive avec son correctif.

**À la main** : prenez le zip de votre jeu dans une [release](https://github.com/ludvdber/Harry-Potter-PC-Fix/releases) et copiez `d3d9.dll` et `d3d9.ini` à côté de l'exécutable du jeu. Les fichiers laissés par d'anciens correctifs (`d3d9_original.dll`, `fps.dll`) peuvent être supprimés : plus rien ne les charge.

**Sous Linux** (Wine ou Proton), Wine utilise son propre `d3d9` sauf indication contraire : `WINEDLLOVERRIDES="d3d9=n,b"`. Accio Launcher le fait pour vous.

Les réglages sont lus au lancement : modifiez `d3d9.ini`, puis relancez le jeu. Chaque ligne du fichier est commentée. Un `d3d9.ini` écrit pour un ancien correctif fonctionne encore : les clés absentes des sections `[Accio.*]` sont lues là où les anciennes versions les rangeaient.

---

## En cas de problème

Chaque lancement écrit `d3d9_accio.log` à côté de l'exécutable du jeu (`Log=0` le désactive). On y lit le jeu reconnu, chaque modification faite ou sautée, ce que Direct3D a accepté (fenêtre, taille d'image, MSAA, profondeur lisible), les départs et retours au premier plan, et la reprise du clavier et de la souris. Joignez-le à tout [signalement de bug](https://github.com/ludvdber/Harry-Potter-PC-Fix/issues/new/choose).

---

## Compiler

Visual Studio 2022 (charge de travail C++ Desktop), Win32 uniquement : les jeux sont en 32 bits.

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
    build\accio-fix.sln /p:Configuration=Release /p:Platform=Win32 /v:minimal
```

Sortie : `data\d3d9.dll`. Sans aucun jeu, `tests\run_keys_test.bat` vérifie la réassignation des touches et `tests\run_settings_test.bat` relit les trois `d3d9.ini` (et un fichier à l'ancien format) avec le lecteur de la DLL. Les trois ini sont générés par `python tools\make_ini.py` ; le build échoue s'ils ne correspondent plus.

| Chemin | Rôle |
|---|---|
| `source/main.cpp` | Point d'entrée, exports, chargement du Direct3D 9 du système |
| `source/settings.cpp` | Lecture de `d3d9.ini` |
| `source/hooks.cpp` | Tables de méthodes, tables d'imports, octets de l'exécutable |
| `source/game.cpp` | Ce qui est modifié dans chaque jeu, et où |
| `source/window.cpp` | Style et position de la fenêtre, premier plan |
| `source/input.cpp` | DirectInput : retour au premier plan, touches bloquées |
| `source/keys.cpp` | `[Accio.Keys]` |
| `source/direct3d.cpp` | Création et réinitialisation du périphérique, textures, profondeur, échantillonneurs |
| `source/present.cpp` | À chaque image : effets, compteur, captures, limite d'images/s |
| `source/effects.cpp`, `shaders.h` | Effets d'image |
| `source/version.h` | Version inscrite dans la DLL (doit correspondre à `VERSION`) |
| `data/HP4`, `data/HP5`, `data/HP6` | Le `d3d9.ini` de chaque jeu |
| `tools/make_ini.py` | Génère ces trois fichiers |

### Depuis GitHub, sans rien installer

**Build automatique.** Chaque push et chaque pull request lance le workflow **Build** : compilation, tests, vérification que la DLL est bien une DLL 32 bits, que les trois ini sont à jour et que la version est la même dans `VERSION` et `source/version.h`. La DLL : **Actions** → **Build** → le run → **Artifacts** → `d3d9-win32`.

**Build d'essai (rien n'est publié).** **Actions** → **Release** → **Run workflow**, case **Créer la release** décochée. Une fois le run vert : **Artifacts** → `release-v<VERSION>` (la DLL et un zip par jeu).

**Créer une release.**

1. Changer le numéro dans `VERSION` **et** dans `source/version.h` (`ACCIO_VERSION_NUM` et `ACCIO_VERSION_STR`), dans le même commit.
2. **Actions** → **Release** → **Run workflow** → cocher **Créer la release** → **Run workflow**.
3. Une fois le run vert : **Releases** → le brouillon `v<VERSION>` → relire → **Edit** → **Publish release**. Rien n'est public avant ce clic.

Chaque fichier de release porte une attestation de provenance signée : `gh attestation verify d3d9.dll --repo ludvdber/Harry-Potter-PC-Fix`.

---

## Licence

© 2026 Accio Launcher. **Ce correctif n'est pas libre** : [PolyForm Strict 1.0.0 avec une permission supplémentaire](license).

- ✅ Vous pouvez lire le code, le forker sur GitHub, le modifier et le compiler **pour votre usage personnel**.
- ❌ Vous ne pouvez pas le redistribuer, modifié ou non, compilé ou en source, en tout ou en partie : ni sur un site de mods (Nexus Mods, ModDB…), ni sur un hébergeur de fichiers, un forum, un serveur Discord, un Patreon, un repack ou un autre launcher, gratuitement ou non.
- ❌ Vous ne pouvez pas le vendre, le mettre derrière un paiement, un abonnement ou une publicité, ni utiliser les noms « Accio Launcher » ou « Harry Potter PC Fix » pour une copie.

Les seules sources officielles sont [Accio Launcher](https://acciolauncher.be/) et les releases de ce dépôt. Une copie trouvée ailleurs n'est pas la nôtre : elle peut être modifiée, et elle sera signalée pour suppression.

Les en-têtes du SDK DirectX de Microsoft, dans `source/dxsdk`, gardent leur propre licence ([avis](THIRD_PARTY_NOTICES.md)). Les jeux et leurs fichiers appartiennent à leurs propriétaires (Electronic Arts, Warner Bros.) ; ce projet ne revendique aucun droit sur eux et n'en distribue aucun.

Ce projet vous est utile ? [Un café sur Ko-fi](https://ko-fi.com/ludovic01) le fait avancer.
