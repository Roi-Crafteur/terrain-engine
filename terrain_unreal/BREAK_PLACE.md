# Casser et poser en jeu (agent 28, phase 15)

Le joueur vise le terrain et retire de la matière (clic gauche) ou en ajoute (clic droit), avec
un bloc de 1 m ou une sphère d'environ 3 ou 5 m de diamètre (19 ou 57 blocs). Un fantôme montre ce qui sera retiré ou
posé. Les modifications passent par le fil de modifications de la session : les tuiles sont
refaites avec leur collision et sans les plantes dont le sol est parti, et l'horizon lointain
suit. Elles peuvent être sauvegardées par seed.

## Qui décide quoi

| Sujet | `terrain_core` et le Bridge (données, déterministe, testé par ctest) | Unreal (affiche, fait jouer) |
|---|---|---|
| Visée | `Bridge/block_targeting` : point touché et normale → bloc cassé (le centre du bloc à 0,5 m sous la surface, le long de la normale), bloc posé (son voisin du côté de la face principale de la normale), sphère cassée (centrée sur le point), sphère posée (son centre à un demi-rayon hors de la surface) ; portée ; tout en entiers dans la modification | trace depuis la caméra sur les tuiles (`ECC_Visibility`, collision complexe), distance mesurée depuis l'œil du pion |
| Outils | bloc 1 m, sphère 3 m, sphère 5 m (rayons 26 et 39 seizièmes de mètre : 19 et 57 blocs, aucun bloc seul au bout d'un axe, agent 41 ; avant, 16 et 32 seizièmes donnaient une croix de 7 blocs et un cube de 27 avec 6 blocs seuls, dessinés en pyramides) | touches 1, 2, 3 (aussi `&`, `é`, `"` en AZERTY), écrit à l'écran ; pas de molette |
| Règles | `Bridge/build_rules` : refus si la carte de dev est ouverte, pendant un chargement, avec un autre pion que celui du composant, sans le focus ; 4 modifications par seconde au plus bouton tenu ; refus d'un volume posé à moins de 5 cm de la capsule | lit la carte, le chargement, le contrôleur, le focus du viewport, la capsule |
| Application | `applyVolumeEdit` sur le fil de modifications (existant), plantes retirées, coutures fermées, horizon | maillage et collision des tuiles refaites, envoyés dans le budget de 6 ms de la #58 |
| Fantôme | boîte ou sphère en coordonnées du core puis d'Unreal | `/Engine/BasicShapes/Cube` ou `Sphere` avec le matériau filaire du moteur (aucun `.uasset`) |
| Sauvegarde | `Bridge/edit_save` : dossier v2 par seed, fil d'écriture, `.tmp` puis renommage, `.bak` ; refus d'un monde illisible, d'une autre seed, d'une autre taille | `Building.bSaveEdits`, `Terrain.EditSave 0\|1` |
| Mesure | `Bridge/edit_latency` : clic → fait → tuile envoyée, médiane, p95, max, images | `Terrain.EditStats` (outils de debug seulement) |
| Creux lisses (agent 41) | positions au bloc près (règle des cubes) ; normales des sommets modifiés tournées vers le gradient de la part lissée des 27 blocs voisins (`tile_edits.hpp` : `lightEdits`) | rien de neuf |
| Blocs vus de loin (agent 41) | LOD 1 à 3 : couverture de la cellule et relais en boîte (`edit_coarse.hpp`), coutures, invalidation, niveau 0 de l'horizon | rien de neuf (maillages déjà envoyés) |
| Lumière des modifs (agent 41) | poids « modifié » par sommet (`TileData::editWeights`), dans le bleu de la couleur de sommet des tuiles modifiées | émissif `couleur de base × EditLightFloor` dans `M_TemporaryTerrain`, **éteint par défaut** |

## Les réglages

Sur `Terrain World`, catégorie `Terrain|Edits`, structure `Building` :

- `bAddToPlayerPawn` (oui) : le pion du joueur reçoit un `Terrain Build` s'il n'en a pas (jamais
  le pion volant des outils) ;
- `bSaveEdits` (**non**) : les modifications de la seed sont lues au début du Play dans
  `Saved/<SaveFolder>/seed_<seed>/` et écrites là toutes les `SaveIntervalSeconds` (10 s) s'il y a
  eu des modifications, et à la fin du Play ; `SaveFolder` vaut `TerrainEngine/Edits`. Jamais le
  fichier planète. La dernière sauvegarde attend d'abord les modifications encore sur le fil (un
  clic juste avant la fin du Play est gardé), et le world.meta d'un monde déjà là est gardé (nom,
  création, point d'apparition, temps de jeu, version du générateur, champs d'une version plus
  récente) : seule sa date de sauvegarde change (agent 41 ; avant, la première sauvegarde le
  réécrivait depuis les valeurs par défaut). Le log de lecture donne la date de création du monde.
