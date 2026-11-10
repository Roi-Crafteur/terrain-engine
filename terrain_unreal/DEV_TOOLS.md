# Les outils de debug en jeu (agent 36)

Placer la caméra, lire l'altitude et comparer deux versions sans écrire de script : un écran F3,
des commandes `Terrain.*`, un fichier de lieux commun, des modes de vue, une page de
performance et le changement de seed sans quitter le Play.

**Jamais dans un build de jeu final** : `TerrainEngine.Build.cs` met `TERRAIN_DEV_TOOLS` à 0 en
Shipping, tout le code des outils est sous `#if TERRAIN_DEV_TOOLS` (vérifié par le test
`unreal_dev_tools_guard`). Dans les autres builds, la case `bDevTools` de `Terrain World`
(catégorie `Terrain|Dev`, cochée par défaut) les active ou non.

## Qui décide quoi

| Sujet | `terrain_core` et le Bridge (données, déterministe, testé par ctest) | Unreal (affiche, fait jouer) |
|---|---|---|
| Infos d'un point | `Bridge/point_info` lit le core : sol, eau (type, niveau, profondeur), relief avant les fleuves, biome, matériau, climat, case et climat de la planète, vallée et lac, chute la plus proche, distance à la côte, LOD de la tuile affichée, chunk, bloc | l'écran F3, `Terrain.Info` |
| Point visé | `Bridge/ground_ray` : marche du rayon sur le sol du core jusqu'à 30 km ; « hors heightmap » quand le point touché s'écarte du relief | trace sur les tuiles qui ont une collision (LOD 0, ~64 m) d'abord |
| Lieux | `terrain_core/dev/places_file` : le fichier des lieux, sur le JSON strict de `terrain/json` (#56) ; `Bridge/place_pose` : yaw = cap − 90, œil selon `eyeMode` | `Terrain.Goto`, `Look`, `Eye`, `Save`, `Pawn` ; le pion volant des outils |
| Images | `Bridge/shot_waiter` : quand tout est chargé, délai maximum, noms, `views.txt` | `FScreenshotRequest`, l'enchaînement de `Terrain.Views` |
| Modes de vue | `Bridge/debug_colors` : couleurs par LOD, pente, normale, sommets hors heightmap | couleurs de sommets, matériau de couleurs de sommets d'Unreal, filaire, sections cachées |
| Performance | `Bridge/dev_perf` : fenêtre glissante ; `TileTimings::totals()` | FPS, temps CPU et GPU, mémoire |
| Seed | `Bridge/planet_job` : quel fichier planète, construit à part, jamais celui de l'utilisateur | le monde du Play refait |

## La touche F3

F3 passe d'une page à la suivante : rien → **joueur** → **point visé** → **performance** →
rien (`Terrain.Hud 0|1|2|3` pour une page donnée). Les libellés sont en anglais, comme le log.

- **Joueur** : l'œil (x, z du moteur, altitude, hauteur au-dessus du sol ou de l'eau), cap,
  inclinaison, pion (celui du jeu ou celui des outils), puis les lignes du core sous l'œil.
- **Point visé** : la distance et ce qui est touché (une tuile, ou le sol du core au-delà des
  tuiles qui ont une collision), puis les lignes du core au point, dont « on / off the
  heightmap » (hors heightmap : roche 3D, grotte ou modification).
- **Performance** : images par seconde, image, game thread, render thread et GPU (moyenne et
  maximum sur 5 s), par LOD les tuiles générées par seconde et leurs temps moyens de génération,
  conversion et envoi, les maillages d'eau, les tuiles affichées, en attente et chargées,
  l'horizon (en calcul ou fini, sa mémoire), la mémoire du processus, le coût de l'écran, et la
  préparation d'une planète en cours.

