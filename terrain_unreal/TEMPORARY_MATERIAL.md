# Le matériau temporaire du terrain (phase 14, provisoire)

> **Provisoire** : un matériau lisible en attendant les vrais matériaux de la phase 14, qui le
> remplaceront. Tout ce qui le concerne tient dans `Scripts/temporary_terrain_look.py` (les
> règles, les couches), `Scripts/temporary_terrain_hlsl.py` (le HLSL),
> `Scripts/create_temporary_terrain_material.py` (les tuiles) et la
> partie « terre » de `Scripts/create_horizon_gpu_material.py` (l'horizon). Le C++ ne connaît que
> le paramètre de texture `SurfaceTexels` et la surface empaquetée des texels.

## Ce qu'il dessine

- **Les tuiles** : `/Game/TerrainEngine/M_TemporaryTerrain`, le matériau par défaut des tuiles
  quand le projet l'a. **L'horizon** : `M_TerrainHorizonGpu`, mêmes règles, sans le détail
  proche (seulement la couleur moyenne de chaque texture).
- **La surface** : le dernier flottant d'un texel de terre de l'horizon porte le matériau et le
  biome du core (`matériau + 8 biome`, `Bridge/terrain_surface.hpp`). Le nuanceur de sommets des
  tuiles lit les 4 texels du niveau 0 autour de chaque sommet (bilinéaire) ; l'horizon lit le
  texel de son sommet et le fond dans le niveau plus grossier, comme ses hauteurs. Les mêmes
  échantillons de près et de loin : pas de saut de couleur entre les tuiles et l'horizon.
- **Les couches du sol** selon le biome (table `BIOMES`) : herbe, herbe sèche, sol de forêt,
  herbe alpine, terre, sable, désert, éboulis ; puis selon le matériau du core : lit de rivière,
  sable ou désert, terre et boue, roche, neige, éboulis. Transitions douces : les poids des
  couches sont interpolés entre les échantillons (8 m sur le niveau 0).
- **La roche** de `RockStartDeg` (34°) à `RockFullDeg` (46°) de pente, par plaques au bord
  déchiqueté (pas un mélange uniforme), partout où le core met de la roche (`CoreRock` = 0,8 de
  celle-ci), et au-dessus de la limite des neiges sur toutes les faces trop raides pour la neige :
  rien n'y pousse. Un minéral gris froid, jamais du sable : ses textures (`T_RockyCliff` et, par
  plaques, `T_Cliff` assombrie) sont presque désaturées (`RockGrey` 0,85), leurs clairs et leurs
  sombres renforcés (`RockContrast` 1,4 : fissures et strates), albédo d'environ 0,15
  (`RockAlbedo`), teinte bleutée.
- **La neige** là où le core dit qu'il fait froid (biome ou matériau neige : sa température tient
  compte de l'altitude et de l'exposition), bord bruité, seulement sur les pentes douces : entière
  sous `SnowHoldDeg` (22°), plus du tout au-dessus de `SnowShedDeg` (33°), par plaques. Un blanc
  propre et un peu froid (`SnowWhiten` 0,92), avec un léger lustre. La roche perce sur les faces
  raides, comme sur le K2. Sous un soleil rasant, roche et neige prennent sa lumière orangée ; à
  midi, roche grise et neige blanche.
- **Pas de sable en montagne** : au-dessus de `SandMaxAltitudeM` (1 800 m) et sur les pentes de
  plus de 30°, le sable et le désert de n'importe quel biome deviennent des éboulis. Vérifié
  (`DebugView` 3) : aucune part de sable sur le flanc, la vue à 2 km et le sommet.
- **Le sol** sur les pentes moyennes (26° à 36°) : la terre sous l'herbe et la forêt, les
  éboulis sous l'alpage ; **les éboulis** aussi au pied des pentes raides (tuiles : la plus raide
  des 4 pentes autour du sommet, à partir de 35°).
- **Des plaques dans chaque biome, comme dans Minecraft** (bruit du monde déformé, bords doux
  mais lisibles, de quelques mètres à quelques dizaines de mètres, `PatchAmount`) : dans les
  prairies, de la terre nue, du gravier, de l'herbe sèche et une herbe plus sombre ; dans le
  désert, du gravier ; dans la roche, des éboulis et une seconde roche ; dans la neige, des
  affleurements rocheux.
- **Les textures**, alignées sur le monde : projection du dessus pour le sol, triplanaire pour la
  roche, une répétition tous les 4 ou 8 m (qui divise 4 096 m, la période des coordonnées
  périodiques du monde, comme les vagues de la mer). **Sans répétition visible** : chaque cellule
  de 0,7 répétition prend son propre décalage et son propre quart de tour, fondus sur le dernier
  quart de la cellule (texture bombing, déterministe, selon la seule position). Entre
  `DetailNearM` (150 m) et `DetailFarM` (400 m), elles se fondent dans la couleur moyenne de
  chaque texture (son dernier mip), la seule que l'horizon dessine : pas de couture.
