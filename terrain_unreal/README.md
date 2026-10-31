# Terrain Engine pour Unreal (`terrain_unreal/`)

Plugin Unreal Engine 5.8 qui streame le monde lisse de `terrain_core` autour du joueur : tuiles
LOD 0 à 3 converties en meshes, collisions sur les tuiles proches, un Character qui marche
dessus. `terrain_core` décide de la forme du monde, Unreal de son apparence.

> **Statut : squelette de la phase 10, vague 1, jamais compilé.** Le code Unreal a été écrit sans
> Unreal ni GPU. Seule la couche C++ pure (`Source/TerrainEngine/Private/Bridge/`) est compilée et
> testée par ctest dans ce dépôt. Les erreurs de la première compilation sont attendues :
> envoie-les (voir « Compiler »).

## 1. Prérequis (Windows)

- Windows 10 ou 11, 64 bits.
- **Visual Studio 2022** (Community suffit) avec la charge de travail
  **« Développement de jeux en C++ »** (*Game development with C++*). Dans ses composants, garde le
  compilateur MSVC v143 et un SDK Windows 10/11 récents ; la version minimale exacte demandée par
  UE 5.8 est dans ses notes de version (*Platform SDK Upgrades*).
- **Unreal Engine 5.8**, installé par l'Epic Games Launcher (Unreal Engine → Bibliothèque → +).
- Git.

## 2. Installer le plugin dans un projet

Le plugin compile les **sources** de `terrain_core` par un chemin relatif
(`terrain_unreal/../terrain_core`) : il doit rester dans une copie complète de ce dépôt.

1. Crée un projet : Epic Games Launcher → Unreal Engine 5.8 → Jeux → **Third Person**, type
   **C++** (pas Blueprint), nom `TerrainSandbox`, par exemple dans `C:\Projets`. Ferme l'éditeur
   quand il s'ouvre.