En build non Shipping, F3 lance aussi `viewmode lit` (touche de debug d'Unreal) : les outils
redemandent leur mode de vue à l'image suivante.

## Les commandes

| Commande | Effet |
|---|---|
| `Terrain.Goto x z` | la vue à 1,8 m au-dessus du sol (ou de l'eau) en (x, z), même cap |
| `Terrain.Goto x z 182.03` | l'œil à l'altitude 182,03 m du core |
| `Terrain.Goto x z +25` | l'œil à 25 m au-dessus du sol ou de l'eau |
| `Terrain.Goto <lieu>` | la vue d'un lieu du fichier (position, cap, inclinaison, œil) ; avertit si le lieu a été mesuré avec le fichier planète et que le monde n'en a pas, ou sans lui et que le monde l'a |
| `Terrain.Look cap inclinaison` | le cap en degrés depuis le nord, sens horaire ; l'inclinaison de −90 à 90, positive vers le haut, **négative vers le bas** (`Terrain.Look 0 -45` : vers le nord, 45° sous l'horizon) |
| `Terrain.Eye 1800` / `Terrain.Eye +2` | l'œil à cette altitude, ou à cette hauteur au-dessus du sol ou de l'eau |
| `Terrain.Pawn` | le pion du jeu rendu, posé sur le sol praticable le plus proche de la vue (50 m au plus), vérifié sur la collision et surveillé une seconde ; refusé au-dessus de l'eau ou sans sol praticable (message à l'écran) |
| `Terrain.Save <nom> [note…]` | la vue écrite dans le fichier des lieux (met à jour un lieu du même nom, ses champs inconnus et sa note gardés) |
| `Terrain.Info` / `Terrain.Info here` | les lignes du point visé / de la colonne de l'œil dans le log |
| `Terrain.Shot <nom>` | une image `Saved/TerrainEngine/Shots/<nom>.png` une fois tout chargé |
| `Terrain.Views <lieux>` | une image par lieu (`chute388_bassin k2_paroi`, `*`, `chute388_*`) dans `Saved/TerrainEngine/Views/<date>/` avec `views.txt` |
| `Terrain.View <mode>` | le mode de vue (ci-dessous) |
| `Terrain.Blend 0\|1` | (outils de dev seulement) les poids des biomes de l'horizon et du niveau 0 sous les tuiles (agent 29) : 0 le biome dominant de chaque échantillon, comme avant eux ; comparer leur coût GPU et leur aspect (après avoir relancé les scripts des matériaux de l'horizon GPU et temporaire) |
| `Terrain.Param <nom> <valeur>` | (outils de dev seulement) un paramètre scalaire du rendu posé sur les matériaux de l'horizon GPU et des tuiles (leur sol et leur eau, agent 40), pour dire dans le jeu quelle part d'un matériau donne ce que montre l'écran : `DebugView 7` (la normale de chaque pixel), `DebugView 6` (les bancs de la roche), `DetailNormals 0` (les normales des grilles seules), `MacroVariation 0`, `PatchAmount 0`, `SmoothNormal 1`, `RockGrey 0`, `SkyReflection 1`… ; les matériaux sans ce paramètre l'ignorent, le log dit sur combien il est posé. Les paramètres que le plugin règle lui-même (placement des niveaux, apparition, surface du niveau 0 sous les tuiles) sont remis par lui : seuls ceux du rendu restent. Valeurs par défaut : celles des scripts des matériaux (`DebugView 0`, `DetailNormals 1`, `MacroVariation` et `PatchAmount` de `temporary_terrain_look.py`) |
| `Terrain.Sun <élévation> <cap>` | (outils de dev seulement, agent 40) le soleil à cette élévation au-dessus de l'horizon et à ce cap (degrés depuis le nord, sens horaire, comme les lieux) : chaque `DirectionalLight` mobile du monde tournée à `(−élévation, cap + 90, 0)`, comme `verify.py --sun élévation,yaw` (yaw = cap + 90) ; sans argument, le log donne le soleil de la première, avec sa valeur de `--sun`. Une lumière qui n'est pas mobile reste telle quelle (le log le compte) |
| `Terrain.HorizonShadows <0 à 4>` | (outils de dev seulement, agent 40) les niveaux de l'horizon GPU qui portent des ombres, du plus fin : 0 aucun (comme avant), n les niveaux 0 à n − 1 (4 : jusqu'à 8 km) ; la valeur au démarrage vient de `HorizonDrawing.ShadowLevels` du Terrain World (`TerrainHorizonDrawing.h`), `bHorizonShadows` les fait tous porter ; pour comparer leur coût (`stat gpu`) et leur aspect |
| `Terrain.Seed <n> [surface\|caves\|regions]` | le monde d'une autre seed et d'un autre preset, sans quitter le Play |
| `Terrain.Hud [0-3]` | la page de l'écran F3 |

Variables console : `Terrain.PlacesFile` (un autre fichier des lieux ; vide : celui du dépôt),
`Terrain.ShotTimeout` (attente maximum d'une téléportation ou d'une image, 60 s),
`Terrain.ShotSettle` (stabilité avant une image, 1 s), `Terrain.ShotShowUI` (0 : le render target
de la vue en pixels physiques ; 1 : le viewport avec son UI, sans les panneaux de l'éditeur ni
ses 3 lignes de bordure, ou la fenêtre entière si Slate ne peut pas le dessiner seul),
`Terrain.PawnRadius` (rayon de la
recherche du sol praticable de `Terrain.Pawn`, 50 m).

### La téléportation : le contrat de `TeleportView`

