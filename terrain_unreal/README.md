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
   `Plugins/terrain-engine/terrain_unreal/Scripts/create_terrain_material.py`. Il crée
   `/Game/TerrainEngine/M_TerrainAltitude`, un matériau éclairé qui affiche la couleur des sommets
   (sable au bord de la mer, vert, gris en altitude, blanc sur les sommets). Sans lui, l'acteur
   prend le matériau de debug d'Unreal qui affiche les couleurs de sommet, et le dit dans le log.
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
| `CoreOriginX`, `CoreOriginZ` | 0, 0 | point du core (m) dont le sol est à l'origine d'Unreal |
| `Lod0/1/2DistanceChunks`, `ViewDistanceChunks` | 4, 8, 16, 32 | anneaux de LOD en chunks de 16 m ; Lod1 − Lod0 ≥ 2, Lod2 − Lod1 ≥ 4 |
| `WorkerThreads` | 2 | threads de génération |
| `MaxTileUploadsPerFrame` | 8 | budget de tuiles converties par frame |
| `MeshType` | Procedural Mesh | type de mesh (Dynamic Mesh, Realtime Mesh : emplacements) |
| `Material` | aucun | matériau des tuiles (voir 4.2) |
| `bAsyncCollisionCooking` | non | cuisson des collisions hors du thread de jeu |
| `FocusActor` | aucun | acteur suivi ; sinon le pion du premier joueur, ou sa caméra |
| `bSnapPawnToGround` | oui | pose sur le sol un nouveau pion qui est dessous |
| `WarmUpRadiusChunks`, `WarmUpTimeoutSeconds` | 1, 10 | tuiles LOD 0 générées avant qu'un nouveau pion bouge |
| `TimingLogIntervalSeconds` | 5 | intervalle du résumé des mesures (0 : aucun) |

## 6. Comment c'est construit

```
terrain_unreal/
  TerrainEngine.uplugin        module Runtime (Win64, Linux, Mac), plugin ProceduralMeshComponent
  Scripts/create_terrain_material.py
  Source/TerrainEngine/
    TerrainEngine.Build.cs
    Public/TerrainWorld.h      ATerrainWorld, ETerrainMeshType
    Private/
      TerrainWorld.cpp         cycle de vie, tick, démarrage d'un pion, mesures
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
`unreal_core_sources`.

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
- **Reste à faire** (`TODO(phase 10, crossing the edges)` dans `Bridge/unreal_frame.hpp`) : les
  Large World Coordinates d'Unreal vont jusqu'à environ 88 000 km (`UE_LARGE_WORLD_MAX`). Un
  voyage de plus d'un tour du monde demandera de ramener le joueur, les tuiles et le repère d'un
  monde entier.

## 9. Pas encore fait

Type de mesh autre que le Procedural Mesh Component (emplacements Dynamic Mesh et Realtime Mesh),
eau, lave, plantes, matériaux du core, retour du joueur d'un monde entier après un long voyage
(voir 8). En build empaqueté,
assigne le matériau explicitement dans `Material` : le chemin chargé par défaut n'est cuit que
s'il est référencé.
