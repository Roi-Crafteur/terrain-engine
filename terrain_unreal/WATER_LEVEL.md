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
identiques. Sonde : `terrain_probe --planet <fichier> <x> <z>…` (qui remplace
`terrain_probe_water`, agent 37) imprime, pour chaque point, la colonne, les vallées et le lac du
core (section `water`), les niveaux des sommets des tuiles LOD 0 à 3 et l'échantillon de l'horizon
(section `render`) et les chutes autour (section `falls`). Côté Bridge, `unreal_water_probe --planet
<fichier> <x> <z>…` (exécutable des tests, lancé à la main) imprime pour la tuile de chaque LOD qui
tient le point : colonnes mouillées, chutes dont la lèvre est dans la tuile (lèvre, pied, chute,
course, pente moyenne, rideau courbe ou droit), triangles de la surface, mailles coupées et sommets
effacés, triangles des rideaux et de l'écume (ou pourquoi il n'y a pas de rideau) ; pour chaque
chute de la tuile LOD 0, trois vues vérifiées contre le sol (du bassin, de côté, de dessus).

## Ce qu'Unreal en fait

**La surface de l'eau des tuiles** (`Bridge/tile_water_mesh.hpp`, section 1 du mesh de chaque
tuile, matériau `M_TerrainWater`)
- Une grille sur les colonnes de la tuile, déjà au niveau de l'eau (calculée par le CPU : les
  bornes la contiennent, sans World Position Offset). Une maille est dessinée dès qu'un coin est
  sous un lac ou un fleuve ; la mer est laissée à l'horizon.
- Coin mouillé : le niveau de sa colonne. Coin sec : le niveau moyen des coins mouillés de sa
  maille, l'eau passe sous la rive et le sol la cache. Lacs plats, fleuves en pente avec leur lit.
- Une maille plus raide que 0,7 (35°, la pente des marches d'une chute dans le core) est coupée :
  c'est la face d'une chute, que son rideau dessine ; les rapides restent dessinés. Les coins des
  mailles coupées ont un bord à 0 (UV2.y), les autres 1 : l'eau s'efface sur la maille avant la
  coupe, sans dents de scie ni écume de pente au bord de la face. La pente de l'eau (normale,
  écume) n'est jamais mesurée à travers une coupe.
- Raccords : deux tuiles d'un même LOD ont les mêmes colonnes au bit près sur leur bord ; du côté
  d'une tuile plus grossière, les colonnes impaires prennent la moyenne de leurs voisines paires,
  sur l'arête droite de la maille grossière ; le bord d'une tuile LOD 3 a exactement les niveaux
  des texels d'eau de l'horizon (`unreal_tile_water_test`).
- Canaux UV : UV0 le sens du courant (X, Y d'Unreal, 0 sur un lac), UV1 (type : 1 fleuve, 2 lac,
  3 lac salé ; pente de l'eau en m/m), UV2 (altitude de l'eau en m ; bord : 0 au coin d'une maille
  coupée, 1 ailleurs).
- Coût : environ 0,01 ms par tuile avec de l'eau sur le thread de jeu (conversion).

**Les chutes** (`Bridge/fall_curtains.hpp`, section 2, matériau `M_TerrainFalls`)
- Pour chaque chute de `TileData::falls` : un rideau large de 0,9 fois la largeur du chenal, de
  l'eau à la lèvre jusqu'à 0,5 m sous l'eau du pied, 0,5 m devant la ligne lèvre-pied ; bombé
  comme l'eau qui quitte la lèvre (une part √t du chemin après une part t de la chute) si la pente
  moyenne dépasse 1,5, droit de la lèvre au pied en dessous ; un disque d'écume sur l'eau du
  bassin, de 0,75 fois la largeur en rayon, centré 0,35 rayon en aval du pied du rideau (il couvre
  le bassin, pas le dessous de la face).
- L'écume : pleine près du centre, un bord fondu et déchiqueté par le bruit, jamais de trou (l'eau
  profonde du bassin, presque noire vue de dessus, s'y voyait en points noirs) ; plus mince là où
  l'eau sous elle devient peu profonde (`FoamDepthM` = 0,8 m, profondeur de la scène derrière le
  pixel) : elle prend la forme du bassin et s'efface vers ses rives et sa paroi, sans coupe nette.