`Terrain.Goto`, `Terrain.Eye`, `Terrain.Views` et la carte de dev (agent 35) passent par la même
fonction, sur `ATerrainWorld` (`Public/TerrainWorld.h`, appelables en Blueprint, catégorie
`Terrain|Dev`). Elle passe elle-même par **la seule téléportation du monde**,
`ATerrainWorld::BeginTeleport` (le chargement de l'agent 38, `TerrainWorldLoadingPhase.cpp`,
`LOADING.md`), comme `TeleportWhenReady` : un seul chemin.

```cpp
bool TeleportView(double X, double Z, double Heading, double Pitch, double Eye,
    ETerrainEyeMode EyeMode);
bool IsViewLoading() const;
bool ReturnToGamePawn();
```

Les entrées :

- `X`, `Z` : le point du core, en mètres (x vers l'est, z vers le sud), de n'importe quelle image
  du monde : la vue va à l'image la plus proche de la vue actuelle (les tuiles la suivent).
- `Heading` : le cap en degrés depuis le nord (−Z du core, −Y d'Unreal), sens horaire ; toute
  valeur est ramenée dans [0, 360). Yaw Unreal = cap − 90.
- `Pitch` : l'inclinaison en degrés, **positive vers le haut, négative vers le bas** (−45 : 45°
  sous l'horizon), ramenée dans [−89,9, 89,9]. C'est la convention de `FRotator` : la rotation
  posée est `FRotator(Pitch, Heading − 90, 0)`, sans roulis (celle de `TeleportWhenReady`,
  `viewAngles` ; `Bridge/place_pose` : `viewRotation` pour `Terrain.Look`, testée par
  `unreal_place_pose_test`). Les lieux, `Terrain.Look` et `Terrain.Save` suivent la
  même convention ; à l'essai, `Terrain.Look 0 -45` regarde vers le nord, 45° sous l'horizon.
- `Eye`, `EyeMode` :
  - `Altitude` : l'œil à l'altitude `Eye` du core, en mètres, sans contrôle (il peut être sous
    le sol ou sous l'eau) ;
  - `Ground` : l'œil à `Eye` mètres au-dessus du haut de la colonne : **la surface de l'eau**
    quand le core y en met (la mer toujours ; les lacs, les fleuves et les lacs salés seulement
    avec le fichier planète), sinon le sol du heightmap (ni surplomb ni grotte).

Elle rend la main tout de suite (≈ 0,2 ms : rien n'est généré ni lu sur le fil du jeu). Quand elle
rend `true` :

1. le joueur possède le **pion volant des outils** (un `ASpectatorPawn`, 50 m/s, collision
   `Spectator`, créé au premier appel) ; le pion du jeu reste où il était, non possédé, gardé
   pour `ReturnToGamePawn`. Le tick du monde ne génère pas le sol de ce pion d'un coup
   (`WarmUpAround`) : il vient avec le chargement ;
2. **l'écran passe au noir** tout de suite, le pion est déplacé en (x, z) et **tenu** (son
   mouvement coupé), le cap et l'inclinaison posés sur le contrôleur ;
3. l'œil : `Altitude`, posé tout de suite à son altitude ; `Ground`, posé à `Eye` m au-dessus du
   sol ou de l'eau dès que **le sol du point est lu sur un fil** (`the ground of (x, z) m read on
   a thread in … ms`). Puis l'œil reste **exactement** là : jamais posé sur le sol, jamais remonté
   au relâchement, jamais déplacé par `bSnapPawnToGround` ;
4. le pion est relâché quand un rayon trouve la collision des tuiles sous lui ; **l'écran
   revient** quand le sol, l'anneau de 64 m, toutes les tuiles et les niveaux 0 à 3 de l'horizon
   sont affichés (2 s au plus, ce qui manquait est dans le log). `IsViewLoading()` est vrai tant
   que l'écran est noir ou le pion tenu ;
5. un nouvel appel remplace le chargement du précédent. Un changement d'œil sur place
   (`Terrain.Eye`) donne donc un noir très bref (≈ 0,25 s de retour).

Elle rend `false` sans rien changer : sans les outils (`bDevTools` faux, build Shipping), hors
Play, pendant la construction du monde (le début du Play ou `Terrain.Seed`), quand le monde est
arrêté sur une erreur, ou sans joueur.

`ReturnToGamePawn()` (et `Terrain.Pawn`) rend le pion du jeu, posé sur le **sol praticable le
plus proche** de la colonne sous la vue, le cap et l'inclinaison gardés, les tuiles autour
générées. Deux étapes (`Bridge/walkable_ground`, `TerrainDevPawn.cpp`) :

1. **Les candidats, sur la heightmap** (`walkableCandidates`) : la colonne même, puis des
   anneaux tous les 2 m jusqu'à `Terrain.PawnRadius` (50 m), chacun parcouru depuis le nord dans
   le sens horaire. Un candidat est une colonne sèche dont la pente est sous l'angle de marche du
   `CharacterMovement` du pion moins 5° (44,765° par défaut, donc 39,8°). La pente est la plus
   forte des quatre pentes à 1 m autour du point. On garde l'anneau le plus proche d'abord, et
   sur un anneau la pente la plus douce d'abord (64 candidats au plus). Mêmes hauteurs, mêmes
   candidats.
2. **Chaque candidat, sur la collision réelle des tuiles** (`checkStand`) : cinq rayons vers le
   bas, au centre et aux quatre bords de la capsule, de 20 m au-dessus à 20 m sous la heightmap.
   Il est refusé si :
   - un rayon ne touche rien (tuiles pas encore chargées) ;
   - une normale est plus raide que l'angle de marche moins 5° ;
   - les impacts sont plus éloignés que la hauteur de marche (`MaxStepHeight`, 45 cm) ;
   - l'impact du centre est à plus de 2 m de la heightmap (une falaise, un surplomb ou un creux
     taillés par le SDF : la collision n'y est plus le sol que la heightmap décrit) ;
   - l'impact est **sous la surface de l'eau du core** à 3 m autour.

Le pion est posé sur le premier candidat accepté, 2 cm au-dessus de l'impact le plus haut, à
vitesse nulle, en `MOVE_Walking`, sans recalage sur la heightmap (`bSnapPawnToGround` coupé pour
la pose). Puis il est **surveillé** :

- deux images plus tard, il doit être au sol (`IsMovingOnGround`) ;
- une seconde plus tard, il ne doit pas avoir bougé de plus de 0,5 m (son mouvement est bloqué
  pendant cette seconde) ;
- sinon, c'est un glissement : il passe au candidat suivant, avec un message, et ainsi de suite
  jusqu'à 8 essais. Après le dernier, un message en donne la raison
  (« slid from the 8 spots tried near the view (the last: moved 3.2 m in a second) »).

Quand le point n'est pas sous la vue, un message le dit à l'écran (8 s) et dans le log
(« the ground under the view does not hold the pawn (72 degrees on the heightmap): the pawn of
the game is put 34 m away », sous la paroi de `k2_paroi`).

Les candidats lisent **la heightmap** du core : ni surplomb ni paroi de la roche 3D. La collision
corrige ce qui est vu de près. Quand la roche 3D de l'agent 20 (12A) sera sur `main`, il faudra
lire la colonne réelle.

Elle rend `false` sans rien changer, avec un message à l'écran et dans le log, la vue des outils
restant en place :

- **au-dessus de l'eau** (la colonne sous la vue a de l'eau au-dessus de son sol : mer, lac,
  fleuve, lac salé) : le pion du jeu n'est jamais posé au fond (« over water (sea, lake or
  river): … move over dry ground first ») ;
- **sans candidat** dans `Terrain.PawnRadius`, avec la raison de `walkableCandidates` :
  `NoDryGround` (« no dry ground: water everywhere around ») ou `TooSteep` (« the dry ground
  around is too steep ») ;
- **sans candidat qui tienne sur la collision** : le message compte les refus par raison (trop
  raide, marche, loin de la heightmap, sous l'eau, sans collision) ;
- **pendant le chargement de la vue** (`IsViewLoading()`) : il tiendrait le pion et le remettrait
  sur la heightmap au relâchement ; `Terrain.Pawn` une fois la vue affichée ;
- quand le joueur n'a pas le pion des outils (il a déjà celui du jeu).

Pour l'agent 35 (clic sur la carte) : `walkableCandidates` rend aussi sa raison. Avec
`NoDryGround`, poser la vue volante à 1,8 m au-dessus de l'eau du core (`TeleportView` en
`Ground`, œil 1,8).

`Terrain.Views` rend le pion du jeu là où il était, sans le poser : il n'est pas concerné.

Le fondu au noir et l'attente sont ceux du chargement de l'agent 38 : rien à envelopper. Attention
en relisant une rotation : Unreal garde
l'inclinaison de la caméra dans [0, 360) (−59 se relit 301) ; les outils la ramènent dans
[−90, 90] (`cameraPose`). Un code qui la borne sans la ramener obtient +90 : du ciel pur.

### Les images

Une image est prise quand rien ne reste à charger autour de la vue : l'écran sorti du noir du
chargement et le pion relâché, toutes les tuiles de la sélection affichées, aucune en attente,
l'horizon GPU affiché et fini (époque et détail), aucune modification en cours, et cela depuis `Terrain.ShotSettle` secondes (l'exposition et la lumière
suivent) ; au plus `Terrain.ShotTimeout` secondes, sinon l'image est prise quand même et marquée
`NOT READY (timeout)`. L'image a la taille de la fenêtre du Play : **garder la même taille de
fenêtre** pour comparer deux séries.

- **L'écran F3 n'est pas dans les images** : `Terrain.Shot` et `Terrain.Views` le cachent pendant
  la capture. `HighResShot` ne le montre pas non plus (`UDebugDrawService` n'y dessine pas) :
  seul `shot showui` prend une image avec F3 (utile pour garder les lignes d'un point avec sa
  vue).
- **Comment l'image est lue** : par défaut (`Terrain.ShotShowUI 0`), les outils lisent le render
  target de la vue en entier, **en pixels physiques**, deux images après avoir caché F3 et les
  messages de l'écran. La capture sans UI d'Unreal lisait un rectangle de la taille du viewport :
  avec Windows à 200 %, le viewport de l'éditeur compte ses pixels à l'échelle du bureau (1920 ×
  1080) alors que son render target a les pixels physiques (3840 × 2160), et l'image ne gardait
  que le quart haut-gauche de la vue (l'horizon d'une vue à plat au bord du bas).
  `Terrain.ShotShowUI 1` prend le viewport avec son UI (F3 caché, les messages de l'écran
  gardés), comme Slate dessine son widget, à la taille du viewport en pixels physiques : sans les
  panneaux de l'éditeur, que `shot showui` prend avec lui (la fenêtre entière, 3 840 × 2 064).
  Les 3 lignes du haut du widget, sa bordure (une ligne gris foncé, puis un trait bleu de 2
  lignes dans l'éditeur), sont coupées : l'image a la largeur du viewport et 3 lignes de moins,
  ce que disent le log et `views.txt` (« its top 3 rows cut »).
  Si Slate ne peut pas dessiner le viewport seul, un `WARNING` le dit et la fenêtre entière est
  prise par Unreal (méthode `whole window`, sa taille relue dans le PNG). Pendant toute la
  prise, la souris ne tourne plus la vue, et la pose est reposée juste avant la capture.
- **Contrôle dans `views.txt`** : pour chaque image, la caméra réelle au moment de la capture (cap,
  inclinaison, FOV, lus sur le `PlayerCameraManager`), la taille de l'image, du viewport et de son
  render target, et la méthode ; `WARNING` si la caméra s'écarte de la pose de plus de 0,5° ou si
  l'image n'a pas la taille de la vue.
- **Pas de flou de mouvement** : de la commande jusqu'à la dernière image de la série,
  `ShowFlag.MotionBlur 0`, puis `ShowFlag.MotionBlur 2` (le réglage par défaut de la fenêtre,
  aussi quand la série s'arrête autrement, ou au Stop). Chaque téléportation est en plus une
  coupure de caméra (`SetGameCameraCutThisFrame`) : le flou et l'historique temporel repartent de
  la nouvelle vue.

`views.txt` : la seed, le preset, le fichier planète, les anneaux de LOD, puis une ligne par vue
(fichier, lieu, x, z, œil (celui de la caméra à la capture), yaw, inclinaison, prête ou non et
après combien de secondes, tuiles en attente et affichées, horizon, images par seconde, et
« WITHOUT the planet file it was measured with » quand le lieu a été mesuré avec le fichier
planète et que le monde ne l'a pas, ou « WITH the planet file, measured without it » dans le cas
inverse).

## Les modes de vue (`Terrain.View`)

| Mode | Ce qu'on voit |
|---|---|
| `normal` | le jeu tel quel |
| `filaire` (`wireframe`) | le filaire d'Unreal, tuiles et horizon |
| `lod` | les tuiles par LOD (0 rouge, 1 jaune, 2 vert, 3 bleu), les niveaux de l'horizon GPU teintés |
| `roche` (`rock`) | les tuiles en gris, en orange les sommets hors heightmap (roche 3D, grottes, modifications) ; horizon caché |
| `eau` (`water`) | l'eau seule : le sol des tuiles et l'horizon cachés |
| `seche` (`dry`) | sans l'eau (comme `Terrain.Water 0`) : comparer le coût GPU de l'eau |
| `normales` (`normals`) | les tuiles colorées par leur normale (X rouge, Y vert, Z bleu) ; horizon caché |
| `pente` (`slope`) | les tuiles par pente : vert < 15°, jaune-vert < 30°, jaune < 45°, orange < 60°, rouge < 80°, magenta jusqu'à la verticale, bleu en surplomb ; horizon caché |

Les couleurs passent par `UpdateMeshSection` (les couleurs seules, aucune collision recuite) et le
matériau de couleurs de sommets d'Unreal ; chaque tuile retrouve ses couleurs et son matériau en
mode `normal`.

## Le fichier des lieux

`data/dev/places.json` à la racine du dépôt (format commun des agents 35, 36 et 37), lu et écrit
par `terrain_core/dev/places_file` :

```json
{ "version": 1,
  "places": [
    { "name": "chute388_bassin", "x": -6185104, "z": 155924,
      "heading": 227, "pitch": 30, "eye": 182.03, "eyeMode": "altitude",
      "planet": true, "note": "chute de 388 m vue depuis son bassin" } ] }
```

- `x`, `z` : mètres du moteur ; `heading` : cap en degrés depuis le nord (−Z), sens horaire,
  dans [0, 360) (yaw Unreal = cap − 90) ; `pitch` : degrés, positif vers le haut, négatif vers
  le bas, dans [−90, 90] ; `eye` : altitude du core (`eyeMode` `altitude`) ou hauteur au-dessus
  du sol ou de l'eau (`ground`, ≥ 0) ; `planet` : le lieu a été mesuré avec le fichier planète
  (`true` : ses fleuves, chutes et lacs, le sol que son hydrologie creuse et remplit, le climat
  des biomes) ou sans lui (`false`) ; `Terrain.Goto` et `Terrain.Views` avertissent quand le
  monde n'est pas dans le même cas, car la vue n'y est plus la même ; `Terrain.Save` écrit si le
  monde a le fichier ; `note` : facultative.
- Lu et écrit par `terrain_core/dev/places_file` sur le lecteur JSON strict de `terrain/json`
  (PR #56, le même que celui du peuplement) : mêmes bits sur toutes les plateformes ; chaque
  nombre est un décimal exact m·10^e (m ≤ 2^53, |e| ≤ 22, donc au plus 15 à 16 chiffres).
- Lecture stricte : une erreur de syntaxe dit le fichier, sa ligne et sa colonne
  (« data/dev/places.json:4:9: expected ',' or '}' ») ; une erreur de contenu dit le lieu et le
  champ fautifs (« data/dev/places.json: place 3 ("k2_paroi"): "pitch" must be in [-90, 90]
  degrees »). Les champs inconnus (ceux d'un autre agent ou d'un autre outil), dans un lieu ou
  autour des lieux, sont gardés et réécrits tels quels, dans leur ordre, en JSON compact
  (sans espaces). Écriture canonique (la mise en page ci-dessus) : `Terrain.Save` ne change que
  le lieu qu'il écrit.
- `Terrain.Save` sur un lieu qui existe déjà : seuls les champs connus (`x`, `z`, `heading`,
  `pitch`, `eye`, `eyeMode`, `planet`) changent ; ses champs inconnus restent au bit près, à
  leur place, et sa note reste si la commande n'en donne pas.
- Les nombres relus sont les mêmes au bit près ; `Terrain.Save` arrondit au centimètre et au
  centième de degré.

| Lieu | x | z | cap | incl. | œil | planète | note |
|---|---|---|---|---|---|---|---|
| `chute388_bassin` | −6 185 104 | 155 924 | 227 | 30 | 182,03 m (altitude) | oui | la chute de 388 m depuis son bassin |
| `chute388_levre` | −6 185 411 | 156 218 | 46 | −30 | +2 m | oui | sa lèvre (eau à 568,99 m), vers le bassin |
| `chute388_mer` | −6 184 762 | 155 681 | 230 | 20 | +2 m | oui | la chute vue de l'aval, à 842 m de sa lèvre |
| `k2_paroi` | 416 102 | −11 414 891 | 225 | 35 | +25 m | oui | la plus haute paroi près du K2 (pied 416 017 ; −11 414 806, 420 m) |
| `falaise_cotiere` | 4 944 509 | −8 706 669 | 225 | 21,4 | +2 m | oui | montagne qui plonge dans la mer (55° en moyenne) en attendant les falaises marines de l'agent 33 : 0 m au rivage, 557 m à 400 m, 1 402 m à 1 km ; vue de la mer à 500 m du rivage, le pied et le sommet dans le cadre |
| `montagnes_mer` | 1 430 074 | 2 052 007 | 133 | 5 | +2 m | oui | des montagnes qui plongent dans la mer, au milieu de l'image (rivage de 84 à 182°, 165 m au plus près) |
| `cote_froide` | 3 624 471 | −10 006 796 | 120 | 5 | +2 m | oui | une côte froide vue de la mer |
| `plage_origine` | 160 | 0 | 273 | 0 | +1,8 m | oui | la plage à 160 m de l'origine, la mer sur toute la vue (peu profonde : 0,12 m d'eau à 50 m, 2 m à 1 km) ; l'œil au-dessus du sable sec |
| `fleuve_plaine` | −241 631 | −86 277 | 5 | 0 | +2 m | oui | un fleuve de plaine (eau à 3,28 m), vers l'aval |
| `lac_montagne` | 7 369 228 | −7 088 911 | 180 | 5 | +2 m | oui | un lac de montagne (eau à 1 725,26 m) |
| `lisiere_tropicale_savane` | −26 927 500 | −270 336 | 265 | −5 | +40 m | **non** | la lisière forêt tropicale / savane à 450 m vers l'ouest, au-dessus des arbres ; **sans** fichier planète seulement (avec lui, savane partout) |

### Les lieux et le fichier planète

Unreal charge le fichier planète (la carte de test a `PlanetFile` par défaut) : tous les lieux
sont mesurés avec lui (`planet.bin` de la seed 20261019, somme `26626564570b0d9e`), sauf
`lisiere_tropicale_savane`, qui n'existe que sans lui. Mesures du 1er novembre 2026 sur `main` :

- **Le même sol avec et sans la planète** (aucun écart de hauteur, au pas de 10 ou 20 m) à
  8 km du pied de `k2_paroi`, à 5 km de `montagnes_mer` et de `cote_froide`, à 2 km du rivage de
  `falaise_cotiere` ; leurs caps restent bons dans les deux cas.
- **`chute388_*`, `fleuve_plaine`, `lac_montagne`** (mesurés avec la planète dès le début) : la
  lèvre de la chute a son eau à 568,99 m, le bassin à 180,03 m (l'œil de `chute388_bassin` à
  182,03 m, 2 m au-dessus), le sol de `chute388_mer` à 9,60 m ; le fleuve a son eau à 3,28 m sur
  306 m de large ; le lac son eau à 1 725,26 m, avec 3 345 m à 3 km vers le sud.
- **L'ancienne `plage_origine`** (−3 985 ; −165, cap 35, mesurée sans la planète) : avec elle,
  l'hydrologie relève le sol de 1,94 m sous la caméra, et de 1,5 à 2 m sur presque toute la vue
  jusqu'à 1 km ; la géographie y garde le drapeau de l'océan, mais le sol est au-dessus de l'eau
  à 0 m : du sable sec. Le sable vu dans l'essai de a028b7e était donc le sol du core, pas la mer
  peu profonde mal rendue, comme le disait la note de la #60 ; le mince coin de mer à gauche
  (caps 345 à 360, au-delà de 100 m) est bien la mer du core. La nouvelle `plage_origine`
  (160 ; 0, cap 273) a la même vue avec et sans la planète : la mer sur toute la vue de 20 m à
  2 km, peu profonde (0,12 m d'eau à 50 m, 0,56 m à 200 m, 1,23 m à 500 m, 2 m à 1 km) ; l'œil
  est au-dessus du sable sec (sol à 0,01 m, l'œil à 1,81 m), l'eau commence à 4,5 m devant.
- **`falaise_cotiere`** : il n'y a pas encore de falaise marine sur `main` (pas de roche 3D sur
  les côtes : elles viendront avec l'agent 33). C'est la côte la plus raide trouvée avec la
  planète parmi les 6 000 cases océan de la carte planète (8 km) voisines des terres les plus
  hautes (la plus forte montée sur 170 m depuis la mer, puis le profil tous les 5 m) : une
  montagne qui plonge dans la mer, 55° en moyenne du rivage au sommet (0 m, 41 m à 100 m, 334 m
  à 300 m, 557 m à 400 m, 1 402 m à 1 km ; 68° au plus raide sur 5 m), de 270 à 340 m de haut à
  300 m du rivage, sur près de 2 km de large autour d'une baie. La vue : de la mer, à 500 m du rivage
  (8,5 m d'eau, l'œil à 2 m), cap 225, inclinaison 21,4 : le pied au rivage à −0,2° et le sommet
  à +43° (rien devant ne le cache), à ±22° du centre, dans le cadre d'une image 16:9 (±29,4°
  pour un FOV horizontal de 90°).
- **La ligne `coast` de `Terrain.Info`** est la distance au rivage du signal de côte de la
  géographie, lissé, pas le premier sol sec : à l'ancienne `falaise_cotiere`, « at sea, 0.95 km
  from the shore », alors que le premier sol sec est à 375 m (cap 208,5), avec et sans la
  planète.

### La mer du plugin en mode `eau` et la mer peu profonde (pour la tâche « eau et horizon »)

En mode `eau`, les outils cachent tout l'horizon et le sol des tuiles (`TerrainDevLooks.cpp`),
et les tuiles ne maillent pas la mer (fleuves et lacs seulement, `Bridge/tile_water_mesh.cpp`) :
la surface bleu clair uniforme vue à l'ancienne `plage_origine` avec `Terrain.View eau`, sa bande
bleu marine sombre à l'horizon et son bord courbe, elliptique, ne sont pas la mer du plugin
(seul l'horizon GPU la dessine : son carré de mer à 0 m sous les tuiles), mais ce qui reste
derrière quand rien n'est dessiné (sans doute le brouillard et le ciel de la carte), et ce mode
ne montre pas si le carré de mer est là. Sous ce carré, les tuiles gardent le sol du core (sans
plancher à 0 m dans `convertMesh`) ; il est posé à 0 m sans descente ni pixel depth offset, et
masqué là où moins de la moitié des 4 texels de 8 m du niveau 0 autour du pixel sont de
l'océan. Le matériau tire la couleur de la mer vers le sable des hauts-fonds
(`Scripts/horizon_sea_hlsl.py`, poids exp(−profondeur / 1,2 m) : 90 % à 0,12 m d'eau, 63 % à
0,56 m, 36 % à 1,23 m) : la nouvelle `plage_origine` est le lieu pour regarder cet aspect.

## Comparer avant / après

1. Avant le changement : `Terrain.Views chute388_bassin k2_paroi fleuve_plaine lac_montagne
   plage_origine` (fichier planète chargé : la carte de test a `PlanetFile` par défaut).
2. Après : la même commande, la même taille de fenêtre.
3. Comparer les deux dossiers `Saved/TerrainEngine/Views/<date>/` image par image ; `views.txt`
   dit pour chaque image si elle était prête.

## Terrain.Seed

`Terrain.Seed <n> [surface|caves|regions]` (les presets sont les options du monde du plugin :
sans grottes, grottes, régions de grottes ; ceux du peuplement viendront plus tard) :

1. le fichier planète de la seed est préparé sur un fil à part, l'ancien monde joue pendant ce
   temps (1 à 2 min pour la planète entière, ≈ 400 Mo de plus ; l'étape sur la page de
   performance) :
   - `PlanetFile` avec `{Seed}` : le fichier de cette seed, lu ou construit et écrit là ;
   - `PlanetFile` fixe (un fichier de l'utilisateur) : lu seulement s'il contient cette seed,
     **jamais écrit** ; sinon le niveau est lu ou construit dans
     `Saved/TerrainEngine/dev/planet_{Seed}.bin` ;
   - `PlanetFile` vide : le relief seul ;
2. puis le monde du Play est refait (le fichier relu : quelques secondes, le jeu figé), le mode
   de vue gardé, le pion posé à nouveau. Le monde du niveau, lui, ne change pas.

Arrêter le Play annule une construction à la fin de l'étape en cours.

## Pour les autres modules

- **Une ligne de plus sur l'écran F3** : une fonction de la forme `InfoProvider`
  (`Bridge/point_info.hpp`) ajoutée à `defaultInfoProviders()` dans `Bridge/point_info.cpp` ; F3
  et `Terrain.Info` l'affichent sans autre changement (type de côte de l'agent 33, roche 3D de la
  12A, royaumes de l'agent 25…).
- **Téléporter** : `ATerrainWorld::TeleportView` (son contrat plus haut), puis
  `IsViewLoading`.
- **Lire les lieux** dans un outil du core : `terrain/dev/places_file.hpp`.

## Limites

- Le code Unreal des outils n'est compilé que par Unreal (ni ici ni en CI) : il faut le compiler
  une fois pour le vérifier.
- La roche 3D de la 12A n'est pas sur `main` : « hors heightmap » (sommets qui s'écartent du
  relief) en tient lieu ; le type de côte et le couloir de chute n'existent pas dans le core
  (distance à la côte et chute la plus proche à la place).
- Coût de l'eau : seulement le temps des maillages d'eau (Unreal) ; le coût GPU se compare avec
  `Terrain.View seche`. Coût de la roche : pas mesuré à part.
- Les images ont la taille de la fenêtre du Play.
- Le pion volant des outils traverse ce que son profil de collision `Spectator` ne bloque pas.
- Aucun réglage d'alimentation, de veille ou d'affichage de Windows n'est touché.

## Tests (ctest, sans Unreal)

`dev_places_file_test` (le fichier du dépôt valide et canonique, les champs inconnus gardés,
le fichier de l'agent 37 lu, les nombres de `Terrain.Save` relus au bit près),
`unreal_place_pose_test`, `unreal_point_info_test`, `unreal_dev_perf_test`,
`unreal_debug_colors_test`, `unreal_planet_job_test`, `unreal_walkable_ground_test` (le sol
praticable sur des reliefs fabriqués et sous la paroi du K2, l'ordre et les raisons des candidats,
la vérification sur la collision : trop raide, marche, loin de la heightmap, sous l'eau),
`unreal_dev_tools_guard`.
