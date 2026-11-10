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
| `Terrain.EditLight 1` / `0` | le plancher de lumière des surfaces modifiées allumé / éteint (éteint par défaut) | tous |
| `Terrain.EditLightFloor 0.15` | la part de sa couleur de base qu'une surface modifiée émet (0 à 1) | tous |

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
- **Tunnel 1 × 2.** Voir « Tunnels » plus bas : il ne passe pas encore.
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

`edit_tunnel_test` fait glisser une capsule dans des tunnels creusés bloc par bloc (LOD 0) :

| Tunnel | 34/88 cm | 35/90 cm | 42/96 cm |
|---|---|---|---|
| 2 × 2 | passe, sol plat | passe | passe |
| 1 × 3 | passe, sol à 45° (trop raide pour marcher) | passe, 45° | passe, 45° |
| 1 × 2 | il manque 4,4 cm | il manque 9,2 cm | il manque 26,8 cm |

Un trait d'un bloc de large a un sol en V et un plafond en ∧ à 45° : Marching Cubes relie le bas
des parois (au centre du bloc du bas) au milieu du sol. Aplatir le V sans écarter les parois
remonte le sol, et il manque plus de place ; le creuser rend le sol trop raide. Pour faire passer
le 1 × 2 : (a) écarter seulement le bas et le haut des parois à ±0,75 m (passent 34/88 et 35/90,
pas 42/96), ou (b) une collision au bloc près, à part du maillage dessiné. **À décider avec la
vraie capsule du personnage** (rayon et demi-hauteur, lus dans Unreal).

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
94 à 96 % de son contour à 2 pixels près et sa luminance à 3,7/255 près ; un trou d'un bloc (97 pixels,
un trait de 1 à 2 pixels de haut) reste visible (285 pixels, voir les limites), sa luminance à
6/255 près. De 100 à 300 m, le mur garde 92 à 100 % de ses pixels au LOD 1, 27 à 35 % au LOD 2,
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
| paroi (pied de la chute de 388 m) | −6 185 120 | 155 968 | 90 | 10 | +1,8 m | 180,16 m, pente 2,9°, sec ; la paroi (88°, 207,7 m à 4 m) commence 4 m à l'est et monte aussi au sud ; le bassin (eau à 180,03 m) 4 m au nord. Paroi du heightmap : roche 3D quand l'agent 20 sera mergé |
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

### Le monde modifié (agent 41)

Avec `planet.bin`, sur la plaine (`Terrain.Goto plaine_casser`, puis `Terrain.Pawn`) : le sol est
le fond sec d'une vallée, à 6,18 m, pente 0,2° ; la ligne de recul vers l'ouest descend
régulièrement à 5,22 m à 300 m, sèche. C'est un marais (sonde : herbes hautes 0,57 à 0,71, arbres
0,03 à 0,04, buissons 0,06 à 0,09) : si une plante cache les modifications, se décaler de quelques
mètres en z, ou casser autour.

| Lieu (`places.json`) | x | z | cap | incl. | œil | sol (core) |
|---|---|---|---|---|---|---|
| `plaine_casser` | −240 631 | −86 677 | 90 | −30 | +1,8 m | 6,18 m ; le sol visé à 3 m à l'est (−240 628) |
| `modif_recul_30m` | −240 658 | −86 677 | 90 | −2 | +1,8 m | 6,09 m |
| `modif_recul_60m` | −240 688 | −86 677 | 90 | −2 | +1,8 m | 5,99 m |
| `modif_recul_90m` | −240 718 | −86 677 | 90 | −2 | +1,8 m | 5,90 m |
| `modif_recul_150m` | −240 778 | −86 677 | 90 | −2 | +1,8 m | 5,70 m |
| `modif_recul_300m` | −240 928 | −86 677 | 90 | −2 | +1,8 m | 5,22 m |
| `cote_falaise_encoche` (agent 33) | 429 778,8 | −855 913,3 | 61,1 | 10 | 4 m d'altitude | une encoche naturelle au pied d'une falaise (tuiles de LOD 0) |
| `stack_arche` (agent 33) | −26 488 769,4 | 7 401 625,9 | 30 | 6 | 6 m d'altitude | une arche naturelle au niveau de la mer |

1. **Creux lisses** (`plaine_casser`, touche 3) : casser une sphère de 5 m. Un creux rond, sans
   pointe ni angle vif, dont la lumière tourne sans marches ; même chose avec la touche 2 (3 m).
2. **Tunnels** : depuis le fond du creux, casser vers l'est un tunnel de 2 de large et 2 de haut
   (bloc par bloc, touche 1) : le personnage y marche. Puis un tunnel de 1 de large et 2 de haut :
   me dire s'il passe, et me donner la capsule du personnage (rayon et demi-hauteur en cm).
3. **Blocs de loin** : sur le sol visé de `plaine_casser`, casser un bloc (un trou d'un bloc), et
   poser à côté un mur d'un bloc d'épaisseur, de 3 de haut et 8 de long, en travers de la vue
   (cap 0 ou 180). Puis `Terrain.Goto modif_recul_30m`, `60m`, `90m`, `150m` et `300m` (avec les
   réglages de LOD par défaut, la tuile des modifications passe au LOD 1 vers 64 m, au LOD 2 entre
   128 et 190 m, au LOD 3 entre 256 et 290 m, selon sa place ; dans la vue de contrôle : LOD 0 à
   60 m, LOD 1 à 90 et 150 m, LOD 2 à 200 et 256 m, LOD 3 à 300 m) : le mur reste
   droit et en roche au LOD 1, perd la moitié de sa hauteur au LOD 2, s'efface au LOD 3 ; le trou
   reste un trait au passage du LOD 0 au LOD 1. Le passage ne doit pas se voir : me dire si un saut
   se voit, à quelle distance, au soleil normal et bas.
4. **Lumière** : au fond du creux, puis sous `cote_falaise_encoche` et `stack_arche`, et dans une
   grotte (`Terrain.Seed 20261019 caves`) : les commandes de « La lumière des creux », une par une.
   Me dire si les creux naturels ont le même noir que les modifications, puis seulement
   `Terrain.EditLight 1` (et `Terrain.EditLightFloor 0.1` à `0.3`).
5. **Sauvegarde** : `Building.bSaveEdits` coché, casser puis arrêter le Play **dans la seconde du
   clic** ; relancer : le dernier creux est là, et le log de lecture donne la même date de création
   du monde (« its world created … ») d'un Play à l'autre.