2. Mets le dépôt entier dans le dossier `Plugins` du projet :
   ```bat
   cd C:\Projets\TerrainSandbox
   mkdir Plugins
   git clone <url du dépôt terrain-engine> Plugins\terrain-engine
   cd Plugins\terrain-engine
   git checkout claude/trusting-lovelace-p7b7ki
   ```
   Pour réutiliser un clone existant, une jonction suffit (pas besoin d'être administrateur) ;
   elle doit viser la **racine** du dépôt, pas `terrain_unreal` :
   ```bat
   mklink /J C:\Projets\TerrainSandbox\Plugins\terrain-engine C:\dev\terrain-engine
   ```
   Unreal trouve `Plugins\terrain-engine\terrain_unreal\TerrainEngine.uplugin`. Plus tard, le
   dépôt du jeu inclura ce dépôt comme sous-module git au même endroit.
3. Clic droit sur `TerrainSandbox.uproject` → **Generate Visual Studio project files**.

## 3. Compiler

En ligne de commande (c'est la sortie à m'envoyer en cas d'erreur) :
```bat
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TerrainSandboxEditor Win64 Development -Project="C:\Projets\TerrainSandbox\TerrainSandbox.uproject" -WaitMutex > build_log.txt 2>&1
```
Ou dans Visual Studio : ouvrir `TerrainSandbox.sln`, configuration **Development Editor**,
plateforme **Win64**, Générer → Générer la solution.

En cas d'échec, envoie `build_log.txt` en entier, et
`%LOCALAPPDATA%\UnrealBuildTool\Log.txt` si la sortie ne montre pas l'erreur. Erreurs possibles,
déjà repérées :
- `FPSemantics` inconnu dans `TerrainEngine.Build.cs` : la propriété n'existe pas sous ce nom en
  5.8, à remplacer (dis-le-moi).
- `terrain_core needs strict floating point` (le `#error` de `det_math.hpp`) : Unreal compile le
  core en `/fp:fast` ou `/fp:contract`, `FPSemantics = Precise` n'a pas pris effet (dis-le-moi) ;
- avertissements ou erreurs MSVC dans les fichiers de `terrain_core` : la CI le compile avec MSVC
  en `/W4 /WX`, mais jamais avec les réglages d'avertissements d'Unreal ;
- `terrain_core not found` : le plugin n'est pas dans une copie complète du dépôt.

Limite connue : `RunUAT BuildPlugin` copie le plugin seul ailleurs et ne trouve donc pas
`terrain_core` ; compile le plugin avec le projet.

## 4. Utiliser

1. Ouvre `TerrainSandbox.uproject`. Vérifie dans Edit → Plugins que **Terrain Engine** et
   **Procedural Mesh Component** sont activés.
2. Matériau (une fois par projet, facultatif) : active **Python Editor Script Plugin** si besoin,
   puis Tools → **Execute Python Script…** →
   `Plugins/terrain-engine/terrain_unreal/Scripts/create_temporary_terrain_material.py`. Il crée
   `/Game/TerrainEngine/M_TemporaryTerrain`, le **matériau temporaire** du terrain (section 13) :
   roche sur les pentes, neige, herbe, sable… selon les matériaux et biomes du core, en attendant
   les vrais matériaux de la phase 14. Ou bien `Scripts/create_terrain_material.py`, qui crée
   `/Game/TerrainEngine/M_TerrainAltitude`, un matériau éclairé qui affiche la couleur des sommets
   (sable au bord de la mer, vert, gris en altitude, blanc sur les sommets). Sans aucun des deux,
   l'acteur prend le matériau de debug d'Unreal qui affiche les couleurs de sommet, et le dit dans
   le log. Pour l'eau des lacs, des fleuves et des chutes : `Scripts/create_water_materials.py`
   (section 14).
3. Niveau : File → New Level → **Basic** (ciel, lumière, un Player Start), puis supprime le sol
   (`Floor`), qui cacherait le terrain. Le mode de jeu du gabarit Third Person
   (`BP_ThirdPersonGameMode`) donne le Character qui marche. Ou bien, dans `ThirdPersonMap`,
   supprime le sol et les obstacles. Un niveau **Empty** n'a pas de lumière : le terrain y serait
   noir.
4. Window → Place Actors, cherche **Terrain World** et pose-le dans le niveau (sa position ne
   compte pas : le sol du point `CoreOriginX`, `CoreOriginZ` du core est à l'origine d'Unreal).
5. **Play**. Au premier tick, les tuiles autour du Character sont générées, puis, s'il est sous le
   sol, il est posé dessus. ZQSD / WASD pour marcher, espace pour sauter.
6. Logs : Window → Output Log, filtre `LogTerrainEngine`. Toutes les 5 s, un résumé des coûts par
   LOD (génération, conversion, envoi au mesh, sommets et triangles moyens) ; une ligne par tuile
   avec la commande console `log LogTerrainEngine Verbose`.

### Check-list du premier essai (à me renvoyer)

- [ ] compilation sans erreur (sinon le log) ;
- [ ] log `the heights and tiles of the default world match terrain_core bit for bit` ;
- [ ] terrain visible **d'en haut**, éclairé du côté du soleil (enroulement et normales) ;
- [ ] couleurs selon l'altitude ;
- [ ] le Character marche et saute sans traverser le sol, y compris loin de son départ ;
- [ ] vu de haut (F8 pour sortir du pion pendant le Play), tuiles plus grossières au loin, pas de
  trou, ni à l'arrêt ni en marchant ;
- [ ] deux ou trois résumés de mesures du log, et les valeurs de `stat unit` ;
- [ ] `MeshType` = Dynamic Mesh : un avertissement, puis le Procedural Mesh Component.

### Dépannage

- **On marche sur le terrain mais on ne le voit que d'en dessous** : l'enroulement des triangles
  est inversé ; une seule ligne à changer dans `Bridge/tile_mesh_conversion.cpp`, dis-le-moi.
- **Éclairage à l'envers** (côté soleil sombre) : les normales ; envoie une capture.
- **`this build does not generate the same world`** : cette compilation ne calcule pas les mêmes
  hauteurs ou les mêmes sommets que `terrain_core` (options flottantes) ; rien n'est streamé ;
  envoie le log.
- **`streaming stopped: …`** : une erreur du core ; le streaming s'arrête, envoie le message.
- **Le Character tombe à travers le sol** : vérifie `bAsyncCollisionCooking` (désactivé par
  défaut) et la ligne `ground ready under …` du log.
- **Saccades** : regarde les temps d'envoi (`upload`) du log ; baisse `MaxTileUploadsPerFrame` ou
  active `bAsyncCollisionCooking`.
- **Rien n'apparaît** : un sol du niveau cache le terrain, ou une erreur est dans le log.

## 5. Paramètres de `Terrain World`

| Propriété | Défaut | Rôle |
|---|---|---|
| `Seed` | 20261019 | seed du monde |
| `bCaves`, `bCaveRegions` | non | grottes, et leurs régions (galeries, rivières, lave) |
| `PlanetFile` | `Saved/TerrainEngine/planet_{Seed}.bin` | fichier du niveau Planète de la seed (`terrain_make_planet`) : fleuves, lacs et chutes des tuiles et de l'horizon, et leur niveau (section 14) ; `{Seed}` remplacé par la seed, un chemin relatif part du dossier du projet, un chemin absolu est pris tel quel ; construit et écrit là s'il manque, s'il est périmé ou abîmé (quelques minutes, la raison dans le log) ; vide : le relief seul, sans fleuves ni lacs |
| `CoreOriginX`, `CoreOriginZ` | 0, 0 | point du core (m) dont le sol est à l'origine d'Unreal |
| `Lod0/1/2DistanceChunks`, `ViewDistanceChunks` | 4, 8, 16, 32 | anneaux de LOD en chunks de 16 m ; Lod1 − Lod0 ≥ 2, Lod2 − Lod1 ≥ 4 |
| `WorkerThreads` | 2 | threads de génération |
| `MaxTileUploadsPerFrame` | 8 | budget de tuiles converties par frame |
| `MeshType` | Procedural Mesh | type de mesh (Dynamic Mesh, Realtime Mesh : emplacements) |
| `Material` | aucun | matériau des tuiles (voir 4.2) ; aucun : `M_TemporaryTerrain`, sinon `M_TerrainAltitude` |
| `bAsyncCollisionCooking` | non | cuisson des collisions hors du thread de jeu |
| `bWater` | oui | lacs et fleuves des tuiles à leur niveau, rideaux et écume des chutes, lacs et fleuves de l'horizon GPU (section 14) ; `Terrain.Water 0\|1` les cache ou les montre en jeu |
| `WaterMaterial`, `FallsMaterial` | aucun | matériaux de la surface de l'eau des tuiles et des chutes ; aucun : `M_TerrainWater`, `M_TerrainFalls` (`Scripts/create_water_materials.py`) |
| `FocusActor` | aucun | acteur suivi ; sinon le pion du premier joueur, ou sa caméra |
| `bSnapPawnToGround` | oui | pose sur le sol un nouveau pion qui est dessous |
| `WarmUpRadiusChunks`, `WarmUpTimeoutSeconds` | 1, 10 | tuiles LOD 0 générées avant qu'un nouveau pion bouge |
| `LoadingSettings` | renfort, fondu 2 s, horizon niveau par niveau | chargement au spawn et à la téléportation : renfort des fils et des envois sous un plafond, écran noir jusqu'au sol, 64 m, toutes les tuiles et les niveaux 0 à 3 de l'horizon, pion tenu jusqu'à sa collision (`LOADING.md`) |
| `TimingLogIntervalSeconds` | 5 | intervalle du résumé des mesures (0 : aucun) |
| `bHorizon` | oui | horizon lointain (section 10) |
| `HorizonLevels` | 10 | niveaux de l'horizon : 8 m, puis le double à chaque niveau ; 10 vont à 520 km |
| `HorizonRenderer` | GPU | GPU : textures et grilles fixes (section 11) ; CPU : un mesh procédural par niveau (section 10), pour comparer, à retirer |
| `HorizonThreads` | 2 | GPU : morceaux calculés à la fois en tâches de basse priorité ; CPU : threads de l'horizon, en plus de ceux des tuiles |
| `HorizonGpuMaterial` | aucun | matériau de l'horizon GPU (voir 11) |
| `HorizonMorphCells` | 32 | GPU : largeur, en mailles de chaque niveau, de la bande où il se fond dans le niveau plus grossier |
| `bHorizonDebugTint` | non | GPU : chaque niveau teinté de sa couleur |
| `HorizonMaterial` | aucun | matériau de l'horizon CPU (voir 10) |
| `HorizonDropCm` | 100 | l'horizon est posé ce tant sous son sol là où les tuiles le recouvrent : elles restent devant ; il remonte à son sol vers le bord des tuiles (section 13) |
| `bHorizonSea` | oui | l'océan à plat au niveau de la mer, et un carré de mer sous les tuiles, seulement là où le core met l'océan (section 12) |
| `PlanetRadiusKm` | 0 | rayon de la planète pour la courbure ; 0 : la circonférence est-ouest du monde (9 554 km) |
| `bHorizonShadows` | non | ombres portées par l'horizon |
| `bHorizonDetailNormals`, `HorizonNormalRatio`, `HorizonNormalJobs` | oui, 4, 2 | GPU : normales de détail, 4 fois plus fines que la grille de chaque niveau, calculées par 2 tâches (TEMPORARY_MATERIAL.md) |
| `HorizonCrestRatio` | 4 | crêtes des montagnes lointaines : à partir du niveau 1, chaque échantillon monte aux crêtes de ses mailles, cherchées 2, 4 ou 8 fois plus finement que la grille (`terrain_core` `streaming/horizon_crests.hpp`) ; en GPU, calculées après l'époque avec les normales de détail, par les mêmes `HorizonNormalJobs` tâches (section 11) ; 0 : les points, l'horizon d'avant |
| `bAtmosphereFollowsCamera` | oui | le ciel, le brouillard et la lumière du ciel suivent la caméra (section 12) |
| `SkyAtmosphere`, `HeightFog`, `SkyLight` | aucun | ceux qui suivent la caméra ; sinon les premiers du niveau |
| `FogEndDistanceKm` | 20 | distance où le brouillard cesse d'intégrer ; 0 : son propre réglage |

## 6. Comment c'est construit

```
terrain_unreal/
  TerrainEngine.uplugin        module Runtime (Win64, Linux, Mac), plugin ProceduralMeshComponent
  Scripts/create_terrain_material.py
  Scripts/create_horizon_material.py
  Scripts/create_horizon_gpu_material.py
  Scripts/create_temporary_terrain_material.py, temporary_terrain_look.py,
    temporary_terrain_hlsl.py  le matériau temporaire
  Scripts/create_water_materials.py, water_look_hlsl.py, horizon_water_hlsl.py,
    horizon_water_nodes.py     l'eau des tuiles, des chutes et de l'horizon
  Scripts/material_settings.py  la remise à zéro des réglages des matériaux reconstruits
  Source/TerrainEngine/
    TerrainEngine.Build.cs
    Public/TerrainWorld.h      ATerrainWorld, ETerrainMeshType, ETerrainHorizonRenderer
    Public/TerrainEdits.h      ETerrainEditShape, ETerrainEditMode
    Private/
      TerrainWorld.cpp         cycle de vie, tick, démarrage d'un pion, mesures
      TerrainWorldHorizon.cpp  l'horizon lointain d'ATerrainWorld
      TerrainWorldEdits.cpp    les modifications pour essayer (section 15), Terrain.Edit
      TerrainWorldWater.cpp    l'eau (section 14), Terrain.Water, la ligne du fichier planète
      TerrainWorldState.h      la partie terrain_core d'un ATerrainWorld en jeu
      TerrainHorizonClipmap.*  l'horizon GPU : grilles fixes, placements
      TerrainHorizonTextures.* ses textures (texels, normales, eau) et leurs envois
      TerrainWorldAtmosphere.cpp, TerrainAtmosphereFollower.*  le ciel qui suit la caméra
      TerrainHorizonGrids.*    les grilles fixes, des UStaticMesh construits en jeu
      TerrainHorizonMeshes.*   l'horizon CPU : un ProceduralMeshComponent par niveau
      TerrainMeshBackend.*     interface des types de mesh, fabrique
      ProceduralMeshTerrainBackend.*
      Bridge/                  C++ pur, compilé par Unreal ET testé par ctest
      TerrainCore/core_*.cpp   un fichier par source de terrain_core (hors io/), qui l'inclut
```

- **Un seul module** compile à la fois `terrain_core` et le code Unreal : un module séparé pour le
  core demanderait des macros d'export dans `terrain_core`.
- **`TerrainCore/core_*.cpp`** : Unreal ne compile que les fichiers du dossier du module ; chaque
  enrobage inclut une source du core. Le test `unreal_core_sources` échoue si une source du core
  n'a pas son enrobage : ajoute-le (une ligne) quand le core gagne un fichier.
- **`Build.cs`** : pas de PCH (aucun en-tête Unreal ne doit entrer dans les sources du core), pas
  de build unity, exceptions activées (le core en lance), C++20, `FPSemantics = Precise` (voir
  « Flottants stricts » plus bas).
- **Coordonnées** (`Bridge/unreal_frame.hpp`, seul endroit) : le core est en mètres, Y vertical ;
  Unreal en centimètres, Z vertical. `X = 100 (x − ox)`, `Y = 100 (z − oz)`, `Z = 100 (y − oy)`.
  Cet échange ne mire pas le monde. Positions en double (Large World Coordinates, actives d'office
  en UE5) ; chaque tuile est un composant placé à son origine, ses sommets restent de petits
  floats. Le recentrage d'origine d'Unreal (`ApplyWorldOffset`) déplace le repère.
- **Monde bouclé** : le monde du core est un tore (`WorldSize`, 60 030 976 × 34 996 224 m par
  défaut), pas celui d'Unreal. Le joueur garde des coordonnées continues de part et d'autre d'un
  bord ; le `StreamManager` les ramène dans le domaine canonique, et ses tuiles ont des clés et
  des origines canoniques. Chaque tuile est montrée à son image la plus proche du joueur
  (`nearestTile`, `tileOrigin`) : en traversant un bord, les tuiles d'au-delà (clés à l'autre bout
  du monde) se posent à côté des autres, sans rien recharger. Une tuile déjà affichée dont l'image
  la plus proche change (un saut d'un monde entier) est déplacée (`MoveTile`), pas regénérée.
- **Enroulement** : Unreal dessine la face avant de (a, b, c) du côté de (c − a) × (b − a),
  l'inverse du core ; l'échange Y ↔ Z inverse déjà les produits vectoriels, donc les indices
  gardent leur ordre (vérifié par `unreal_tile_mesh_test`).
- **Streaming** (`Bridge/tile_streamer.hpp`) : le budget par frame est celui du `StreamManager`
  (`maxLoadsPerUpdate`) ; une tuile n'est retirée qu'une fois ses remplaçantes affichées, dans la
  même frame : pas de trou. Collisions sur toutes les tuiles LOD 0.
- **Autocontrôle du déterminisme** (`Bridge/determinism_check.cpp`) : au `BeginPlay`, les
  hauteurs de 9 points du monde par défaut (dont le coin du monde et un point à un monde de
  distance d'un autre), puis l'empreinte (FNV-1a des bits de l'origine, des sommets, des normales
  et des indices) de 4 tuiles LOD 0 (dont celle du coin du monde), sont comparées à des tables
  produites par le core compilé par CMake et vérifiées par ctest sur les 5 jobs de la CI, macOS
  compris. En cas de différence : erreur dans le log et pas de streaming.
- **Flottants stricts** : le core compilé par CMake utilise `-ffp-contract=off -fno-fast-math`
  (GCC, Clang) ou `/fp:precise` (MSVC). Unreal compile les mêmes sources avec ses propres options ;
  deux gardes s'en assurent :
  - le `#error` de `terrain_core/include/terrain/det_math.hpp` refuse la compilation sous
    `__FAST_MATH__` (`-ffast-math`), `_M_FP_FAST` (`/fp:fast`) et `_M_FP_CONTRACT`
    (`/fp:contract`). `FPSemantics = Precise` donne `/fp:precise` sous MSVC, qui ne définit aucune
    de ces macros (et ne contracte jamais depuis Visual Studio 2022) : le plugin passe ce
    `#error`. Si la compilation s'arrête sur `terrain_core needs strict floating point`, c'est que
    `FPSemantics` n'a pas été appliqué : envoie le log ;
  - **`-ffp-contract=fast` (ou `on`) de GCC et Clang n'est pas détecté par ce `#error`** : ces
    compilateurs ne définissent aucune macro pour la contraction en FMA, qui change pourtant les
    résultats sur les cibles qui ont l'instruction (ARM64 toujours, x86-64 avec AVX2). C'est
    l'**autocontrôle par empreintes de tuiles** qui l'attrape au `BeginPlay`. Vérifié ici : des
    builds du core par les fichiers d'enrobage avec `-march=native -ffp-contract=fast` (GCC,
    Clang) et `-ffp-contract=on` (Clang) passent le `#error` et les 9 hauteurs, mais sont refusés
    sur l'empreinte de la tuile de montagne. GCC `-O3` et Clang `-O2` sans contraction passent.
    Sous Linux et Mac, si la chaîne d'Unreal contracte malgré `FPSemantics = Precise`, le plugin
    refusera donc de streamer avec `this build does not generate the same world`.

Tests ctest de la couche pure (`scripts/run_tests.sh` à la racine) : `unreal_frame_test`,
`unreal_tile_mesh_test`, `unreal_altitude_color_test`, `unreal_tile_timings_test`,
`unreal_tile_streamer_test`, `unreal_terrain_session_test`, `unreal_determinism_check_test`,
`unreal_horizon_layout_test`, `unreal_horizon_mesh_test`, `unreal_horizon_streamer_test`,
`unreal_horizon_epochs_test`, `unreal_horizon_textures_test`, `unreal_horizon_gpu_streamer_test`,
`unreal_horizon_normals_test`, `unreal_horizon_detail_test`, `unreal_horizon_crests_test`,
`unreal_horizon_atmosphere_test`, `unreal_terrain_surface_test`, `unreal_tile_water_test`,
`unreal_water_scripts_test` (les scripts de l'eau, sans Unreal), `unreal_planet_file`,
`unreal_core_sources`. Mesures à la main : `unreal_horizon_bench` (temps de l'horizon GPU, crêtes
dans l'époque ou après, voir l'en-tête de `tests/unreal_horizon_bench.cpp`).

## 7. Linux et Mac (non essayés)

```sh
<UE_5.8>/Engine/Build/BatchFiles/Linux/Build.sh TerrainSandboxEditor Linux Development -Project=/chemin/TerrainSandbox.uproject
<UE_5.8>/Engine/Build/BatchFiles/Mac/Build.sh TerrainSandboxEditor Mac Development -Project=/chemin/TerrainSandbox.uproject
```
Risque connu : le core utilise `std::jthread`, que la libc++ fournie avec Unreal (Linux) et celle
d'Apple n'exposent peut-être que comme expérimental. Si la compilation échoue là-dessus, le core
devra passer à `std::thread` (petit changement dans `terrain_core`, par l'agent concerné).

## 8. Le monde bouclé de la phase 9

- **Placement des tuiles** : fait. Chaque tuile (clé et données canoniques) est montrée à son
  image la plus proche du joueur, et déplacée si cette image change. Tests :
  `unreal_tile_mesh_test` (une tuile du coin montrée à une image un monde plus loin, sur le sol),
  `unreal_tile_streamer_test` (marche à travers le bord est, saut d'un monde entier, démarrage au
  coin du monde).
- **Tables de l'autocontrôle** : à chaque changement des valeurs du terrain (un nouveau
  `kGeneratorVersion` du format de monde), mettre à jour `kHeightProbes` et `kTileProbes`
  (`Bridge/determinism_check.cpp`) dans le même commit que le core ;
  `unreal_determinism_check_test` échoue d'ici là et affiche les nouvelles valeurs
  (`probe (x, z): 0x… expected, 0x… computed`, `tile (x, z): 0x… expected, 0x… computed`).
- **Retour d'un tour du monde** : fait. Les Large World Coordinates d'Unreal vont jusqu'à
  environ 88 000 km (`UE_LARGE_WORLD_MAX`). Quand le joueur (`FocusActor`, sinon le pion) passe à
  plus de 0,75 monde de l'origine d'Unreal en X ou en Y (45 000 km en X, 26 000 km en Y), il est
  ramené d'un nombre entier de mondes (`worldTurnOffset`, `Bridge/unreal_frame.hpp`) par un
  téléport qui garde sa vitesse ; dans le même tick, toutes les tuiles se déplacent du même
  décalage, exactement. Rien ne bouge à l'écran, le repère ne change pas. Les autres acteurs
  (autres joueurs, objets posés) ne sont pas encore ramenés : ce sera la phase multijoueur.
  Tests : `unreal_frame_test` (seuils, plusieurs tours, jamais d'aller-retour, décalage exact),
  `unreal_tile_streamer_test` (un joueur au-delà du seuil à l'est et au nord : aucune tuile
  chargée ni retirée, toutes déplacées du décalage).

## 9. Pas encore fait

Type de mesh autre que le Procedural Mesh Component (emplacements Dynamic Mesh et Realtime Mesh),
eau des grottes, lave, plantes, vrais matériaux de la phase 14 (un matériau temporaire en tient lieu,
section 13). En build empaqueté, assigne les matériaux explicitement dans `Material`,
`HorizonGpuMaterial`, `HorizonMaterial`, `WaterMaterial` et `FallsMaterial` : le chemin chargé par
défaut n'est cuit que s'il est référencé.

## 10. L'horizon lointain (phase 13, étape 1)

Les 10 niveaux de grilles de `terrain_core` (`HorizonStreamer` : 257 × 257 échantillons, de 8 m à
4 km, 520 km de rayon) sont montrés autour du joueur, au-delà des tuiles. Cette section décrit le
mode CPU (`HorizonRenderer` = CPU) ; le mode par défaut, sur le GPU, est en section 11.

- **Matériau** (une fois par projet) : Tools → Execute Python Script… →
  `Plugins/terrain-engine/terrain_unreal/Scripts/create_horizon_material.py`. Il crée
  `/Game/TerrainEngine/M_TerrainHorizon` : la couleur des sommets, comme `M_TerrainAltitude`, et
  la **courbure** de la planète : son World Position Offset abaisse chaque sommet de d² / 2R (d :
  sa distance horizontale à la caméra, R : le paramètre `PlanetRadiusCm`, que l'acteur règle). La
  courbure est recalculée à chaque frame, sans rien reconstruire, et les deux côtés d'un raccord
  tombent de la même hauteur. Sans ce matériau, l'horizon prend celui des tuiles et reste plat
  (un avertissement dans le log). Les tuiles ne sont pas courbées : 2 cm à 640 m.
- **Un ProceduralMeshComponent par niveau**, sans collision ni ombre, placé à l'image du niveau la
  plus proche du joueur (comme les tuiles). Section 0, le corps : la fenêtre moins un carré central
  fixe, qui contient la fenêtre du niveau plus fin où qu'elle soit ; ses triangles ne changent
  jamais, un déplacement du niveau ne renvoie que ses sommets (`UpdateMeshSection`). Section 1, la
  bordure : le cadre de 1 ou 2 mailles laissé par la fenêtre plus fine, reconstruit quand l'un des
  deux niveaux bouge. Toutes les sections d'une époque sont appliquées dans la même frame : les
  niveaux s'emboîtent toujours, sans trou ni recouvrement (`Bridge/horizon_layout.hpp`).
- **Raccords** : le bord extérieur de chaque niveau est cousu sur le niveau plus grossier (ses
  échantillons pairs prennent l'altitude de l'échantillon grossier, que les crêtes ont pu relever ;
  les impairs, la moyenne de leurs voisins), et une jupe tournée vers l'intérieur pend dessous
  (2 m, ou un seizième du pas). Les bornes de chaque mesh descendent jusqu'au sol courbé (un sommet sans triangle) : aucun
  niveau n'est éliminé à tort.
- **Sous les tuiles** : le niveau 0 a un trou carré qui contient toutes les tuiles LOD 0 à 2
  (grottes, falaises, surplombs) ; autour, une bande de tuiles LOD 3 (128 m, au moins 64 m)
  recouvre l'horizon, posé `HorizonDropCm` plus bas : pas de z-fighting, et pas de trou quand des
  tuiles du bord arrivent en retard. Là où les tuiles peuvent le couvrir (jusqu'à 128 m au-delà
  de cette bande, puis en fondu sur 128 m), le sol du niveau 0 est aussi repoussé de 1 m en
  profondeur (pixel depth offset, `BehindTilesCm`) : sans cela ses mailles de 8 m traversaient
  les tuiles en damier de carrés sombres sur les pentes. Les tuiles doivent finir dans le niveau 0 :
  `ViewDistanceChunks` de 28 à 55 avec les anneaux par défaut, sinon l'horizon est désactivé avec
  une erreur dans le log.
- **Mer** : sur l'horizon, tout échantillon sous le niveau de la mer est posé à plat au niveau de
  la mer (un lac en altitude garde son sol) ; un carré de mer à 0 m s'étend sous les tuiles, dessiné
  seulement là où le core met l'océan (une dépression sèche sous 0 m montre son sol). Son
  aspect : section 12. Les lacs et fleuves : section 14 (horizon GPU seulement).
- **Threads** : `HorizonThreads` threads du core, à part de ceux des tuiles. Tant que rien ne
  bouge, le tick de l'horizon coûte 0,01 ms.
- **Log** : toutes les `TimingLogIntervalSeconds`, le coût de l'horizon sur le thread de jeu (le
  premier affichage, les ticks qui déplacent un niveau, par niveau, les autres), la conversion et
  l'envoi des corps et des bordures, les époques (du plan à l'application) et la mémoire.
- **Scène** : une `SkyAtmosphere`, une `SkyLight` en capture temps réel et un
  `ExponentialHeightFog` peu dense au niveau de la mer ; l'acteur règle et déplace le reste
  (section 12).

### Mesures (UE 5.8, éditeur en PIE, Ryzen 9 9950X3D, 10 niveaux, `ViewDistanceChunks` 32)

- **Premier affichage** : 0,6 à 1,05 s après le démarrage (660 490 échantillons, 2 threads), puis
  28 à 35 ms sur le thread de jeu, une fois (10 corps et 9 bordures créés).
- **Déplacement d'un niveau** (le niveau n bouge tous les 16 × 2ⁿ m) : 2,1 à 2,9 ms en moyenne sur
  le thread de jeu (conversion 0,5 à 1 ms, envoi 1,3 à 1,7 ms), 3,4 ms au plus, quelques pics à
  5 ou 6 ms ; les époques sont calculées en 9 à 13 ms. Sans déplacement : 0,01 ms par tick.
- **Frames** (Character, 6 m/s puis 30 m/s) : sans horizon 8,3 / 9,0 ms en moyenne, 99ᵉ centile
  8,9 / 12,2 ms ; avec 8,4 / 8,6 ms, 99ᵉ centile 9,0 / 18,0 ms : à 108 km/h, un déplacement de
  niveau (deux par seconde) fait parfois manquer une image à 120 Hz. La génération des tuiles ne
  change pas (0,22 à 0,27 ms par tuile LOD 0 avec ou sans horizon).
- **Mémoire** : 12 Mo d'échantillons et de topologies ; 521 000 sommets et 1 018 000 triangles
  (508 000 et 994 000 avec `ViewDistanceChunks` 45), dont 87 Mo gardés par les Procedural Mesh
  Components côté CPU ; côté GPU, environ 35 à 45 Mo (estimation).
- **Image** : depuis un sommet de 4 085 m, les silhouettes à 45, 147 et 400 km tombent à environ
  0,2° près sur celles du panorama de contrôle du core calculé avec le même rayon (sans la
  courbure, celle à 400 km serait 1° trop haut) ; aucun niveau n'est
  éliminé en tournant la caméra ; traversée du bord est du monde et tour du monde sans
  reconstruction (les niveaux se déplacent d'un monde, en moins de 2 ms).
- **Étape 2** : faite, section 11.

## 11. L'horizon sur le GPU (phase 13, étape 2)

Par défaut (`HorizonRenderer` = GPU), l'horizon n'est plus un mesh reconstruit par le CPU : chaque
niveau est une texture réécrite par bandes et une grille fixe que le matériau déforme.

- **Matériau** (une fois par projet) : Tools → Execute Python Script… →
  `Plugins/terrain-engine/terrain_unreal/Scripts/create_horizon_gpu_material.py`. Il crée
  `/Game/TerrainEngine/M_TerrainHorizonGpu`. Sans lui, l'horizon CPU est dessiné, avec un
  avertissement dans le log.
- **Textures** (`Bridge/horizon_textures.hpp`) : une texture de 257 × 257 texels par niveau,
  `PF_A32B32G32R32F`, au plus proche, sans mipmap. Un texel : la hauteur (m, déjà à plat au niveau
  de la mer), les deux pentes, la surface empaquetée `matériau + 8 biome` (exacte en float,
  `Bridge/terrain_surface.hpp`, lue par le matériau temporaire, section 13) ou, sur l'océan, le sol
  sous la mer (négatif).
  Adressage torique, celui du core : l'échantillon (i, k) de la fenêtre est au texel
  (premier emplacement + (i, k)) mod 257. Un déplacement de niveau n'écrit que la bande nouvelle
  (`UpdateTextureRegions`, quelques centaines de texels), préparée sur les tâches : rien n'est
  converti sur le thread de jeu, rien n'est gardé côté CPU en dehors des échantillons du core.
- **Grilles fixes** (`TerrainHorizonGrids.cpp`, `bodyGrid`, `trimGrid`) : des `UStaticMesh`
  construits une fois en jeu (`BuildFromMeshDescriptions`, sans accès CPU), sommets en mailles
  (i, k, 0) : la grille du niveau 0 (sa fenêtre moins le trou sous les tuiles, plus le carré de
  mer), une grille d'anneau partagée par les niveaux 1 à 9 (mise à l'échelle de leur pas), et les
  9 bordures possibles (la place de la fenêtre plus fine dans le carré central). Un déplacement de
  niveau : une nouvelle position du composant, quelques paramètres scalaires, parfois une autre
  bordure. Les bornes sont étendues (10 km au-dessus, 45 km dessous) : aucun niveau n'est éliminé
  à tort.
- **Nuanceur de sommets** : il lit le texel du sommet, le fond dans le niveau plus grossier
  (géomorphing), abaisse la jupe, pose la mer à 0 m, descend l'horizon de `HorizonDropCm` et
  courbe la planète (d² / 2R, comme en section 10). La normale (espace monde) et la couleur passent
  au nuanceur de pixels par des interpolateurs.
- **Géomorphing** (`Bridge/horizon_morph.hpp`) : dans une bande de `HorizonMorphCells` mailles
  (32 par défaut, sans couture visible dans les captures teintées) avant le bord de chaque niveau,
  chaque sommet glisse vers la surface du niveau plus grossier : la moyenne des deux échantillons
  grossiers de l'arête du triangle grossier qui le porte, lus dans sa texture (hauteur et sol). Avec
  les crêtes (`HorizonCrestRatio`), un échantillon pair glisse vers l'échantillon grossier relevé ;
  sans elles (0), les deux sont les mêmes au bit près et rien ne change.
  Facteur α = clamp((d − (K/2 − 2 − w)) / w, 0, 1), d la distance de Tchebychev au joueur en
  mailles, K = 256, w la largeur : α vaut 1 au bord, où les deux niveaux coïncident (pas de
  couture), et varie avec le joueur, pas avec l'emplacement du niveau (un déplacement de niveau ne
  fait rien sauter). Les pentes glissent vers celles du niveau plus grossier, lues dans sa texture.
- **Époques en tâches de basse priorité** (`Bridge/horizon_epochs.hpp`) : les morceaux d'une
  époque sont calculés par `UE::Tasks` en `BackgroundLow`, au plus `HorizonThreads` à la fois ;
  les workers des tuiles passent devant. Une époque est appliquée en une frame (bandes et
  placements de tous ses niveaux), comme à l'étape 1.
- **Crêtes après l'époque** (`HorizonGpuParams::deferCrests`, `Bridge/horizon_crest_texels.hpp`) :
  l'époque ne calcule que les points ; les crêtes viennent ensuite, des tâches des normales de
  détail, lues dans la même grille fine (`terrain_core` `streaming/horizon_detail.hpp`) ; les
  texels des échantillons qui montent sont réécrits par bandes (les seules lignes qui montent),
  au bit près ceux des crêtes calculées dans l'époque. L'horizon s'affiche donc au coût d'une
  époque sans crêtes, en points, puis ses crêtes arrivent, les niveaux fins d'abord. Pas de
  fissure entre un niveau relevé et un voisin pas encore relevé : au bord, le géomorphing ne lit
  que les texels du niveau plus grossier (`unreal_horizon_crests_test`). Une maille retouchée par
  le joueur garde la hauteur de son époque, sans crête (E si elle l'est entièrement).
- **Inchangé** : le monde bouclé (image la plus proche, tour du monde sans rien reconstruire), la
  mer à 0 m, le raccord sous les tuiles, l'autocontrôle bit à bit.
- **Teinte de débogage** : `bHorizonDebugTint` colore chaque niveau de sa teinte.
- **Hors éditeur** : vérifié en `UnrealEditor.exe TerrainSandbox.uproject <niveau> -game`
  (standalone) : les grilles se construisent (250 ms) et l'horizon s'affiche. Build empaqueté :
  non essayé (assigner `HorizonGpuMaterial`, section 9).
- **Log** : en plus de la section 10, `horizon texels` (envoi des bandes : ticks, ms, texels par
  tick, crêtes comprises), `horizon detail` (du commit d'une époque à l'arrivée de toutes ses
  crêtes et normales) et, toujours, `frames` : thread de jeu, thread de rendu et GPU, comme
  `stat unit`.

### Mesures, GPU contre CPU (UE 5.8, éditeur en PIE, Ryzen 9 9950X3D, `ViewDistanceChunks` 32)

Même marche dans les trois cas (Character, 40 s à 6 m/s puis 60 s à 30 m/s), même machine, même
jour ; « sans » : `bHorizon` désactivé.

| | GPU | CPU (étape 1) | sans |
|---|---|---|---|
| Déplacement d'un niveau, thread de jeu, moyenne par niveau | 0,04 à 0,08 ms | 2,2 à 2,9 ms | – |
| Déplacement d'un niveau, au plus (marche, course) | 0,10 ms, 0,13 ms | 3,0 ms, 4,9 ms | – |
| Envoi des bandes, thread de jeu, par époque | < 0,01 ms | 2,5 ms (conversion et envoi) | – |
| Premier affichage, thread de jeu (660 490 échantillons) | 4,1 à 4,9 ms | 33 à 43 ms | – |
| Époques, du plan à l'application | 11,3 à 11,6 ms | 11,5 à 11,6 ms | – |
| Frames à 6 / 30 m/s, moyenne | 10,5 / 10,7 ms | 10,8 / 11,0 ms | 10,2 / 10,4 ms |
| Frames à 6 / 30 m/s, 99ᵉ centile | 13,6 / 14,6 ms | 14,5 / 22,4 ms | 13,2 / 14,1 ms |
| Temps GPU par frame, à 6 / 30 m/s | 8,0 / 8,1 ms | 8,2 / 8,3 ms | 7,5 / 7,6 ms |
| Mémoire côté CPU | 10,1 Mo d'échantillons | 12 Mo + 87 Mo de meshes | – |
| Mémoire côté GPU | 16,2 Mo (textures 10,1, grilles 6,1) | 35 à 45 Mo (estimation) | – |

- Le temps GPU est celui de `stat unit` (`RHIGetGPUFrameCycles`) : l'horizon coûte environ 0,5 ms
  de GPU par frame (1 018 000 triangles). Le thread de rendu de l'éditeur (environ 10 ms) borne
  les frames dans les trois cas.
- Les grilles sont construites une fois, au `BeginPlay` : 215 à 270 ms sur le thread de jeu,
  pendant le chargement du niveau.
- Génération des tuiles LOD 0 pendant la première époque (0,9 s) : 0,25 ms par tuile en GPU comme
  en CPU, 0,24 ms sans horizon, et de même avec l'éditeur limité à 2 cœurs logiques (0,27, 0,25 et
  0,26 ms). Pas d'effet mesurable de la basse priorité ici : les tuiles du départ sont générées en
  quelques dizaines de millisecondes, bien avant la fin de l'époque.
- Bord est du monde traversé à 100 m/s (500 m d'altitude) : 0,04 à 0,07 ms par déplacement de
  niveau, 0,15 ms au plus ; tour du monde : les 10 niveaux replacés d'un monde en 0,05 ms, sans
  époque (étape 1 : moins de 2 ms).

### Les matériaux et les biomes

Fait sans seconde texture : le dernier flottant d'un texel de terre porte la surface (matériau et
biome) au lieu de la couleur d'altitude, et les tuiles lisent les texels du niveau 0 (section 13).

### Limites

- Le mode CPU (`HorizonRenderer` = CPU) reste pour comparer ; il sera retiré après la fusion du
  nouveau relief (agent 10).
- Silhouettes à la maille de chaque niveau, relevées aux crêtes de leurs mailles
  (`HorizonCrestRatio`) ; les normales de détail éclairent le relief 4 fois plus fin. Seul le coin
  le plus haut d'une maille monte : les fonds de vallée ne montent jamais. Écart de la silhouette
  à la vérité (`heightAt`), moyen / moyen absolu, en degrés (`terrain_export_silhouette`, 120°
  autour du cap) :

  | | vallée du K2 | pied de la plus longue chaîne |
  |---|---|---|
  | points (0) | −0,175 / 0,180 | −0,112 / 0,118 |
  | 2 | −0,110 / 0,133 | −0,075 / 0,091 |
  | 4 (défaut) | −0,090 / 0,118 | −0,057 / 0,080 |

  (relief d'après la 11B ; mêmes chiffres et mêmes images, au bit près, avant et après les crêtes
  différées.) Coût en GPU (`unreal_horizon_bench`, 4 cœurs, GCC, normales à 4), affichage /
  crêtes et normales complètes :

  | | K2, 1 thread | K2, 2 threads | chaîne, 1 thread | chaîne, 2 threads |
  |---|---|---|---|---|
  | crêtes à 2 dans l'époque (avant) | 17,2 / 45,5 s | 8,2 / 21,2 s | 15,7 / 40,2 s | 8,0 / 20,9 s |
  | sans crêtes | 11,8 / 39,1 s | 5,6 / 19,2 s | 11,0 / 35,1 s | 5,7 / 19,6 s |
  | crêtes à 4 après l'époque (défaut) | 11,3 / 36,0 s | 6,5 / 19,1 s | 10,9 / 34,5 s | 5,8 / 17,8 s |

  Marche de 2 km par pas de 8 m, du pas jusqu'au détail complet : 20 à 21 ms par pas au K2, 34 à
  43 ms au pied de la chaîne, avant comme après (à la dispersion près). Même mémoire (10,6 Mo
  d'échantillons). Les crêtes arrivent avec les normales : celles des niveaux lointains (les
  silhouettes) les dernières, environ 25 s après l'affichage sur un thread, 13 s sur deux.
- Matériaux et biomes : dessinés par le matériau temporaire (section 13).
- Les grilles sont construites sur le thread de jeu au `BeginPlay` (0,2 s, une fois) ; elles
  pourraient l'être hors de ce thread si ce temps gêne au chargement.

## 12. Le ciel et la mer de l'horizon (phase 13, étape 3)

- **Pourquoi le ciel devenait noir** : la `SkyAtmosphere` est une sphère du rayon de la planète,
  immobile dans le monde, alors que l'horizon est courbé autour de la caméra. À 5 000 km de son
  sommet, la caméra est pour elle à 1 229 km au-dessus de son sol, dans l'espace (son air s'arrête
  à 60 km) ; à un demi-monde, au-delà de la planète. Sa lumière du ciel (`SkyLight` en capture
  temps réel) est calculée depuis où elle est posée : noire elle aussi, d'où des ombres noires.
- **Le ciel suit la caméra** (`bAtmosphereFollowsCamera`, `Bridge/horizon_atmosphere.hpp`) : au
  `BeginPlay`, la `SkyAtmosphere` (la première du niveau, ou `SkyAtmosphere`) reçoit le rayon de la
  planète et le mode *Planet Top at Component Transform* ; un composant
  (`TerrainAtmosphereFollower`) qui passe après la caméra (`TG_PostUpdateWork`) pose, à chaque
  frame, le sommet de sa planète au niveau de la mer sous la caméra : la même sphère que la
  courbure de l'horizon, dont la mer tombe sur son sol et l'horizon sur le sien. Il pose
  l'`ExponentialHeightFog` sous la caméra à sa hauteur au-dessus de la mer, et la `SkyLight` sur la
  caméra. Après un tour du monde ou un recentrage d'origine, la frame suivante a déjà son ciel
  (vérifié par les positions et des captures). Coût : 0,0015 ms par frame sur le thread de jeu.
- **Perspective aérienne** : son volume (`r.SkyAtmosphere.AerialPerspectiveLUT.Depth`, 96 km par
  défaut) est porté à la portée de l'horizon (748 km avec 10 niveaux), rendu à `EndPlay` : les
  montagnes lointaines bleuissent et s'effacent jusqu'au bout, sans bord visible.
- **Brouillard** : il est plat, l'horizon courbé : à 500 km le sol est 14 km sous lui et serait
  entièrement noyé. `FogEndDistanceKm` (20 km ; 0 : son réglage) borne l'intégration
  (`End Distance`) ; au-delà, la perspective aérienne prend le relais.
- **Mer** (`Scripts/create_horizon_gpu_material.py`, qui reconstruit l'asset en place) : sur
  l'océan, la texture porte le sol sous la mer (`texelGround`) ; la côte est là où le sol
  interpolé passe sous 0, au pixel près (pas de z-fighting ni de trou : un seul maillage). Couleur
  selon la profondeur (sable sous l'eau, turquoise, bleu profond ; `SeaDeepColor`, `SeaOpenColor`,
  `SeaShallowColor`), 14 vagues sinusoïdales fixes dans le monde (période 4 096 m, qui divise
  chaque côté du monde : rien ne bouge après un tour du monde ; `WaveOriginX/Y` suit le
  recentrage), qui s'effacent dans la rugosité selon la taille du pixel (aucune répétition au
  loin, un chemin de soleil plus large), reflet du ciel (Fresnel, ciel lu dans la table de vue de
  la `SkyAtmosphere`) et reflet du soleil (GGX) en émissif, sans spéculaire du moteur (aucun reflet
  Lumen à tracer). Le carré de mer sous les tuiles lit le sol du niveau 0 : même mer près et loin ;
  il est masqué (matériau Masked) là où moins de la moitié des 4 texels du niveau 0 autour du
  pixel sont de l'océan. Le fond marin n'a sa couleur que sous le drapeau mer du core.
  Les normales penchent avec la courbure (d / R). Seuls les pixels de mer paient ces calculs.
- **Mesures** (PIE 2042 × 1752, 3,6 Mpx, autant que le 1440p ; même session, A sans ces
  changements, B avec, GPU saturé à 200 % de résolution) : GPU +0,1 à +0,6 ms à 4 fois les pixels
  du 1440p, soit environ +0,03 à +0,15 ms en 1440p ; déplacement d'un niveau inchangé (0,05 ms en
  moyenne sur le thread de jeu, mêmes pointes que sur `main`).