- Il ne lit que la lèvre, le pied, les niveaux et la largeur de la chute, jamais la forme de la
  roche : une fente refaite (agent 20, lèvre en surplomb, niche derrière) le garde devant la lèvre ;
  une cuvette plus profonde au pied ne change rien.
- Canaux UV : UV0 (mètres en travers, mètres depuis la lèvre ; sur l'écume, mètres depuis son
  centre), UV1 (4 rideau ou 5 écume, hauteur de la chute), UV2 (altitude, largeur du rideau ou
  rayon de l'écume). Jusqu'à `9db1286`, UV2.y valait 0 : le matériau prenait une largeur de 10 cm,
  le rideau était transparent et l'écume réduite à 10 cm.
- La chute de 388 m (`unreal_water_probe`) : aux LOD 0 à 3, chaque tuile qui tient une lèvre a son
  rideau (faces de 80,89 et 73,51 m : 21 et 19 rangs, 42 et 38 triangles, plus 16 d'écume) ; les
  petites marches de 7 à 13 m ont 2 rangs.

**L'horizon GPU** (`Scripts/horizon_water_hlsl.py`, `horizon_water_nodes.py`)
- Un nœud de sommets à côté de `HorizonVertex` lit le niveau d'eau du niveau et du niveau plus
  grossier, avec le même géomorphing ; sur un lac ou un fleuve, il monte le sommet au niveau de
  l'eau (jamais sous le sol), avec la même descente et la même courbure que le sol.
- Sous les tuiles, rien ne monte : les tuiles y dessinent leur eau, l'horizon garde son sol
  dessous, ses pixels d'eau sont coupés (masque). Sur les derniers `DropFadeCm` (64 m) avant le
  bord de la zone des tuiles (`DropCoveredCm` du centre du niveau 0), les deux eaux se partagent
  les pixels :
  - l'eau de l'horizon y monte à son vrai niveau, celui de l'eau des tuiles. La descente du sol
    sous les tuiles (`DropCm`) est reprise sur l'eau, sur une maille de plus pour qu'aucune maille
    de la bande ne soit à demi montée ;
  - la part de l'horizon s va de 0 à 1 (`FTerrainWater::HorizonShare`). Les tuiles la calculent
    de leur position au-dessus du niveau 0, l'horizon de sa position dans sa grille : même endroit,
    même part ;
  - un seuil par pixel (`FTerrainWater::Dither`, même code dans les deux matériaux, bruit de
    gradient entrelacé décalé chaque 1/60 s) départage les pixels : l'horizon garde ceux sous s,
    les tuiles les autres ;
  - chaque pixel montre une seule eau avec tout son aspect, et TSR les mélange. Jusqu'à
    `31389d2`, l'eau des tuiles s'effaçait (opacité × (1 − s)) : on voyait le lit à travers ou
    deux reflets, d'où une bande plus claire ou plus sombre.

  Au-delà, l'horizon seul. Les réglages de l'aspect (couleurs, absorptions, `SkyReflection`,
  `WaterAlpha`…) sont copiés de l'instance de l'eau des tuiles sur chaque niveau de l'horizon
  (`UTerrainHorizonClipmap::CopyLook`) : un réglage vaut pour les deux.
- Rives : un échantillon sec voisin de l'eau prend pour profondeur le niveau de son voisin le plus
  haut moins son sol (au lieu de −1 m) ; la rive passe là où le sol de l'horizon rencontre l'eau,
  plus sur les diagonales des mailles.
- Type pour la couleur : fleuve là où le core dit que le sol est de l'eau, lac salé dans un désert,
  lac ailleurs.

**L'aspect** (`Scripts/water_look_hlsl.py`, le même code et les mêmes paramètres pour les tuiles et
l'horizon : pas de saut d'aspect au bord des tuiles)
- Couleur de l'eau profonde et absorption par mètre selon le type et le lieu : fleuve brun-vert
  (limon), lac vert, lac de montagne turquoise (de `GlacierFromM` 1 400 m à `GlacierFullM`
  2 600 m), lac salé turquoise pâle. Le lit s'efface à travers l'épaisseur d'eau
  (exp(−absorption × épaisseur)) : les hauts-fonds montrent leur fond, l'eau profonde sa couleur.
  Tuiles : l'épaisseur le long du rayon derrière chaque pixel (profondeur de la scène moins celle
  du pixel, ramenée à la longueur du rayon) ; horizon : la profondeur divisée par le cosinus de la
  vue. La même épaisseur apparente des deux côtés.