- **Variation à grande échelle** du ton et de la teinte (cellules de 32 à 512 m,
  `MacroVariation`) : une prairie n'est pas un vert uniforme. Puis saturation (`Saturation` 1,2)
  et chaleur (`Warmth` 1,03 ; 1 ; 0,95) pour le sol seulement : la roche reste grise, la neige
  blanche.
- **Les normales des tuiles grossières** : au-delà des tuiles LOD 0, la normale penche vers la
  normale de détail du niveau 0 (2 m, ci-dessous) ou, tant qu'elle n'est pas prête, vers la
  normale lisse de ses échantillons (8 m), de `SmoothNearM` (60 m) à `SmoothFarM` (220 m),
  jusqu'à `SmoothNormal` (0,8) ; pas sur les falaises, surplombs et grottes, où les deux normales
  ne s'accordent pas.
- **Les normales de détail de l'horizon** (`horizon_detail_normals.py`) : le core calcule, pour
  chaque niveau, les pentes du sol `HorizonNormalRatio` (4) fois plus fines que sa grille (2 m sur
  le niveau 0, 16 m sur le niveau 3 à 4-8 km, 32 m sur le niveau 4 à 8-16 km :
  `HorizonNormalSampler`, filtre de Sobel sur les hauteurs à un pas, au bit près). Une seconde
  texture par niveau (demi-flottants, 4 Mo) les porte ; le matériau de l'horizon les lit par pixel
  (et celles du niveau plus grossier dans la bande de géomorphing) : crêtes, faces, ravines et
  murs restent lisibles à 2-16 km, sous un soleil rasant comme à midi. Elles arrivent après les
  échantillons (`HorizonNormalJobs` tâches de basse priorité, environ 10 s pour un horizon neuf,
  quelques millisecondes par pas de marche) ; en attendant, la normale de la grille. Le détail
  s'efface là où un pixel couvre plus d'un texel (pas de scintillement au loin ni en vue
  rasante). `DetailNormals` 0 sur les instances (ou `bHorizonDetailNormals` faux) rend les
  normales d'avant, pour comparer. Les pentes de détail suivent le relief du core, pas les
  modifications des joueurs (leur résumé ne change que les hauteurs).
- **Sans l'horizon GPU** (`HasSurface` 0) : les couches suivent la seule altitude.

## Le pack Stylescape HD (licence Fab : jamais dans le dépôt git)

Les textures viennent de `/Game/StylescapeHD/Landscape/Textures` du projet de jeu : `T_Grass`,
`T_DriedGrass`, `T_ForestGround` et `T_ForestGround_Alpine`, `T_DirtDry`, `T_Trampled_Sand`,
`T_Sand_Rocky`, `T_RockyGround`, `T_Snowy_Rocky_Dirt`, `T_RockyCliff` (albédo `_D`, normale `_N`),
et de `/Game/StylescapeHD/Foliage/Textures/Cliff` : `T_Cliff` (la seconde roche).

1. Décompresse le zip du pack dans un dossier à lui (par exemple `C:\Projets\StylescapeHD_56`) ;
   ne lance aucun script du pack.
2. Copie `Content/StylescapeHD/{Landscape, Materials, Foliage, Misc}` dans `Content/StylescapeHD`
   du projet de jeu (même chemin `/Game/StylescapeHD`), **sans** `M_Landscape_Nwiro` et
   `MI_Landscape_Nwiro` (ils demandent le plugin Nwiro, absent), ni `Levels` ni `Audio`. Les
   assets de 5.6 s'ouvrent dans 5.8 ; les textures sont recompressées au premier chargement.
3. Lance `create_temporary_terrain_material.py` puis `create_horizon_gpu_material.py`.

Sans le pack, chaque couche est une couleur unie (la moyenne de sa texture), avec un
avertissement dans le log : les scripts marchent dans tout projet. Les deux scripts reconstruisent
les matériaux en place ; `delete_all_material_expressions` n'efface qu'environ la moitié des nœuds
par appel (UE 5.8) et les paramètres restés en double gardaient leurs anciennes valeurs : les
scripts l'appellent jusqu'à ce qu'il n'en reste aucun (`clear_material`).

## Réglages

Paramètres des matériaux, modifiables sur des instances : la teinte de chaque couche
(`GrassTint`, `RockTint`, …), `RockStartDeg`, `RockFullDeg`, `SnowHoldDeg`, `SnowShedDeg`,
`CoreRock`, `DetailNearM`, `DetailFarM`, `Saturation`, `Warmth`, `NormalStrength`, `SnowWhiten`,
`MacroVariation`, `PatchAmount`, `RockGrey`, `RockAlbedo`, `RockContrast`, `SandMaxAltitudeM` ;
`DebugView` 1 montre les couches (vert herbe, jaune herbe sèche, vert foncé forêt, turquoise
alpin, brun terre, beige sable, orange désert, gris éboulis, rouge roche, blanc neige), 2 la roche
(rouge), la neige (vert) et le pied des pentes (bleu), 3 la part de sable et de désert (rouge).

## Coût