- **Limites** : un seul ciel pour la première caméra locale (écran partagé : non) ; le reflet de
  la mer ne voit que le ciel (ni relief, ni nuages) ; le reflet du soleil ignore les ombres ; la
  mer reste opaque (transparence, caustiques, écume : phase 14) ; avec la caméra du Game Animation
  Sample, une frame est vue d'ailleurs pendant un recentrage d'origine (la caméra du jeu, pas le
  ciel ; une `CameraActor` ne le fait pas).

## 13. Le matériau temporaire du terrain (phase 14, provisoire)

En attendant les vrais matériaux de la phase 14 : roche grise sur les pentes raides, neige sur le
sol froid peu pentu, herbe, herbe sèche, forêt, alpage, terre, sable, désert et éboulis selon les
matériaux et biomes du core, avec des plaques dans chaque biome et des textures sans répétition
visible, les mêmes de près (tuiles, `M_TemporaryTerrain`) et de loin
(`M_TerrainHorizonGpu`). Sur les tuiles, les biomes et matériaux se fondent selon les poids du
core (`TileData::surfaceBlend`, une texture commune `TileBlendTexels`). Ses textures viennent du pack **Stylescape HD** (licence Fab : jamais
dans ce dépôt) copié sous `/Game/StylescapeHD` du projet ; sans lui, des couleurs unies. Tout est
décrit dans [`TEMPORARY_MATERIAL.md`](TEMPORARY_MATERIAL.md) : règles, installation du pack,
réglages, coût, limites, et trois défauts de rendu (le trait sombre au bord des tuiles, corrigé
par le fondu de `HorizonDropCm` ; les dents de scie sous un soleil bas, corrigées par des
normales lissées ; la « ligne d'ombre » au pied de la chaîne, une arête du relief).

## 14. L'eau à son niveau (phase 11, tâche 7b ; agent 27)

Les lacs et fleuves sont dessinés à leur niveau : sur les tuiles, une surface translucide tirée des
colonnes d'eau du core (`TileData::waterColumns`), en pente avec les fleuves, plate sur les lacs,
sans fente entre tuiles ni entre LOD ; aux chutes, un rideau de la lèvre au pied et de l'écume
(`TileData::falls`) ; sur l'horizon GPU, l'eau montée au niveau de ses textures d'eau (R32F, le
niveau du core au bit près), qui se fond dans celle des tuiles sur leur bande extérieure. Même
aspect de près et de loin : couleur et transparence selon l'épaisseur d'eau et le lieu, rides de
bruit dans le sens du courant, reflet du ciel et du soleil, écume des rapides, rives fondues. Il faut un fichier planète (`PlanetFile`, section 5,
par défaut dans `Saved/TerrainEngine`) et les matériaux de `Scripts/create_water_materials.py`.
Données, formats, aspect, coût, évolution vers Single Layer Water et limites :
[`WATER_LEVEL.md`](WATER_LEVEL.md). La sonde `terrain_probe_water` dit ce que le core met en un
point, `unreal_water_probe` ce que le Bridge en fait (chutes, rideaux, mailles coupées par LOD).