- Translucide sur les tuiles (Surface ForwardShading), opaque sur l'horizon (comme la mer).
- Rides : trois octaves de bruit de valeur (mailles de 3,1, 1,3 et 0,55 m) sur des réseaux
  tournés (0,59, 1,89 et 3,68 rad : ni rayures ni grille), chacune dérivant le long de son axe ;
  périodiques sur 4 096 m en X et Y (rien ne bouge après un tour du monde ni au recentrage
  d'Unreal par périodes entières), gradient exact ; chacune s'efface dans la rugosité quand un
  pixel couvre plus de la moitié de sa maille, et ce qu'une octave gardée varie dans un pixel
  passe aussi dans la rugosité (antialiasing spéculaire : pas d'amas de points du soleil vus de
  haut). Sur les fleuves, entraînées dans le sens du courant
  (`FlowSpeed`, deux phases décalées d'un demi-cycle). Avant : 8 sinusoïdes, lues en rayures
  régulières vues de 150 à 600 m.
- Reflet du ciel (Fresnel, table de vue de la `SkyAtmosphere`, × `SkyReflection` = 0,6) et du soleil
  (GGX), comme la mer ; le soleil n'est pas réduit. Un lac reste bleu-vert vu de loin.
- Rives : l'eau des tuiles s'efface (opacité, donc reflet) sur `ShoreFadeM` = 1,5 m d'eau le long
  de la vue ; l'horizon de même. Le bord de l'eau ne suit plus les triangles du sol.
- Bord des tuiles : sur la moitié extérieure de leur bande au-dessus du niveau 0, l'eau des tuiles
  laisse à l'horizon les pixels sous sa part (ci-dessus). Le matériau des tuiles est une instance
  que l'horizon place (`SurfaceOrigin`, `SurfaceCells`, `SurfacePitchCm`, `SurfaceCoveredCm`,
  `SurfaceBandCm`, `HasSurface`, comme le matériau du sol) ; sans horizon, l'eau des tuiles seule.
- Écume des rapides selon la pente de l'eau (`FoamFromSlope`, `FoamFullSlope`).
- Rideaux : traînées qui tombent avec l'eau (décalées du temps de chute, jamais étirées), douces
  contre la roche et l'eau.
- Aucune texture : rien à licencier.
- Réglages (instances) : `RiverWaterColor`, `LakeWaterColor` (0,018 ; 0,060 ; 0,052),
  `GlacierWaterColor` (0,012 ; 0,150 ; 0,160), `SaltWaterColor` et leurs `…Absorption`,
  `FoamColor`, `CurtainColor`, `GlacierFromM`, `GlacierFullM`, `RippleStrength`, `WaterAlpha`,
  `FlowSpeed`, `FoamFromSlope`, `FoamFullSlope`, `SkyReflection`, `ShoreFadeM`, `FoamDepthM`.
  Réglés sur le matériau de l'eau des tuiles (ou son instance `WaterMaterial`), ils valent aussi
  pour l'horizon.

**Pour comparer** : la commande `Terrain.Water 0|1` (ou `SetWaterShown`) cache ou montre l'eau des
tuiles et des chutes et les lacs et fleuves de l'horizon ; `bWater` faux sur l'acteur : comme avant.

## Vues pour vérifier (`planet.bin` chargé, seed 20261019)

Coordonnées du core (x, z en m) ; en posant `CoreOriginX/Z` sur le point, l'œil est en (0, 0) dans
Unreal, à 100 × (altitude de l'œil − sol du point) cm. Cap en degrés dans le sens des aiguilles
d'une montre depuis le nord (−z du core, −Y d'Unreal) : le yaw d'Unreal vaut cap − 90. Chaque
point est vérifié avec `terrain_probe` (eau, niveau, sol ; avant l'agent 37 :
`terrain_probe_water`) ; les vues de la chute viennent de `unreal_water_probe` (œil dans l'air,
vue dégagée au-dessus du sol du core).

| Vue | Point (x ; z) | Sol | Eau | Œil (altitude) | Cap | Inclinaison | Ce qu'on doit voir |
|---|---|---|---|---|---|---|---|
| Fleuve de plaine | −241 631 ; −86 277 | −2,84 m | fleuve à 3,28 m | 5,28 m (2 m au-dessus de l'eau) | 0,5° | −2° | au milieu du fleuve (rives à 150 et 160 m), vers l'aval ; eau à 3,28 m à 50, 100, 200 et 300 m devant ; rides qui dérivent vers l'aval |
| Fleuve, 150 m | −241 631 ; −86 277 | −2,84 m | fleuve à 3,28 m | 153,28 m | 0,5° | −12° | le fleuve vers l'aval jusqu'à l'horizon : pas de bande en travers à la jonction tuiles / horizon |
| Fleuve, 600 m | −241 631 ; −86 277 | −2,84 m | fleuve à 3,28 m | 603,28 m | 0,5° | −90° | le fleuve de dessus : rides sans rayures, pas d'amas de points du soleil sous la caméra ; les marches de la rive sont celles du sol (limites) |
| Lac de montagne, 500 m | 7 369 228 ; −7 088 911 | 1 708,47 m | lac à 1 725,26 m | 1 745,26 m (20 m) | 157,5° | −2° | le lac jusqu'à sa rive (7 369 420 ; −7 088 449) à 500 m : bleu-vert, pas blanc |
| Lac de montagne, 5 km | 7 367 506 ; −7 093 068 | 1 712,03 m | lac à 1 725,26 m | 1 755,26 m (30 m) | 157,5° | −1° | la même rive à 5 km : l'eau des tuiles qui se fond dans celle de l'horizon vers 500 à 850 m, sans bande claire |
| Chute, de face | −6 185 080 ; 155 902 | 178,82 m | bassin à 180,03 m | 182,03 m (2 m) | 226,9° | +21,5° | la face de 73,51 m à 95 m : rideau du pied (180,03 m) à la lèvre (253,54 m), écume sur le bassin |
| Chute, de côté | −6 185 124 ; 155 984 | 259,90 m | sec | 266,78 m (6,9 m) | 316,9° | −59° | la même face de profil à 58 m : le rideau courbe, décollé de la paroi |
| Chute, de dessus | −6 185 144 ; 155 962 | 212,57 m | dans la face (213,78 m) | 373,54 m (120 m au-dessus de la lèvre) | 46,9° (vers l'aval) | −90° | la lèvre, le rideau, son pied et l'écume (pleine, bord fondu, dans le bassin), sans triangles blancs autour |
| Côte | −6 184 762 ; 155 681 | 9,6 m | — | 11,3 m | 50° | −2° | la mer de l'horizon, le fleuve qui la rejoint |

La chute : lèvre de la face de 73,51 m en (−6 185 149,5 ; 155 966,8), pied en (−6 185 139,9 ;
155 957,9), course 13,1 m, pente moyenne 5,6 (rideau courbe), 40 m de large ; la face de 80,89 m
au-dessus : lèvre (−6 185 378,1 ; 156 185,8), bassin à 463,48 m. Ses vues :
`unreal_water_probe --planet <fichier> -6185378.1 156185.8`.

## Coût GPU (à mesurer dans Unreal)

Comme pour le matériau temporaire : même résolution (PIE 2042 × 1752 ou 1440p), même session de
l'éditeur, `Terrain.Water 0` puis `Terrain.Water 1`, A B A B, temps GPU moyen par frame (profil
CSV ou `stat unit`), sur quatre vues : le fleuve de plaine, le lac de montagne à 500 m, la côte, la
chute. Le surcoût attendu vient des pixels d'eau des tuiles (translucides, 3 octaves de bruit,
deux phases sur les fleuves) et des pixels de l'horizon sous l'eau ; rien sur la terre sèche.
Mesuré sur `9db1286` (8 sinusoïdes) : fleuve +0,21 ms, lac +0,10, chute +0,42, côte +0,47 ; sur
`31389d2` (3 octaves de bruit, fondu au bord des tuiles) : fleuve +0,65 ms, lac +0,33, chute
+0,26, côte +0,42.

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
- les pixels coupés de l'horizon sous les tuiles (masque) et le partage des pixels de la bande
  (tramage complémentaire) : Single Layer Water voit la scène derrière l'eau et n'a pas d'opacité
  par pixel ; il faudra un masque de pixels sur l'eau des tuiles, ou un autre raccord, et vérifier
  que l'horizon n'apparaît pas sous l'eau des tuiles ;
- le coût du passage Single Layer Water (à mesurer sur les mêmes vues que ci-dessous).

## Limites

**À reprendre en phase 14 (belle eau)**, vus dans Unreal sur `351aa38` (validé) :
- **Bord droit de l'écume au pied du rideau.** Le disque d'écume est coupé net là où il passe sous
  le rideau ou contre la face : il ne lit que la profondeur derrière lui, pas la forme du rideau.
- **Traînée du reflet du soleil en taches carrées**, en regardant vers le soleil à 3 m de l'eau.
  Le reflet GGX suit les mailles du bruit de valeur des rides (réseaux tournés, mais des cellules
  carrées) : de près et à contre-jour, elles se lisent.
- **Petit amas de points brillants vu de dessus à 600 m**, sous la caméra. Le reflet du soleil
  garde un peu de scintillement malgré l'antialiasing spéculaire analytique des rides.
- **Bord haut du rideau en escalier vu de côté.** Le rideau part de l'eau de la lèvre, mais la
  surface des tuiles s'arrête à la dernière maille non coupée (0,7) avant la face, effacée sur
  une maille : vu de profil, ce bord suit les mailles (1 à 8 m selon la LOD).
- **Rive en escalier** (le sol, le matériau temporaire), détaillée plus bas.

**Autres limites :**
- Pas de rideau sur l'horizon : une chute vue de plus de 850 m n'a que ses marches d'eau.
- Les fleuves étroits ne sont que des points sur les niveaux grossiers de l'horizon (un
  échantillon tous les 16, 32 m…).
