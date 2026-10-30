# L'eau dans Unreal à son niveau (phase 11, tâche 7b ; agent 27)

Le core sait où est l'eau et à quel niveau : la mer à 0 m, un lac à son niveau, un fleuve à sa
surface, en marches aux chutes. Unreal la dessine maintenant : la surface des lacs et des fleuves
des tuiles, les rideaux et l'écume des chutes, et les lacs et fleuves de l'horizon GPU, au même
niveau. La mer reste celle de l'horizon (README, section 12). Il faut un fichier planète
(`PlanetFile`, section 5 du [README](README.md)) : sans lui, pas de fleuves, de lacs ni de chutes,
seulement la mer.

## Ce qu'il faut faire une fois par projet

1. Lancer `Scripts/create_water_materials.py` (Tools → Execute Python Script…) : il crée
   `/Game/TerrainEngine/M_TerrainWater` (la surface de l'eau des tuiles) et `M_TerrainFalls` (les
   rideaux et l'écume des chutes). Sans eux, les tuiles ne dessinent pas d'eau, avec un
   avertissement dans le log.
2. Relancer `Scripts/create_horizon_gpu_material.py` : `M_TerrainHorizonGpu` lit maintenant
   `HorizonWater` et `CoarseWater`.
3. Le fichier planète : par défaut `Saved/TerrainEngine/planet_{Seed}.bin` dans le dossier du
   projet. S'il manque, ou s'il est périmé (une autre seed, un autre monde, une autre version du
   relief ou d'autres paramètres) ou abîmé, le premier Play le construit et l'écrit (le jeu
   attend). Le log dit toujours ce qui s'est passé :
   - `planet level read from … in 2.7 s` : lu ;
   - `planet level BUILT (no file | <file>: stale: … | …) in … s, written to …` : construit, et
     pourquoi (un avertissement), avec la mémoire du processus avant et après.
   Pour éviter l'attente : `terrain_make_planet <projet>/Saved/TerrainEngine/planet_20261019.bin
   --threads 4` (sous Linux, 4 cœurs : 119 s, 412 Mo au plus, 247 Mo sur le disque). Un chemin
   absolu (`C:/TerrainEngine/planet.bin`) est pris tel quel ; vide : le relief seul.

## Ce que le core donne (déterministe, au bit près)

| Donnée | Où | Contenu |
|---|---|---|
| Colonnes d'eau d'une tuile | `TileData::waterColumns` | les 17 × 17 colonnes de la tuile : type (fleuve, lac, lac salé, mer) et niveau de `HeightSample` au bit près ; sur les fleuves, le sens du courant (tangente du tronçon le plus proche, `Hydrology::reach`) ; vide sans lac ni fleuve |
| Chutes d'une tuile | `TileData::falls` | les chutes dont la lèvre est dans la tuile : lèvre, pied (`RiverFall::footX/Z`), eau au-dessus et en dessous, largeur |
| Niveau aux sommets | `TileData::waterLevels`, canal UV 0 du sol | inchangé (ci-dessous) |
| Niveau de l'horizon | `HorizonCell::waterLevel`, textures `HorizonWater` | le niveau du core en float 32 bits |

Rien de tout cela ne change le monde : golden du monde, montagnes gelées, somme du niveau Planète
identiques. Sonde : `terrain_probe_water --planet <fichier> <x> <z>…` imprime, pour chaque
point, la colonne, les vallées et le lac du core, les niveaux des sommets des tuiles LOD 0 à 3,
l'échantillon de l'horizon et les chutes autour.

## Ce qu'Unreal en fait

**La surface de l'eau des tuiles** (`Bridge/tile_water_mesh.hpp`, section 1 du mesh de chaque
tuile, matériau `M_TerrainWater`)
- Une grille sur les colonnes de la tuile, déjà au niveau de l'eau (calculée par le CPU : les
  bornes la contiennent, sans World Position Offset). Une maille est dessinée dès qu'un coin est
  sous un lac ou un fleuve ; la mer est laissée à l'horizon.
- Coin mouillé : le niveau de sa colonne. Coin sec : le niveau moyen des coins mouillés de sa
  maille, l'eau passe sous la rive et le sol la cache. Lacs plats, fleuves en pente avec leur lit.
- Une maille plus raide que 1,5 (56°) est coupée : c'est la face d'une chute, que son rideau
  dessine.
- Raccords : deux tuiles d'un même LOD ont les mêmes colonnes au bit près sur leur bord ; du côté
  d'une tuile plus grossière, les colonnes impaires prennent la moyenne de leurs voisines paires,
  sur l'arête droite de la maille grossière ; le bord d'une tuile LOD 3 a exactement les niveaux
  des texels d'eau de l'horizon (`unreal_tile_water_test`).
- Canaux UV : UV0 le sens du courant (X, Y d'Unreal, 0 sur un lac), UV1 (type : 1 fleuve, 2 lac,
  3 lac salé ; pente de l'eau en m/m), UV2 (altitude de l'eau en m).
- Coût : environ 0,01 ms par tuile avec de l'eau sur le thread de jeu (conversion).

**Les chutes** (`Bridge/fall_curtains.hpp`, section 2, matériau `M_TerrainFalls`)
- Pour chaque chute dont la pente moyenne dépasse 1,5 : un rideau large de 0,9 fois la largeur du
  chenal, de l'eau à la lèvre jusqu'à 0,5 m sous l'eau du pied, 0,5 m devant la ligne lèvre-pied
  et bombé comme l'eau qui quitte la lèvre (une part √t du chemin après une part t de la chute) ;
  un disque d'écume au pied, sur l'eau du bassin.
- Il ne lit que la lèvre, le pied, les niveaux et la largeur de la chute, jamais la forme de la
  roche : une fente refaite (agent 20, lèvre en surplomb, niche derrière) le garde devant la lèvre ;
  une cuvette plus profonde au pied ne change rien.
- Canaux UV : UV0 (mètres en travers, mètres depuis la lèvre ; sur l'écume, mètres depuis son
  centre), UV1 (4 rideau ou 5 écume, hauteur de la chute), UV2 (altitude, largeur du rideau ou
  rayon de l'écume).

**L'horizon GPU** (`Scripts/horizon_water_hlsl.py`, `horizon_water_nodes.py`)
- Un nœud de sommets à côté de `HorizonVertex` lit le niveau d'eau du niveau et du niveau plus
  grossier, avec le même géomorphing ; sur un lac ou un fleuve, il monte le sommet au niveau de
  l'eau (jamais sous le sol), avec la même descente et la même courbure que le sol.
- Sous les tuiles (dans `DropCoveredCm` du centre du niveau 0), rien ne monte : les tuiles y
  dessinent leur eau, l'horizon garde son sol dessous ; les mailles à cheval sur le bord des tuiles
  sont coupées (masque). Au bord des tuiles, l'eau de l'horizon commence exactement au niveau de
  la leur.
- Type pour la couleur : fleuve là où le core dit que le sol est de l'eau, lac salé dans un désert,
  lac ailleurs.

**L'aspect** (`Scripts/water_look_hlsl.py`, le même code et les mêmes paramètres pour les tuiles et
l'horizon : pas de saut d'aspect au bord des tuiles)
- Couleur de l'eau profonde et absorption par mètre selon le type et le lieu : fleuve brun-vert
  (limon), lac vert, lac de montagne turquoise (de `GlacierFromM` 1 400 m à `GlacierFullM`
  2 600 m), lac salé turquoise pâle. Le lit s'efface à travers l'épaisseur d'eau
  (exp(−absorption × épaisseur)) : les hauts-fonds montrent leur fond, l'eau profonde sa couleur.
  Tuiles : l'épaisseur derrière chaque pixel (profondeur de la scène moins celle du pixel) ;
  horizon : la profondeur divisée par le cosinus de la vue.
- Translucide sur les tuiles (Surface ForwardShading), opaque sur l'horizon (comme la mer).
- Rides : 8 sinusoïdes fixes dans le monde (période 4 096 m, rien ne bouge après un tour du monde),
  de 5,6 à 0,35 m, qui s'effacent dans la rugosité selon la taille du pixel ; sur les fleuves,
  entraînées dans le sens du courant (`FlowSpeed`, deux phases décalées d'un demi-cycle).
- Reflet du ciel (Fresnel, table de vue de la `SkyAtmosphere`) et du soleil (GGX), comme la mer.
- Écume des rapides selon la pente de l'eau (`FoamFromSlope`, `FoamFullSlope`).
- Rideaux : traînées qui tombent avec l'eau (décalées du temps de chute, jamais étirées), douces
  contre la roche et l'eau.
- Aucune texture : rien à licencier.
- Réglages (instances) : `RiverWaterColor`, `LakeWaterColor`, `GlacierWaterColor`,
  `SaltWaterColor` et leurs `…Absorption`, `FoamColor`, `CurtainColor`, `GlacierFromM`,
  `GlacierFullM`, `RippleStrength`, `WaterAlpha`, `FlowSpeed`, `FoamFromSlope`, `FoamFullSlope`.

**Pour comparer** : la commande `Terrain.Water 0|1` (ou `SetWaterShown`) cache ou montre l'eau des
tuiles et des chutes et les lacs et fleuves de l'horizon ; `bWater` faux sur l'acteur : comme avant.

## Vues pour vérifier (`planet.bin` chargé, seed 20261019)

Coordonnées du core (x, z en m) ; en posant `CoreOriginX/Z` sur le point, l'œil est en (0, 0) dans
Unreal, à 100 × (altitude de l'œil − sol du point) cm. Cap en degrés dans le sens des aiguilles
d'une montre depuis le nord (−z du core, −Y d'Unreal) : le yaw d'Unreal vaut cap − 90.

| Vue | Point (x ; z) | Sol | Œil (altitude) | Cap | Inclinaison | Ce qu'on doit voir |
|---|---|---|---|---|---|---|
| Grand fleuve de plaine | −241 933 ; −90 181 (rive) | 3,98 m | 5,7 m | 290° | −2° | fleuve de 305 m (1 887 m³/s) à 3,28 m, plaine de 2,4 km ; rides vers l'aval (337°) |
| Lac de montagne, 500 m | 7 369 622 ; −7 087 959 | 1 865,5 m | 1 867,2 m | 337,5° | −12° | lac à 1 725,26 m, plat, tuiles translucides |
| Lac de montagne, 5 km | 7 371 344 ; −7 083 802 | 3 572,4 m | 3 574 m | 337,5° | −20° | le même lac sur l'horizon, au même niveau, turquoise |
| Chute de 388 m | −6 185 104 ; 155 924 (bassin) | 178,8 m | 182 m | 227° | +30° | bassin en eau à 180,03 m, rideau de la face de 73 m, bassin à 253,54 m au-dessus, écume |
| Côte | −6 184 762 ; 155 681 | 9,6 m | 11,3 m | 50° | −2° | la mer de l'horizon, le fleuve qui la rejoint |

Les niveaux du core en ces points : `terrain_probe_water --planet <fichier> <x> <z>`. Sur la
chute : lèvre (−6 185 411 ; 156 218) à 568,99 m, bassins au pied des faces de 90 et 73 m à 253,54
et 180,03 m (−174,5 et −248 m dans Unreal, origine à 428 m), en eau aux LOD 0 à 3.

## Coût GPU (à mesurer dans Unreal)

Comme pour le matériau temporaire : même résolution (PIE 2042 × 1752 ou 1440p), même session de
l'éditeur, `Terrain.Water 0` puis `Terrain.Water 1`, A B A B, temps GPU moyen par frame (profil
CSV ou `stat unit`), sur quatre vues : le fleuve de plaine, le lac de montagne à 500 m, la côte, la
chute. Le surcoût attendu vient des pixels d'eau des tuiles (translucides, 8 sinusoïdes, deux
phases sur les fleuves) et des pixels de l'horizon sous l'eau ; rien sur la terre sèche.

## Formats (inchangés pour le sol)

**Tuiles et horizon CPU : le canal UV 0 du sol** (`TexCoord[0]`)
- `x` : la hauteur de l'eau au-dessus du sommet, en mètres ; négative quand le sommet est
  au-dessus du niveau qu'on lui donne (une berge, un bloc posé dans un lac) ;
- `y` : 1 sous l'eau (mer, lac, fleuve), 0 au sec (et `x` vaut 0).
- Le niveau, en cm d'Unreal : le Z monde du sommet + 100 × `x`. Les UV sont des demi-flottants :
  la hauteur au-dessus du sommet garde le centimètre près des rives.

**Horizon GPU : une texture d'eau par niveau**
- 257 × 257 texels `PF_R32_FLOAT`, au plus proche, bouclée et adressée comme la texture des texels
  (l'échantillon (i, k) de la fenêtre au texel (premier emplacement + (i, k)) mod 257), écrite par
  bandes en même temps qu'elle (`Bridge/horizon_textures.hpp`).
- Le texel : le niveau du core en mètres, au bit près (la mer : 0) ; `kNoWaterLevel` (−1e9) au
  sec : un matériau prend un texel sous −1e8 (`kWetWaterTexel`) pour du sec. Avant : un code de
  16 bits par pas de 0,25 m (jusqu'à 12,5 cm d'écart avec les tuiles).
- Paramètres : `HorizonWater` (le niveau) et `CoarseWater` (le niveau plus grossier), lus par
  `Load`. Les texels du sol ne changent pas : leur 4e flottant garde le code d'eau (≥ 128 sous un
  lac ou un fleuve, le sol sous la mer, négatif, sur l'océan).

**Mémoire** : textures d'eau 2,6 Mo sur le GPU (10 niveaux, 4 octets par texel, +1,3 Mo), +2
octets par texel envoyé ; colonnes d'eau 16 octets × 289 par tuile qui a de l'eau douce (les
autres : rien) ; sections d'eau : 4 sommets au plus par colonne mouillée.

## Évolution prévue : Single Layer Water (phase 14)

Le matériau de l'eau des tuiles est à part (son script, ses paramètres) : il pourra passer au
modèle Single Layer Water d'Unreal (réfraction, absorption physique, reflets Lumen ou SSR des
rives et des montagnes) sans toucher au reste. Il faudra alors régler au raccord tuiles / horizon
(vers 500 à 850 m) :
- la couleur : régler l'absorption et la diffusion de Single Layer Water pour retrouver celles de
  `water_look_hlsl.py` que l'horizon garde, ou faire calculer à l'horizon une couleur équivalente ;
- les reflets : de près les montagnes, au loin le ciel seul (l'horizon n'a pas de reflet de scène) ;
  un fondu vers le reflet du ciel dans la bande extérieure des tuiles, ou des reflets d'écran sur
  l'horizon ;
- les pixels coupés de l'horizon sous la bande des tuiles (masque) : Single Layer Water voit la
  scène derrière l'eau, il faudra vérifier que l'horizon n'y apparaît pas ;
- le coût du passage Single Layer Water (à mesurer sur les mêmes vues que ci-dessous).

## Limites

- Pas de rideau sur l'horizon : une chute vue de plus de 850 m n'a que ses marches d'eau.
- Les fleuves étroits ne sont que des points sur les niveaux grossiers de l'horizon (un
  échantillon tous les 16, 32 m…).
- L'horizon CPU (`HorizonRenderer` = CPU, pour comparer) ne dessine pas les lacs et fleuves.
- L'eau ne se voit pas de dessous ; les lacs des grottes (`TileData::water`) ne sont pas dessinés.
- L'eau est statique : les modifications des joueurs ne changent pas son niveau (un trou au bord
  d'un lac reste sec, un bloc posé dans un lac est sous l'eau).
- À l'embouchure, le fleuve translucide rejoint la mer opaque de l'horizon : changement d'aspect.
- Les rides des tuiles ne suivent pas le recentrage d'origine d'Unreal (`WaveOrigin`) : elles
  peuvent sauter à un recentrage, rare.
- Sur l'horizon, les crêtes relevées (`HorizonCrestRatio`) peuvent poser un sol au-dessus de son
  lac : l'eau y est cachée.
- Le premier Play avec un fichier planète manquant ou périmé attend la construction (quelques
  minutes pour la planète entière).