## 15. Les modifications vues de loin (phase 15)

En attendant « casser et poser » en jeu, des modifications pour essayer : les tuiles au-dessus sont
générées à nouveau (elles restent montrées jusqu'à leur nouvelle version), et l'horizon lointain les
montre, en GPU comme en CPU.

- **Console** (en jeu, `²` ou `` ` ``) : `Terrain.Edit` modifie le sol devant la caméra, à une
  distance comptée à l'horizontale le long de sa direction, en mètres :
  - `Terrain.Edit raze 5000 1000 500` : un carré de 1 000 m de côté à 5 km, rasé jusqu'à 500 m
    sous son point le plus bas (sans profondeur : 10 m) ;
  - `Terrain.Edit blast 2000 32` : une explosion de 32 m de rayon sur le sol, à 2 km ;
  - `Terrain.Edit heap 2000 60` : un tas de roche, une sphère de 60 m de rayon posée un tiers sous
    le sol.
  La commande rend la main tout de suite : la modification part sur le fil de modifications de
  la session (`Bridge/edit_thread.hpp`), qui calcule son volume, l'écrit dans les masques des
  tuiles puis lit le sol modifié pour l'horizon. À chaque image, le thread de jeu reprend les
  modifications finies, dans leur ordre : il fait générer à nouveau leurs tuiles et les passe à
  l'horizon (`TickTerrainEdits`). Les bits sont ceux du chemin synchrone. Le log
  (`LogTerrainEngine`) note la mise en file, puis, une fois finie, les chunks à générer, le
  temps passé sur le fil de modifications et celui passé sur le thread de jeu.
- **Blueprint** : `Apply Volume Edit` (boîte, sphère ou explosion, retirer, ajouter ou rendre le
  sol d'origine, en coordonnées du monde Unreal) et `Edit Ahead` (comme la console). Elles
  renvoient vrai quand la modification est en file ; un refus arrive dans le log.
- **L'horizon** (`terrain_core` `streaming/horizon_edits.hpp`) : le niveau 0 montre sur chaque
  colonne de 8 m le sommet de la tuile LOD 3 (au raccord : pas de marche ; un fond rasé paraît 0,5 à
  7,5 m plus haut que le vrai, comme sur les tuiles) ; chaque niveau plus grossier, le changement
  moyen du sol de ses mailles, borné par le sol modifié quand toute la maille l'est (un fond rasé
  reste plat, crêtes comprises). La modification est appliquée entre deux époques : l'époque
  suivante ne recalcule et n'envoie que les bandes autour d'elle. Le matériau ne change pas (le sol
  d'origine, comme les tuiles).
- **Normales de détail** (GPU) : elles sont calculées sans les modifications ; là où au moins la
  moitié d'une maille est modifiée, ses texels prennent les pentes de la grille, qui suit les
  modifications (interpolées entre les 4 échantillons de sa maille, comme la pente lissée des
  tuiles). Jamais « en attente » : le filtre bilinéaire du matériau mêlerait un texel en attente
  aux texels voisins, et un faible poids donnerait une pente prise pour vraie, une normale
  couchée éclairée par le ciel seul (une ligne couleur ciel le long des coins d'un bloc rasé).
- **Coût mesuré** (Linux, `horizon_edits_cost_test`) : une explosion de 64 m se voit de loin 3 à
  4 ms après elle (sans époque en vol ; sinon après la fin de celle-ci) ; une montagne rasée
  (1 km × 1 km × 500 m) : environ 0,25 s pour lire son sol, 0,1 à 0,2 s pour son époque si le
  niveau 0 la contient, 1 Mo de résumé. Le rasage du K2 (`Terrain.Edit raze 11300 1000 500`
  depuis sa vallée, `unreal_horizon_edits_cost_test`) : la commande prend 0,03 ms sur le thread
  de jeu, puis au plus 6,4 ms par image (reprendre la modification : 1,5 ms) ; il se voit de loin
  20 ms après la fin du fil de modifications.
- **Limites** : entre deux colonnes, un mur de modification coupe le bord des tuiles plus raide
  que la grille de 8 m de l'horizon : un écart de quelques mètres au plus, sur une maille (pas de
  trou : l'horizon passe sous les tuiles) ; sous la bande des tuiles LOD 3, l'horizon peut
  dépasser de la paroi d'un cratère de plus de `HorizonDropCm` ; un tas sorti de la mer reste
  dessiné en mer ; une très grosse modification met quelques centaines de millisecondes à
  sortir du fil de modifications (sans bloquer le jeu), et les modifications suivantes
  attendent leur tour.