- L'horizon CPU (`HorizonRenderer` = CPU, pour comparer) ne dessine pas les lacs et fleuves.
- L'eau ne se voit pas de dessous ; les lacs des grottes (`TileData::water`) ne sont pas dessinés.
- L'eau est statique : les modifications des joueurs ne changent pas son niveau (un trou au bord
  d'un lac reste sec, un bloc posé dans un lac est sous l'eau).
- À l'embouchure, le fleuve translucide rejoint la mer opaque de l'horizon : changement d'aspect.
- Une maille coupée sans chute du core (un échelon de plus de 0,7 entre un fleuve et un lac, par
  exemple) laisse un trou d'eau effacé sur ses bords, sans rideau.
- Le partage des pixels au bord des tuiles est un tramage : il peut scintiller sans antialiasing
  temporel (TSR ou TAA). Avec TSR, 0,02 % des pixels changent d'une image à l'autre (mesuré sur
  `31389d2`).
- La couleur de l'eau profonde est éclairée par Unreal : en translucide (Surface ForwardShading)
  sur les tuiles, en opaque (Lumen) sur l'horizon. Les deux ne diffèrent que sous un ciel occulté
  (une gorge) : un écart de teinte sans bande y viendrait de là.
- **Rive en escalier du fleuve de plaine, vue de haut** (marches de plusieurs mètres le long de la
  bande claire) : c'est le sol, pas l'eau. À corriger avec le matériau du terrain (phase 14).
  - Le core passe du sec au mouillé là où le sol croise le niveau. Coupe tous les 2 m à
    z = −86 277 : la plaine à 3,95 m, 3,84 m au sec, 3,27 m sous 1 cm de fleuve, l'eau à
    3,28 m. L'eau des tuiles ne s'arrête jamais sur un bord de maille au-dessus d'un sol plus bas :
    son bord est l'intersection d'un plan plat avec le maillage du sol.
  - Le pied de la rive (0,6 m sur 2 m) est plus étroit qu'une maille à partir de la LOD 2 (4 m),
    en LOD 3 (8 m) et sur l'horizon (8 m). Il tombe sur la dernière colonne mouillée, et une rive
    en biais devient un escalier d'une maille, avec ou sans eau.
  - La bande claire est la classification du sol : galets de rive (`Pebbles`, à moins de
    `MaterialParams::riverBankWidth` = 25 m d'un échantillon de fleuve) et lit (`Water`). Elle
    est classée sur une trame par une distance de chanfrein 8-connexe (`terrain_surface.cpp`,
    `riverDistances`), puis dessinée par le matériau temporaire (éboulis, terre). Ses texels font
    4 m en LOD 0 à 32 m en LOD 3, et 8 m sur le niveau 0 de l'horizon : ses bords ont des marches
    et des octogones au pas de la trame.
- Les rides des tuiles ne suivent pas le recentrage d'origine d'Unreal (`WaveOrigin`) : elles
  peuvent sauter à un recentrage, rare.
- Sur l'horizon, les crêtes relevées (`HorizonCrestRatio`) peuvent poser un sol au-dessus de son
  lac : l'eau y est cachée.
- Le premier Play avec un fichier planète manquant ou périmé attend la construction (quelques
  minutes pour la planète entière).
