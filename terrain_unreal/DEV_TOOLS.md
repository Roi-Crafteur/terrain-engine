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
| Lieux | `terrain_core/dev/` : lecteur JSON strict et fichier des lieux ; `Bridge/place_pose` : yaw = cap − 90, œil selon `eyeMode` | `Terrain.Goto`, `Look`, `Eye`, `Save`, `Pawn` ; le pion volant des outils |
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
| `Terrain.Goto <lieu>` | la vue d'un lieu du fichier (position, cap, inclinaison, œil) ; avertit si le lieu a besoin du fichier planète et que le monde n'en a pas |
| `Terrain.Look cap inclinaison` | le cap en degrés depuis le nord, sens horaire ; l'inclinaison vers le haut, de −90 à 90 |
| `Terrain.Eye 1800` / `Terrain.Eye +2` | l'œil à cette altitude, ou à cette hauteur au-dessus du sol ou de l'eau |
| `Terrain.Pawn` | le pion du jeu rendu, posé au sol sous la vue |
| `Terrain.Save <nom> [note…]` | la vue écrite dans le fichier des lieux (remplace un lieu du même nom) |
| `Terrain.Info` / `Terrain.Info here` | les lignes du point visé / de la colonne de l'œil dans le log |
| `Terrain.Shot <nom>` | une image `Saved/TerrainEngine/Shots/<nom>.png` une fois tout chargé |
| `Terrain.Views <lieux>` | une image par lieu (`chute388_bassin k2_paroi`, `*`, `chute388_*`) dans `Saved/TerrainEngine/Views/<date>/` avec `views.txt` |
| `Terrain.View <mode>` | le mode de vue (ci-dessous) |
| `Terrain.Seed <n> [surface\|caves\|regions]` | le monde d'une autre seed et d'un autre preset, sans quitter le Play |
| `Terrain.Hud [0-3]` | la page de l'écran F3 |

Variables console : `Terrain.PlacesFile` (un autre fichier des lieux ; vide : celui du dépôt),
`Terrain.ShotTimeout` (attente maximum d'une téléportation ou d'une image, 60 s),
`Terrain.ShotSettle` (stabilité avant une image, 1 s).

### La téléportation

`Terrain.Goto` et la carte de dev (agent 35) appellent la même fonction,
`ATerrainWorld::TeleportView(X, Z, Heading, Pitch, Eye, EyeMode)` (appelable en Blueprint) :

1. le joueur prend le **pion volant des outils** (un `ASpectatorPawn`, 50 m/s), dont l'œil est
   posé exactement à l'œil demandé, à l'image du point la plus proche de la vue actuelle ;
2. les tuiles LOD 0 autour sont générées avant que la fonction rende la main (le sol existe) ;
3. le pion reste immobile jusqu'à ce que toutes les tuiles autour soient affichées
   (`Terrain.ShotTimeout` au plus), le log dit après combien de temps.

`Terrain.Pawn` (ou `ATerrainWorld::ReturnToGamePawn`) rend le pion du jeu. Elle est synchrone ;
l'agent 38 la rendra progressive (chargement, fondu au noir).

### Les images

Une image est prise quand rien ne reste à charger autour de la vue : toutes les tuiles de la
sélection affichées, aucune en attente, l'horizon GPU affiché et fini (époque et détail), aucune
modification en cours, et cela depuis `Terrain.ShotSettle` secondes (l'exposition et la lumière
suivent) ; au plus `Terrain.ShotTimeout` secondes, sinon l'image est prise quand même et marquée
`NOT READY (timeout)`. L'écran F3 est caché pendant la capture. L'image a la taille de la fenêtre
du Play : **garder la même taille de fenêtre** pour comparer deux séries.

`views.txt` : la seed, le preset, le fichier planète, les anneaux de LOD, puis une ligne par vue
(fichier, lieu, x, z, œil, yaw, inclinaison, prête ou non et après combien de secondes, tuiles en
attente et affichées, horizon, images par seconde, et « WITHOUT the planet file it needs » quand
le lieu demande le fichier planète et que le monde ne l'a pas).

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

- `x`, `z` : mètres du moteur ; `heading` : cap en degrés depuis le nord (−Z), sens horaire, dans
  [0, 360) (yaw Unreal = cap − 90) ; `pitch` : degrés vers le haut, dans [−90, 90] ; `eye` :
  altitude du core (`eyeMode` `altitude`) ou hauteur au-dessus du sol ou de l'eau (`ground`,
  ≥ 0) ; `planet` : la vue a besoin du fichier planète (fleuves, chutes, lacs, climat des
  biomes) ; `note` : facultative.
- Lecture stricte : une erreur dit sa ligne et sa colonne, ou le lieu et le champ fautifs
  (« place 3 ("k2_paroi"): "pitch" must be in [-90, 90] degrees »). Les champs inconnus (ceux
  d'un autre agent) sont gardés et réécrits. Écriture canonique (la mise en page ci-dessus) :
  `Terrain.Save` ne change que le lieu qu'il écrit.
- Les nombres relus sont les mêmes au bit près ; `Terrain.Save` arrondit au centimètre et au
  centième de degré.

| Lieu | x | z | cap | incl. | œil | planète | note |
|---|---|---|---|---|---|---|---|
| `chute388_bassin` | −6 185 104 | 155 924 | 227 | 30 | 182,03 m (altitude) | oui | la chute de 388 m depuis son bassin |
| `chute388_levre` | −6 185 411 | 156 218 | 46 | −30 | +2 m | oui | sa lèvre (eau à 568,99 m), vers le bassin |
| `chute388_mer` | −6 184 762 | 155 681 | 230 | 20 | +2 m | oui | la chute vue de l'aval, à 842 m de sa lèvre |
| `k2_paroi` | 416 102 | −11 414 891 | 225 | 35 | +25 m | non | la plus haute paroi près du K2 (pied 416 017 ; −11 414 806, 420 m) |
| `falaise_cotiere` | 429 908 | −855 305 | 230 | 3 | +2 m | non | la côte vue de la mer (côte basse sur `main`) |
| `montagnes_mer` | 1 430 074 | 2 052 007 | 220 | 5 | +2 m | non | des montagnes qui plongent dans la mer |
| `cote_froide` | 3 624 471 | −10 006 796 | 120 | 5 | +2 m | non | une côte froide vue de la mer |
| `plage_origine` | −3 985 | −165 | 40 | 0 | +1,8 m | non | la plage près de l'origine, vers la mer |
| `fleuve_plaine` | −241 631 | −86 277 | 5 | 0 | +2 m | oui | un fleuve de plaine (eau à 3,28 m), vers l'aval |
| `lac_montagne` | 7 369 228 | −7 088 911 | 180 | 5 | +2 m | oui | un lac de montagne (eau à 1 725,26 m) |
| `lisiere_tropicale_savane` | −26 927 500 | −270 336 | 0 | −5 | +2 m | oui | la lisière (sur `main`, le core n'y voit que de la savane) |

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
- **Téléporter** : `ATerrainWorld::TeleportView`.
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

`dev_json_test`, `dev_places_file_test` (le fichier du dépôt valide et canonique),
`unreal_place_pose_test`, `unreal_point_info_test`, `unreal_dev_perf_test`,
`unreal_debug_colors_test`, `unreal_planet_job_test`, `unreal_dev_tools_guard`.