- `bEditLightFloor` (**non**) et `EditLightFloor` (0,15) : un plancher de lumière sur les surfaces
  faites par les modifications (agent 41). Elles émettent `EditLightFloor` fois leur couleur de
  base, en proportion de leur poids « modifié » (1 au fond et sur les parois d'un trou, 0 sur le
  sol qu'elles ont laissé). Contre les creux presque noirs ; à n'allumer qu'après avoir vérifié la
  lumière du niveau (« La lumière des creux » plus bas). Il faut relancer
  `create_temporary_terrain_material.py` une fois pour avoir ses nœuds.

Sur le composant `Terrain Build` (catégorie `Terrain|Build`) : `ReachMeters` (6 m, depuis l'œil du
pion, pas depuis la caméra de troisième personne), `Tool`, `EditsPerSecond` (4), `bShowGhost`,
`GhostMaterial` (vide : le matériau filaire du moteur), et l'entrée : `MappingContext` et ses cinq
actions de boutons (`BreakAction`, `PlaceAction`, `BlockToolAction`, `SmallSphereToolAction`,
`LargeSphereToolAction`). Vides : créés au début du Play avec les touches par défaut. Les outils ont
chacun deux touches, dans un seul tableau du code (`TerrainBuildComponent.cpp`, `FToolKeys`) : la
rangée des chiffres en QWERTY (`One`, `Two`, `Three`) et la même rangée en AZERTY, qu'Unreal nomme
par le caractère tapé sans Maj (`Ampersand`, `E_AccentAigu`, `Quote`). **Pas de molette** : la
caméra de GASP zoome avec, et un contexte ne peut lui prendre que la touche exacte qu'il utilise
(sans doute `MouseWheelAxis`, un axe que nos actions de boutons ne lisent pas). Pour changer les
touches, donner un contexte **et** ses actions. Le contexte est ajouté avec la priorité
`MappingPriority` (1, au-dessus du personnage) ; les actions sont liées (`BindAction`) sur un
`UEnhancedInputComponent` du composant, poussé sur la pile du contrôleur. Le log écrit seulement
les touches du composant : `Terrain Build: input of <pion> at priority 1: break: LeftMouseButton,
…`. Les conflits avec les touches du personnage se voient en jeu.

Le nœud Blueprint `Build (Break | Place)` fait la même chose qu'un clic (vrai si la modification
est en file ; sinon `Get Last Refusal` dit pourquoi).

## Les commandes

| Commande | Effet | Build |
|---|---|---|
| `Terrain.EditSave 1` | sauvegarde à partir de maintenant : la sauvegarde de la seed est **remplacée** par les modifications de ce Play (régions écrites à nouveau, régions sans modification supprimées) | tous |
| `Terrain.EditSave 0` | une dernière sauvegarde, puis plus rien | tous |
| `Terrain.EditStats` | les dernières modifications du joueur : clic → fait, clic → tuile affichée (médiane, p95, max), images, et les sauvegardes | outils de debug seulement |
| `Terrain.EditLight 1` / `0` | le plancher de lumière des surfaces modifiées allumé / éteint (éteint par défaut) ; sans argument, son état | tous |
| `Terrain.EditLightFloor 0.15` | la part de sa couleur de base qu'une surface modifiée émet (0 à 1) | tous |
| `Terrain.EditAim <dig\|place> <block\|sphere> [rayon] [at x y z]` | un clic du joueur pour les scripts, par le code du clic (`BuildRequested` : règles, visée depuis la caméra, portée de 6 m depuis l'œil, capsule, 4 par seconde, file, donc sauvegarde). `sphere` sans rayon : la sphère de 3 m (1,625 m de rayon) ; un rayon de 1/16 à 32 m. `at x y z` : un point du core par lequel passe la trace (sinon le centre de l'écran) ; refus si elle touche à plus de 0,25 m de ce point. Une ligne dans le log à chaque appel (voir « Les scènes ») | outils de debug seulement |
| `Terrain.EditRead 1` / `0` | dans l'éditeur, entre deux Play : le prochain Play relit et sauvegarde les modifications de la seed comme avec `Building.bSaveEdits`, sans toucher l'acteur ni la carte ; sans argument, l'état | outils de debug seulement |

Chaque modification du joueur affichée écrit aussi dans le log (outils de debug seulement) :
`Terrain World: edit shown 12.3 ms after its click (1.1 ms to be done), 1 frames later`.

## L'eau

Le core ne recalcule pas l'eau, et cette tâche ne touche pas à l'hydrologie. Règle : **l'eau reste
où le core la met** ; on peut casser et poser partout.

- Dans un lac, une rivière ou sous la mer : casser creuse le fond sous la nappe, qui reste plate et
  à son niveau ; poser fait sortir de la roche à travers la surface.
- Au bord de l'eau : un trou est **sec**, même sous le niveau de l'eau ; l'eau ne s'y écoule pas.
- Près d'une chute : le rideau d'eau reste à sa place, même si on casse sa lèvre.

L'eau physique (écoulement, remplissage) reste dans « Plus tard » de la ROADMAP.

## Mesures (Linux, GCC, sans Unreal)

`unreal_edit_latency_test` : une session réelle, image après image comme le fil de jeu (reprendre
les modifications faites, mettre à jour le streamer, envoyer), sans dessin :