Mesuré dans l'éditeur (PIE 2042 × 1752, 3,6 Mpx, autant que le 1440p ; RTX 4090), mêmes vues
fixes, sessions PIE alternées A B A B dans une même session de l'éditeur : A avec les couleurs
d'altitude et le matériau d'horizon de `main`, B avec le matériau temporaire ; temps GPU moyen par
frame (profil CSV), écart B − A. Version finale (roche grise, plaques, textures sans répétition),
base A de 5,2 à 5,5 ms :

| Vue | Écart |
|---|---|
| pied de la chaîne, 1,7 m, soleil bas | +0,29 ms |
| pied de la chaîne, 300 m | +0,29 ms |
| flanc à 5 km, 1,7 m (roche, neige et éboulis au premier plan : le pire cas) | +0,80 ms |
| plaine, 1,7 m | +0,54 ms |
| plaine, 300 m | +0,31 ms |

Au plus +0,8 ms, sous le budget de +1,0 ms (la version sans plaques ni texture bombing coûtait de
0 à +0,9 ms selon les sessions). Le surcoût vient des pixels proches : jusqu'à 4 lectures par
texture pour casser la répétition, la roche triplanaire et ses deux variantes. Le matériau
n'échantillonne que ce qui se voit (pas de sol sous la roche ou la neige pleine, deux plans au plus
pour la roche, une seule carte de normales pour le sol, la seconde roche seulement dans ses
plaques). L'horizon ne lit que la couleur moyenne de chaque texture.

**Normales de détail** (relief 11R bis étape 5 bis, mêmes conditions, PIE 2027 × 1724 à 103 %,
`DetailNormals` 0 puis 1 dans la même session, A B A B) : +0,01 à +0,02 ms sur les six vues (K2,
mur, pied de la chaîne, à 1,7 m et 300 m, base 4,4 à 4,6 ms). CPU (`horizon_cost_test`, autour
du K2) : 15,9 s sur un thread pour tout l'horizon (1,5 µs par texel ; les échantillons seuls :
1,5 s), 10,6 s sur 2 tâches ; dans le jeu, le détail est complet environ 10 s après la première
apparition de l'horizon, qu'il ne retarde pas ; envoi au GPU 0,01 ms par tick. Mémoire : 42 Mo
de textures sur le GPU, rien de gardé sur le CPU. Au ratio 8 : 4 fois tout cela (169 Mo, environ
35 s), pour un gain visible mais modeste. Les ombres portées de l'horizon (`bHorizonShadows`)
coûtent +0,45 ms à la vue du K2.

## Trois défauts de rendu, au passage

- **Fissure sombre sur la plage vue de 300 m** : l'horizon est posé `HorizonDropCm` (1 m) sous son
  sol là où les tuiles le recouvrent ; au bord extérieur des tuiles restait une marche de 1 m,
  dont l'ombre (6 m sous un soleil à 10°) dessinait un trait sombre sur le sol plat. La descente
  s'efface maintenant dans la bande des tuiles au-dessus du niveau 0 (pleine dans sa moitié
  intérieure, nulle au bord des tuiles ; `dropFade`, `dropShare` de `Bridge/terrain_surface.hpp`,
  paramètres `PitchCm`, `DropCoveredCm`, `DropFadeCm` du matériau de l'horizon). Vérifié : le
  trait revient sans le fondu, et disparaît aussi quand les tuiles ne projettent pas d'ombre.
- **Dents de scie et facettes sous un soleil bas** : pas les ombres (identiques sans aucune
  shadow map), mais les normales par sommet des tuiles grossières (Marching Cubes en mailles de 2
  à 8 m) le long de la limite ombre-lumière. Corrigé par les normales lissées ci-dessus.
- **Ligne droite au-delà de laquelle les ombres s'arrêtent (vue de 300 m au pied de la
  chaîne)** : pas un défaut de rendu. Les taches sombres ne sont pas des ombres portées (la vue
  est la même sans aucune shadow map) mais les pentes tournées à l'opposé d'un soleil rasant ; la
  ligne est la crête d'une longue arête droite du relief 3b, dans l'axe de la chaîne : un versant
  face au soleil, l'autre à contre-jour. Elle est dans les normales de l'horizon seul et dans un
  ombrage calculé hors d'Unreal depuis les hauteurs du core. À signaler au relief si cette arête
  rectiligne n'est pas voulue. Par ailleurs, l'horizon ne projette pas d'ombres
  (`bHorizonShadows`) : au-delà des tuiles, aucune ombre portée.

## Limites

- Provisoire : pas de mélange par hauteur des textures, pas de couches par biome au-delà de la
  table, pas de RVT ; une texture par couche (deux pour la roche).
- Les plaques et le texture bombing n'existent que sur les tuiles ; l'horizon garde la couleur
  moyenne des couches et sa variation à grande échelle.
- Les biomes changent tous les 8 m sur le niveau 0 : des transitions douces mais régulières.
- La neige vient du biome du core : une neige plus basse ou plus haute demande de régler le
  climat du core, pas le matériau.
- La normale des tuiles grossières suit le relief à 2 m (les normales de détail du niveau 0) ;
  celle de l'horizon à un quart de la maille de chaque niveau. Les silhouettes restent celles des
  grilles (8 m × 2^niveau) et l'horizon ne projette pas d'ombre (`bHorizonShadows`).