| Outil | clic → fait | clic → tuile envoyée (médiane) | max | images |
|---|---|---|---|---|
| bloc 1 m | 0,2 ms | 2,2 ms | 6,6 ms | 6 au plus (à 1 ms l'image) |
| sphère 3 m | 1,1 ms | 3,2 ms | 5,5 ms | 5 au plus |
| sphère 5 m | 1,1 ms | 3,3 ms | 3,3 ms | 3 au plus |

Remesuré par l'agent 41 (avant : 2,2 à 2,4 ms de médiane, 2 images au plus). Les tuiles de LOD 1 à
3 lisent maintenant les modifications (« Blocs vus de loin ») : une modification refait aussi
celles d'un chunk autour, d'où quelques millisecondes de plus pour la dernière tuile envoyée. À
16,7 ms l'image, la tuile du clic reste prête dans l'image du clic ou la suivante.

`edit_lod_cost_test`, une tuile sur 1, 10 et 100 blocs cassés contre la même tuile intacte : ×1,03
à ×1,12 au LOD 0, ×1,17 à ×1,23 au LOD 1, ×1,06 à ×1,13 au LOD 2, ×1,08 à ×1,16 au LOD 3 (limite
×1,3). Sans modification à portée : aucun bloc lu, ×1,00 au bruit près, et les tuiles au bit près.

À 60 images par seconde (16,7 ms l'image), la tuile refaite est donc prête dans l'image du clic ou
la suivante. Le temps dans Unreal (envoi du maillage, cuisson de la collision, dessin) est à lire
avec `Terrain.EditStats`.

## Limites

- Le fantôme montre la forme demandée (un cube aligné sur la grille, une sphère), pas la surface
  lisse qui sera refaite ; un bloc retiré sur une pente creuse moins qu'un cube entier.
- Le fantôme est filaire et seulement visible quand la cible est valide ; un placement refusé
  (capsule) le dit à l'écran au clic.
- Un pion qui apparaît au début du Play dans de la roche posée et sauvegardée n'est remonté que
  jusqu'au sol de la heightmap (`bSnapPawnToGround`), pas au-dessus de la roche posée.
- Le matériau filaire du moteur n'est pas garanti dans un jeu empaqueté : donner `GhostMaterial`
  pour un build final.
- Les messages à l'écran (`AddOnScreenDebugMessage`) n'existent pas en Shipping ; le log reste.
- **Silhouette à facettes.** Les positions restent au bloc près (collision, plantes et visée en
  dépendent) : le bord d'un creux garde des facettes de 1 m ; seules ses normales sont lissées.
- **D'autres rayons de sphère** (`Terrain.Edit`, `Apply Volume Edit`) peuvent encore laisser un
  bloc seul au bout d'un axe, donc une pointe. Les outils du joueur n'en ont pas.
- **Les parois latérales des creux reculent de 3,1 cm** (la règle des parois, voir « Tunnels ») :
  un trou d'un bloc fait 1,06 m de large à hauteur de son centre ; les sols et les plafonds ne
  bougent pas, les blocs posés non plus.
- **Un trou d'un bloc vu de loin** fait au LOD 1 un creux de 4 m de large au lieu de 2 (la maille
  de 2 m) ; à 60 m c'est un trait de 2 à 3 pixels de haut au lieu de 1 à 2.
- **Au LOD 3** (au-delà d'environ 290 m), rien de plus petit que la moitié d'une cellule (4 m) ne
  se voit ; un mur de 3 m s'efface en deux pas (la moitié de sa hauteur au LOD 2).
- **Horizon sur une modification géante.** Le niveau 0 de l'horizon lit les modifications par les
  cellules de 8 m : une montagne rasée (1 km × 1 km × 500 m) relit ses colonnes en 1,1 s au lieu
  de 0,17 s, et une grosse explosion se voit à 0,6 km 12,7 ms après au lieu de 5,5 ms. Hors du
  thread de jeu ; sans effet sans modification.
- **Le plancher de lumière** n'est pas lié à l'heure du jour : la nuit, une surface modifiée émet
  aussi sa part de couleur. Il est éteint par défaut.
- **Le matériau de secours** `M_TerrainAltitude` (utilisé seulement sans `M_TemporaryTerrain`)
  dessine la couleur de sommet telle quelle : sur une tuile modifiée, le bleu y est le poids des
  modifications (le bleu de l'altitude perdu, les creux en bleu). Le mode de vue des couleurs de
  sommet montre aussi ce poids : utile pour voir quelles surfaces le plancher éclairera.

## Le monde modifié (agent 41)

### Les creux

Les pointes venaient des outils. Une sphère de 1 m ou 2 m de rayon centrée sur un bloc garde les
blocs à d² ≤ r² : une croix de 7 blocs, ou un cube de 27 avec 6 blocs seuls au bout des axes.
Marching Cubes dessine chaque bloc seul en pyramide (les pointes) et chaque arête en chanfrein
(les angles). Les outils ont maintenant les rayons 26/16 et 39/16 m : 19 et 57 blocs, chaque bloc
avec au moins deux voisins dans la sphère. Les normales du creux suivent la part lissée des 27
blocs voisins : sa lumière tourne sans marches.

`edit_hollow_test` (sol plat, pente de 20°, flanc d'une masse posée) : aucun sommet à plus de
0,26 m hors de la sphère ni 0,38 m en dedans ; les normales s'écartent de la direction du centre
de 3,4 à 8,9° en moyenne (10,9° au plus ; 7,6 à 16° en moyenne et jusqu'à 34° avant) ; la sphère
de 2 m de rayon d'avant échoue (0,50 m hors de la sphère, 6 blocs seuls) ; aucun bit de normale ne
change loin du creux.

### Tunnels

`edit_tunnel_test` : sous le sol, une salle à sol plat, et depuis sa paroi est un tunnel de 2 × 2,
1 × 3 ou 1 × 2 blocs, creusé bloc par bloc sur 6 blocs. Une capsule va du milieu de la salle au
dernier bloc et revient, tous les 5 cm :
- elle tient : aucun triangle plus près que son rayon ;
- le sol la porte comme le décide le `CharacterMovement` d'Unreal (`ComputeFloorDist`) : le contact
  de sa sphère basse s'il est marchable, sinon la surface que touche une ligne verticale sous son
  centre ;
- elle ne monte jamais de plus de 45 cm d'un pas à l'autre.

Une capsule tourne sur son axe sans changer de forme : tenir au fond, c'est pouvoir y faire
demi-tour. La capsule du jeu (`SandboxCharacter_CMC` de GASP, lue dans Unreal : rayon 30 cm,
demi-hauteur 86 cm) avec les réglages par défaut que le jeu garde (angle marchable 44,765°,
marche 45 cm), dans le tunnel 1 × 2 :

| | Marge (place − besoin) | Sol dans le tunnel | Sol au plus (l'embouchure) | Murs |
|---|---|---|---|---|
| avant | +3,1 cm (calcul : 2 − 2√2 × 0,30 = 1,1515 m pour 1,12 m) | 45,0° : pas marchable | 45,0° | ±0,500 m |
| après | +5,5 cm | 43,3° : marchable | 44,1° | ±0,531 m |

**La règle des parois (option a, au plus juste).** À côté d'un bloc retiré, le long de x ou de z,
un point du réseau prend la distance 0,5 (1 − 0,531) / 0,531 au lieu des 0,5 de son cube :
Marching Cubes met la paroi à 0,531 m du centre du bloc. Le V d'un tunnel d'un bloc de large
penche alors de atan(0,5 / 0,531) = 43,3°, soit 1,5° sous l'angle marchable. Le sol et le
plafond ne bougent pas ; les blocs posés gardent leurs cubes. Les trois chemins de la densité
(`ModificationStore`, `EditWindow`, `EditLattice`) donnent les mêmes bits. Revers : **toutes** les
parois latérales des creux reculent de 3,1 cm. N'écarter que celles des creux d'un bloc de large
demanderait de lire les blocs à 2 m, ou de baisser une densité au retrait d'un bloc. Si l'angle
marchable du jeu valait 46° ou plus (`Terrain.Pawn` l'écrit dans le log), on annulerait cet
écartement.

Les autres tunnels :
- **2 × 2** : toutes les capsules passent, sol plat, sans changement ;
- **1 × 3** : sol à 43,3° au lieu de 45° ; il devient marchable lui aussi ;
- **1 × 2 avec les anciennes capsules** : il manque encore 1,4 cm (34/88), 6,2 cm (35/90) et
  23,5 cm (42/96), contre 4,3, 9,1 et 26,9 cm avant.

La scène b de la vérification dans Unreal y est aussi : depuis le fond d'une sphère de 5 m, un
tunnel 1 × 2 de 6 blocs. La capsule du jeu y entre et en ressort (marge +5,5 cm, sol à 44,1° au
plus).

### Blocs vus de loin

Avant, un point des tuiles de LOD 1 à 3 (tous les 2, 4 ou 8 m) ne lisait que les blocs qui le
touchent : un bloc de 1 m disparaissait entre deux points, ou devenait une bosse d'une cellule
(jusqu'à 8,7 m au LOD 3). Maintenant chaque point répond de sa cellule (`edit_coarse.hpp`) :

- ce qui couvre la moitié d'une cellule ou plus est dessiné à sa taille (les murs d'un grand trou
  à une demi-cellule près à tous les LOD) ;
- en dessous, un relais : la boîte des blocs modifiés, à sa vraie taille, ramenée sur la colonne
  de sa cellule (1 m au plus au LOD 1), avec sa vraie profondeur ou hauteur quel que soit le rang
  de la maille où elle tombe. Un bloc cassé creuse le LOD 1 de 1,18 à 1,20 m, un bloc posé l'élève
  de 0,80 à 0,82 m, à toutes les places d'une cellule ; au LOD 2 la moitié de sa hauteur, au LOD 3
  rien ;
- les normales des faces du relais suivent sa boîte : un mur d'un bloc reste droit et en roche ;
- les coutures entre LOD restent fermées (au plus 2 µm), et une modification refait les tuiles
  grossières à un chunk autour.

`edit_lod_views_test` et l'outil `terrain_export_edit_lods <dossier>` (vues de 30 à 300 m, soleil
à 30° et à 6°) : au passage du LOD 0 au LOD 1 vu de 60 m, un mur d'un bloc (9 068 pixels) garde
94 à 96 % de son contour à 2 pixels près et sa luminance à 3,7/255 près ; un trou d'un bloc (90 pixels,
un trait de 1 à 2 pixels de haut) reste visible (285 pixels, voir les limites), sa luminance à
5/255 près. De 100 à 300 m, le mur garde 92 à 100 % de ses pixels au LOD 1, 27 à 35 % au LOD 2,
aucun au LOD 3 (contre les mêmes vues sur des tuiles de LOD 0). Ce rendu de contrôle ne remplace pas Unreal.

### La lumière des creux

Rien dans le dépôt ne règle Lumen ni la lumière du ciel. Les tuiles sont des
`ProceduralMeshComponent` avec les réglages par défaut : sans distance field ni surface cache,
Lumen les voit mal, et un creux ouvert au ciel peut rester presque noir (RGB ≈ 3). Avant tout
plancher, passer ces commandes une par une, au fond d'un creux puis sous un surplomb naturel, et
noter ce que chacune change (si une commande n'existe pas en 5.8, la console le dit : passer à la
suivante) :

1. `viewmode lightingonly`, puis `ShowFlag.DirectLighting 0` : la lumière seule, puis sans le
   soleil (ce qui reste est la lumière du ciel et de Lumen) ;
2. `r.Lumen.ScreenProbeGather.ScreenTraces 0` ;
3. `r.Lumen.ScreenProbeGather.ShortRangeAO 0` ;
4. `r.DynamicGlobalIlluminationMethod 0` : Lumen coupé ;
5. `r.Lumen.HardwareRayTracing 1`, puis `r.Lumen.HardwareRayTracing.LightingMode 2` ;
6. `Terrain.EditLight 1`, puis `Terrain.EditLight 0` : le plancher sur les seules modifications.

Revenir ensuite à `viewmode lit`, `ShowFlag.DirectLighting 1` et
`r.DynamicGlobalIlluminationMethod 1`. **Si les grottes et les surplombs naturels ont le même noir,
la vraie correction est dans la lumière du niveau, pas un plancher sur les seules modifications.**

**Les réglages à lire d'abord**, sans rien changer : leurs valeurs dans un projet Lumen par
défaut. En Python : `unreal.SystemLibrary.get_console_variable_int_value("r.ReflectionMethod")`.
Dans la console : le nom seul affiche la valeur.

| Réglage | Attendu | Ce qu'un autre dit |
|---|---|---|
| `r.DynamicGlobalIlluminationMethod` | 1 (Lumen) | 0 : pas de lumière indirecte, les creux ne reçoivent que le ciel direct ; 2 : SSGI |
| `r.ReflectionMethod` | 1 (Lumen) | 0 ou 2 : réflexions sans Lumen |
| `r.Lumen.DiffuseIndirect.Allow` | 1 | 0 : Lumen coupé pour la lumière indirecte |
| `r.Lumen.Reflections.Allow` | 1 | 0 : réflexions de Lumen coupées |
| `r.Lumen.HardwareRayTracing` | 0 (1 si le projet l'active et que la carte le permet) | 1 : Lumen voit les tuiles par le ray tracing matériel |
| `r.Lumen.HardwareRayTracing.LightingMode` | 0 | 2 : l'éclairage au point touché, le plus juste dans un creux |
| `r.Lumen.TraceMeshSDFs` | 1 | 0 : Lumen logiciel sans les distance fields des maillages |
| `r.GenerateMeshDistanceFields` | 1 (réglage du projet) | 0 : aucun distance field ; de toute façon, les tuiles (`ProceduralMeshComponent`) n'en ont pas |
| `r.Lumen.ScreenProbeGather.ScreenTraces` | 1 | 0 : plus de traces d'écran, le seul moyen du Lumen logiciel de voir les tuiles |
| `r.Lumen.ScreenProbeGather.ShortRangeAO` | 1 | 0 : sans l'occlusion de près qui assombrit les creux |
| `ShowFlag.DirectLighting` | 1 | 0 : sans le soleil |
| `Terrain.EditLight` (sans argument) | « light floor of the edits off (0.15 of the base color) » | on : le plancher allumé |

Sans distance field, le Lumen logiciel ne voit les tuiles que par les traces d'écran : un creux
dont le ciel n'est pas à l'écran reste noir. Le ray tracing matériel (`r.Lumen.HardwareRayTracing
1`, `LightingMode 2`) voit les tuiles.

`unreal_edit_light_test` : sans modification, aucune tuile n'a de poids (et reste au bit près) ;
au fond d'un trou le poids vaut 1 aux LOD 0 à 2, 0 sur le sol à 8 m et deux cellules ; la
conversion met le poids dans le bleu, garde le rouge, le vert et l'alpha. `unreal_edit_light_scripts_test`
vérifie sans Unreal les nœuds de l'émissif et qu'aucun autre matériau des tuiles ne lit le bleu
(eau, écume, chutes : aucune couleur de sommet ; les côtes de l'agent 33 : l'alpha et les UV 1 à 3).

## La vérification dans Unreal

Niveau `TerrainGasp`, `Terrain World` avec **`planet.bin` chargé** (le `PlanetFile` par défaut ;
seed 20261019, somme `26626564570b0d9e`). Les positions sont celles du core (x vers l'est, z vers
le sud), le cap en degrés depuis le nord (−Z) dans le sens horaire, l'inclinaison négative vers le
bas. Pour chaque lieu : `Terrain.Goto x z +1.8`, `Terrain.Look cap inclinaison`, puis
`Terrain.Pawn` (le personnage GASP posé sur le sol praticable le plus proche : casser et poser ne
marchent qu'avec lui, jamais avec le pion volant des outils).

| Lieu | x | z | cap | incl. | œil | sol (core) |
|---|---|---|---|---|---|---|
| plaine | −240 631 | −86 677 | 90 | −30 | +1,8 m | 6,18 m, pente 0,2°, sec, à 1 km du fleuve de `fleuve_plaine` |
| paroi (pied de la chute de 388 m) | −6 185 120 | 155 968 | 90 | 10 | +1,8 m | 180,16 m, pente 2,9°, sec ; la paroi (88°, 207,7 m à 4 m) commence 4 m à l'est et monte aussi au sud ; le bassin (eau à 180,03 m) 4 m au nord. Paroi du heightmap : roche 3D quand l'agent 20 sera mergé. **Plus une paroi** (sonde du 13 novembre, agent 41) : le sol y est à 245,6 m, sur une pente de 40° ; le tunnel de l'agent 41 se creuse dans la plaine (scène b) |
| bord de lac (`lac_montagne`) | 7 370 322 | −7 088 911 | 270 | −35 | +1,8 m | 1 725,4 m, sec, pente 3° ; l'eau (1 725,26 m, quelques cm de fond) commence 4 m à l'ouest |
| chute de 388 m | −6 185 124 | 155 984 | 227 | 0 | +1,8 m | 260,1 m, pente 35°, près du bassin de `chute388_bassin` |
| K2 | voir `Terrain.Goto k2_paroi` puis `Terrain.Pawn` | | | | | |

Sur chaque lieu :

1. Avant tout : `Terrain.EditSave 0` n'est pas nécessaire (coupé par défaut). Le log doit dire
   `Terrain World: breaking and placing given to <pion>` puis `Terrain Build: input of <pion> at
   priority 1: …`. Vérifier en jeu que le clic droit et 1/2/3 (ou `&`, `é`, `"` en AZERTY) ne
   déclenchent pas aussi une action de GASP, et me le renvoyer. La molette reste à GASP.
2. **Fantôme** : un cube filaire de 1 m sur le sol visé à moins de 6 m de l'œil ; rien au-delà.
   Touches 2 et 3 : une sphère de 3 m puis de 5 m ; `Tool: …` en haut de l'écran. Carte ouverte,
   pendant le noir d'une téléportation et avec le pion volant : pas de fantôme, et le log dit
   `Terrain Build: ghost hidden: …` puis `ghost allowed again`.
3. **Casser** (clic gauche, touche 1) : un creux d'environ 1 m dans l'image du clic ou la suivante.
   Tenir le bouton : 4 creux par seconde, pas plus. Puis `Terrain.EditStats` : « click -> tile
   shown » en millisecondes et en images (but : 0 ou 1 image).
   Tenir la sphère de 5 m sur le même point : chaque sphère creuse 2,4 m sous le point visé ; dès
   que le fond passe au-delà de la portée (6 m de l'œil), le log dit `refused: no ground within
   reach` et les creux s'arrêtent. Seules les modifications mises
   en file comptent pour les 4 par seconde : un essai refusé ne retarde plus le suivant.
4. **Poser** (clic droit) : un bloc sur la face visée (sur le sol : au-dessus ; sur la paroi : en
   avant d'elle). Viser ses propres pieds : refus, message « Cannot place: too close to the
   character ». Chaque refus est aussi dans le log, une fois par changement de raison :
   `Terrain Build: block 1 m refused: it would close on the pawn`. Monter sur ce qu'on a posé (sauter) : la collision le porte.
5. **Tomber** : se placer sur la plaine, viser sous ses pieds (inclinaison −80), casser avec la
   sphère de 5 m : le personnage tombe dans le trou **sans passer à travers le fond**, et peut en
   ressortir en sautant ou en posant des blocs.
6. **Plantes** : en plaine, casser sous un arbre ou un buisson : la plante disparaît.
7. **Eau** (bord de lac, inclinaison −35° : le sol visé à 3 à 4 m de l'œil ; à −20° il est à
   7,5 m, hors de portée) : casser vers l'eau : le fond se creuse sous la nappe, qui ne bouge pas ;
   casser au bord, sur le sec : un trou sec. Près de la chute : casser la pente, le rideau reste.
8. **Paroi** : casser plusieurs blocs dans la paroi : aucun trou vers le vide autour du creux (avec
   la roche 3D de l'agent 20 aussi).
9. **Pas d'action** : ouvrir la carte de dev (M) et cliquer : rien n'est cassé ; pendant le noir
   d'un `Terrain.Goto` : rien ; avec le pion volant (après `Terrain.Goto`, avant `Terrain.Pawn`) :
   ni fantôme ni modification ; fenêtre sans le focus (Alt-Tab **en tenant le clic gauche**) : les
   creux doivent s'arrêter. Le composant ne lit pas le focus lui-même (aucune API vérifiée en
   5.8) : il compte sur Unreal, qui n'envoie aucune entrée sans le focus et relâche les boutons
   tenus quand il le perd.
10. **Sauvegarde** : `Terrain.EditSave 1`, casser et poser quelques blocs, arrêter le Play
    (« edits saved N times this play »), relancer avec `Building.bSaveEdits` coché : « … saved
    blocks of seed 20261019 read from …\Saved\TerrainEngine\Edits\seed_20261019 » et les trous
    sont là. Décoché : le monde est intact. Abîmer `world.meta` (et effacer `world.meta.bak`) :
    message clair, pas de crash, monde intact.


### Les scènes du monde modifié (agent 41)

Toutes avec **`planet.bin`** (seed 20261019, somme `26626564570b0d9e`), sur la plaine de
`plaine_casser` : le fond sec d'une vallée de 1,6 km de large, sol de 5,3 à 6,4 m, pente 0,2°.
Chaque scène peut avoir son propre Play ; la scène e suit la scène c dans le même Play. Les lieux
sont dans `data/dev/places.json` : `Terrain.Goto <lieu>` pose la vue (x, z, cap, inclinaison, œil
au-dessus du sol), puis `Terrain.Pawn` y pose le personnage. Les coordonnées de `at` sont celles
du core (x vers l'est, y l'altitude, z vers le sud), au centre de la face visée.

**Les attentes entre deux commandes :**
- après `Terrain.Goto` : la ligne `Terrain World: load N report` (le lanceur l'attend déjà) ;
- après `Terrain.Pawn` : `Terrain dev: the pawn of the game stands, spot 1 of …`, précédée de
  `Terrain dev: the pawn of the game: capsule radius 30.0 cm, half height 86.0 cm; walkable floor
  angle 44.765 degrees, step 45.0 cm` (à relever : si l'angle vaut 46° ou plus, on annule la règle
  des parois) ;
- entre deux `Terrain.EditAim` : **0,3 s** (4 modifications par seconde au plus) ;
- avant de viser une surface qu'un appel a créée (le bloc suivant d'un tunnel, le 2e rang d'un
  mur) : la ligne `Terrain World: edit shown … after its click` du précédent ;
- après `Terrain.Shot <nom>` : `Terrain dev: image …: ready after … s`, avant la commande suivante
  (une image à la fois ; ne pas mettre le jeu en pause).

Chaque `Terrain.EditAim` écrit une ligne : `Terrain dev: EditAim <dig|place> <forme> at (x, y, z):
hit (x, y, z), block (x, y, z) | sphere at (x, y, z) radius r m, accepted | refused: <pourquoi>`.
Le point touché (`hit`) est à quelques cm du point demandé ; le bloc ou la sphère doit être
**exactement** celui attendu ci-dessous. Si la ligne dit `refused: the ray hits … m from the point
aimed at`, la caméra de GASP voit la face de biais ou un obstacle est sur la trace : avancer ou
tourner le pion (`Terrain.Look`) et recommencer.

**a) Un creux en sphère** (lieu `modif_sphere` : −240 632 ; −86 717, cap 90, inclinaison −30,
œil +1,8 m ; sol 6,08 m)

```
Terrain.Goto modif_sphere
Terrain.Pawn
Terrain.EditAim dig sphere 2.4375 at -240628 6.09 -86717
    -> sphere at (-240628, 6, -86717) radius 2.438 m, accepted
Terrain.Goto modif_vue_sphere
Terrain.Shot a_sphere
```

À voir : un creux rond de 5 m, sans pointe ni angle vif, dont la lumière tourne sans marches.

**b) Un tunnel 1 × 2 de 6 blocs** (lieu `modif_tunnel` : −240 614 ; −86 697, cap 90, inclinaison
−20, œil +1,8 m ; sol 6,18 m). La plaine n'a pas de paroi : on casse une sphère de 5 m, puis le
tunnel part vers l'est depuis son fond, aux rangs 4 et 5 (sol du tunnel à 3,5 m), sous 0,7 m de
sol. `edit_tunnel_test` vérifie la même scène dans le core.

```
Terrain.Goto modif_tunnel
Terrain.Pawn
Terrain.EditAim dig sphere 2.4375 at -240610 6.19 -86697
    -> sphere at (-240610, 6, -86697) radius 2.438 m, accepted
(marche de 3,5 m au cap 90 : le pion descend au fond de la sphère, sol 3,5 m)
Terrain.Look 90 -10
Terrain.EditAim dig block at -240608.469 4 -86697    -> block (-240608, 4, -86697), accepted
Terrain.EditAim dig block at -240607.469 4 -86697    -> block (-240607, 4, -86697), accepted
Terrain.EditAim dig block at -240607.469 5 -86697    -> block (-240607, 5, -86697), accepted
Terrain.EditAim dig block at -240606.469 4 -86697    -> block (-240606, 4, -86697), accepted
Terrain.EditAim dig block at -240606.469 5 -86697    -> block (-240606, 5, -86697), accepted
Terrain.EditAim dig block at -240605.469 4 -86697    -> block (-240605, 4, -86697), accepted
Terrain.EditAim dig block at -240605.469 5 -86697    -> block (-240605, 5, -86697), accepted
Terrain.EditAim dig block at -240604.469 4 -86697    -> block (-240604, 4, -86697), accepted
Terrain.EditAim dig block at -240604.469 5 -86697    -> block (-240604, 5, -86697), accepted
(marche de 2 m au cap 90, dans le tunnel)
Terrain.EditAim dig block at -240603.469 4 -86697    -> block (-240603, 4, -86697), accepted
Terrain.EditAim dig block at -240603.469 5 -86697    -> block (-240603, 5, -86697), accepted
Terrain.Shot b_tunnel
(marche de 3 m au cap 90 : le pion s'arrête au fond, x vers -240603,2)
Terrain.Look 270 -10
Terrain.Shot b_demi_tour
(marche de 8 m au cap 270 : il ressort au fond de la sphère, x vers -240610)
```

Le bloc (−240 608, 5) appartient déjà à la sphère. À voir et à me renvoyer :
- le pion marche dans le tunnel, fait demi-tour au fond et ressort, sans rester coincé ni glisser
  (sol en V à 43,3°) ;
- si un bloc n'est pas atteint (`refused: no ground within reach (6.0 m of the eye)`), avancer
  d'un mètre et recommencer.

**c) Un trou d'un bloc et un mur d'un bloc** (lieux `plaine_casser` : −240 631 ; −86 677, et
`modif_mur` : −240 631 ; −86 668 ; cap 90, inclinaison −30, œil +1,8 m). Le mur fait 1 bloc
d'épaisseur, 2 de haut et 8 de long, nord-sud, de z −86 671 à −86 664. Il n'a que 2 rangs : un
3e rang se poserait en visant le dessus du 2e (3,3 m au-dessus des pieds), peut-être plus haut
que la caméra, qui toucherait alors une face latérale et poserait le bloc de côté.

```
Terrain.Goto plaine_casser
Terrain.Pawn
Terrain.EditAim dig block at -240628 6.18 -86677     -> block (-240628, 6, -86677), accepted
Terrain.Goto modif_mur
Terrain.Pawn
Terrain.EditAim place block at -240628 6.20 -86671   -> block (-240628, 7, -86671), accepted
Terrain.EditAim place block at -240628 6.20 -86670   -> block (-240628, 7, -86670), accepted
Terrain.EditAim place block at -240628 6.20 -86669   -> block (-240628, 7, -86669), accepted
Terrain.EditAim place block at -240628 6.21 -86668   -> block (-240628, 7, -86668), accepted
Terrain.EditAim place block at -240628 6.21 -86667   -> block (-240628, 7, -86667), accepted
Terrain.EditAim place block at -240628 6.21 -86666   -> block (-240628, 7, -86666), accepted
Terrain.EditAim place block at -240628 6.21 -86665   -> block (-240628, 7, -86665), accepted
Terrain.EditAim place block at -240628 6.22 -86664   -> block (-240628, 7, -86664), accepted
Terrain.EditAim place block at -240628 7.5 -86671    -> block (-240628, 8, -86671), accepted
Terrain.EditAim place block at -240628 7.5 -86670    -> block (-240628, 8, -86670), accepted
Terrain.EditAim place block at -240628 7.5 -86669    -> block (-240628, 8, -86669), accepted
Terrain.EditAim place block at -240628 7.5 -86668    -> block (-240628, 8, -86668), accepted
Terrain.EditAim place block at -240628 7.5 -86667    -> block (-240628, 8, -86667), accepted
Terrain.EditAim place block at -240628 7.5 -86666    -> block (-240628, 8, -86666), accepted
Terrain.EditAim place block at -240628 7.5 -86665    -> block (-240628, 8, -86665), accepted
Terrain.EditAim place block at -240628 7.5 -86664    -> block (-240628, 8, -86664), accepted
Terrain.Goto modif_vue_trou_mur
Terrain.Shot c_trou_mur
```

**d) Un bloc posé dans la capsule** (lieu `modif_capsule` : −240 610 ; −86 650, cap 90,
inclinaison −60, œil +1,8 m ; sol 6,31 m ; le pion se pose sous la vue, spot 1)

```
Terrain.Goto modif_capsule
Terrain.Pawn
Terrain.EditAim place block at -240610 6.31 -86650
    -> block (-240610, 7, -86650), refused: it would close on the pawn
Terrain.EditAim place block at -240612 6.30 -86650   -> block (-240612, 7, -86650), accepted
```

Le refus s'écrit aussi à l'écran : « Cannot place: too close to the character ». Si `Terrain.Pawn`
a posé le pion ailleurs que sous la vue (un autre spot que le 1), le premier bloc peut être
accepté : le dire.

**e) Les blocs vus de loin**, juste après la scène c, dans le même Play. Avec les anneaux de LOD
par défaut (4, 8, 16 et 32 chunks : le log de démarrage écrit `LOD rings 4/8/16/32`), la tuile du
trou et du mur passe au LOD 1 à 60 m à l'ouest du trou, au LOD 2 à 124 m, au LOD 3 à 252 m. Ces
distances sont vérifiées par la sélection des tuiles du core. Les lieux sont au milieu d'un chunk,
de part et d'autre de chaque passage : cap 90, inclinaison −2, œil +1,8 m, z −86 677.

| Lieu | x | Distance au trou | LOD |
|---|---|---|---|
| `modif_lod0_52m` | −240 680 | 52 m | 0 |
| `modif_lod1_68m` | −240 696 | 68 m | 1 |
| `modif_lod1_116m` | −240 744 | 116 m | 1 |
| `modif_lod2_132m` | −240 760 | 132 m | 2 |
| `modif_lod2_244m` | −240 872 | 244 m | 2 |
| `modif_lod3_260m` | −240 888 | 260 m | 3 |

```
Terrain.Goto modif_lod0_52m
Terrain.Shot e_lod0_52m
Terrain.Goto modif_lod1_68m
Terrain.Shot e_lod1_68m
Terrain.Goto modif_lod1_116m
Terrain.Shot e_lod1_116m
Terrain.Goto modif_lod2_132m
Terrain.Shot e_lod2_132m
Terrain.Goto modif_lod2_244m
Terrain.Shot e_lod2_244m
Terrain.Goto modif_lod3_260m
Terrain.Shot e_lod3_260m
Terrain.View lod
Terrain.Goto modif_lod1_68m
Terrain.Shot e_couleurs_lod_68m
Terrain.View normal
```

À voir, au soleil normal puis bas :
- de 52 à 68 m, le passage du LOD 0 au LOD 1 ne se voit pas : le mur reste droit, en roche, à sa
  hauteur ; le trou reste un trait de quelques pixels ;
- au LOD 2 le mur garde la moitié de sa hauteur ; au LOD 3 il disparaît ;
- `e_couleurs_lod_68m` colore les tuiles par LOD.

**f) La sauvegarde**, sans toucher l'acteur ni la carte (lieux `modif_sauvegarde` : −240 611 ;
−86 621, cap 90, inclinaison −40, œil +1,8 m ; et `modif_vue_sauvegarde`)

Play 1 :

```
Terrain.EditSave 1
    -> Terrain World: the edits of seed 20261019 are saved in … from now on
Terrain.Goto modif_sauvegarde
Terrain.Pawn
Terrain.EditAim dig sphere at -240610 6.37 -86625
    -> sphere at (-240610, 6, -86625) radius 1.625 m, accepted
Terrain.EditAim place block at -240607 6.38 -86622   -> block (-240607, 7, -86622), accepted
```

Arrêter le Play **moins d'une seconde** après le dernier appel. Le log dit alors `Terrain World:
edits saved N times this play, …`.

Dans l'éditeur, hors Play :

```
Terrain.EditRead 1
    -> Terrain dev: the next play reads the saved edits: yes
```

Play 2 : le log dit d'abord `Terrain World: the saved edits read and saved this play, as with
Building.bSaveEdits (asked by Terrain.EditRead 1)`, puis `Terrain World: N saved blocks of seed
20261019 read from … (C chunks; its world created …)`. La date de création est celle du monde.

```
Terrain.Goto modif_vue_sauvegarde
Terrain.Shot f_relue
Terrain.EditRead 0
    -> Terrain dev: the next play reads the saved edits: no
```

À voir sur `f_relue` : le creux de la sphère de 3 m et le bloc posé, le dernier compris (il était
encore sur le fil quand le Play s'est arrêté). Les modifications restent dans le dossier de
sauvegarde de la seed. Pour le vider : un Play avec `Terrain.EditSave 1` et aucune modification.
