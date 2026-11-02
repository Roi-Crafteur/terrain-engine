# Casser et poser en jeu (agent 28, phase 15)

Le joueur vise le terrain et retire de la matière (clic gauche) ou en ajoute (clic droit), avec
un bloc de 1 m ou une sphère de 2 ou 4 m de diamètre. Un fantôme montre ce qui sera retiré ou
posé. Les modifications passent par le fil de modifications de la session : les tuiles sont
refaites avec leur collision et sans les plantes dont le sol est parti, et l'horizon lointain
suit. Elles peuvent être sauvegardées par seed.

## Qui décide quoi

| Sujet | `terrain_core` et le Bridge (données, déterministe, testé par ctest) | Unreal (affiche, fait jouer) |
|---|---|---|
| Visée | `Bridge/block_targeting` : point touché et normale → bloc cassé (le centre du bloc à 0,5 m sous la surface, le long de la normale), bloc posé (son voisin du côté de la face principale de la normale), sphère cassée (centrée sur le point), sphère posée (son centre à un demi-rayon hors de la surface) ; portée ; tout en entiers dans la modification | trace depuis la caméra sur les tuiles (`ECC_Visibility`, collision complexe), distance mesurée depuis l'œil du pion |
| Outils | bloc 1 m, sphère 2 m, sphère 4 m (rayons 16 et 32 seizièmes de mètre) | touches 1, 2, 3 et molette, écrit à l'écran |
| Règles | `Bridge/build_rules` : refus si la carte de dev est ouverte, pendant un chargement, avec un autre pion que celui du composant, sans le focus ; 4 modifications par seconde au plus bouton tenu ; refus d'un volume posé à moins de 5 cm de la capsule | lit la carte, le chargement, le contrôleur, le focus du viewport, la capsule |
| Application | `applyVolumeEdit` sur le fil de modifications (existant), plantes retirées, coutures fermées, horizon | maillage et collision des tuiles refaites, envoyés dans le budget de 6 ms de la #58 |
| Fantôme | boîte ou sphère en coordonnées du core puis d'Unreal | `/Engine/BasicShapes/Cube` ou `Sphere` avec le matériau filaire du moteur (aucun `.uasset`) |
| Sauvegarde | `Bridge/edit_save` : dossier v2 par seed, fil d'écriture, `.tmp` puis renommage, `.bak` ; refus d'un monde illisible, d'une autre seed, d'une autre taille | `Building.bSaveEdits`, `Terrain.EditSave 0\|1` |
| Mesure | `Bridge/edit_latency` : clic → fait → tuile envoyée, médiane, p95, max, images | `Terrain.EditStats` (outils de debug seulement) |

## Les réglages

Sur `Terrain World`, catégorie `Terrain|Edits`, structure `Building` :

- `bAddToPlayerPawn` (oui) : le pion du joueur reçoit un `Terrain Build` s'il n'en a pas (jamais
  le pion volant des outils) ;
- `bSaveEdits` (**non**) : les modifications de la seed sont lues au début du Play dans
  `Saved/<SaveFolder>/seed_<seed>/` et écrites là toutes les `SaveIntervalSeconds` (10 s) s'il y a
  eu des modifications, et à la fin du Play ; `SaveFolder` vaut `TerrainEngine/Edits`. Jamais le
  fichier planète.

Sur le composant `Terrain Build` (catégorie `Terrain|Build`) : `ReachMeters` (6 m, depuis l'œil du
pion, pas depuis la caméra de troisième personne), `Tool`, `EditsPerSecond` (4), `bShowGhost`,
`GhostMaterial` (vide : le matériau filaire du moteur), et l'entrée : `MappingContext` et ses six
actions (`BreakAction`, `PlaceAction`, `CycleToolAction`, `BlockToolAction`,
`SmallSphereToolAction`, `LargeSphereToolAction`). Vides : créés au début du Play avec les touches
par défaut. Pour changer les touches, donner un contexte **et** ses actions. Le contexte est ajouté
avec la priorité `MappingPriority` (1, au-dessus du personnage). Les touches déjà prises par
d'autres actions du joueur sont écrites une fois dans le log :
`Terrain Build: the key LeftMouseButton of TerrainBreak is also mapped to …`.

Le nœud Blueprint `Build (Break | Place)` fait la même chose qu'un clic (vrai si la modification
est en file ; sinon `Get Last Refusal` dit pourquoi).

## Les commandes

| Commande | Effet | Build |
|---|---|---|
| `Terrain.EditSave 1` | sauvegarde à partir de maintenant : la sauvegarde de la seed est **remplacée** par les modifications de ce Play (régions écrites à nouveau, régions sans modification supprimées) | tous |
| `Terrain.EditSave 0` | une dernière sauvegarde, puis plus rien | tous |
| `Terrain.EditStats` | les dernières modifications du joueur : clic → fait, clic → tuile affichée (médiane, p95, max), images, et les sauvegardes | outils de debug seulement |

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
| bloc 1 m | 1,1 ms | 2,4 ms | 2,6 ms | 2 au plus (à 1 ms l'image) |
| sphère 2 m | 1,2 ms | 2,3 ms | 3,3 ms | 2 au plus |
| sphère 4 m | 1,1 ms | 2,2 ms | 2,3 ms | 2 au plus |

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
| paroi (pied de `k2_paroi`) | 416 017 | −11 414 812 | 175 | 10 | +1,8 m | 6 715 m, pente 75° ; roche 3D quand l'agent 20 sera mergé |
| bord de lac (`lac_montagne`) | 7 370 340 | −7 088 911 | 270 | −20 | +1,8 m | 1 726,9 m, sec ; l'eau (1 725,26 m) commence 30 à 40 m à l'ouest |
| chute de 388 m | −6 185 124 | 155 984 | 227 | 0 | +1,8 m | 260,1 m, pente 35°, près du bassin de `chute388_bassin` |
| K2 | voir `Terrain.Goto k2_paroi` puis `Terrain.Pawn` | | | | | |

Sur chaque lieu :

1. Avant tout : `Terrain.EditSave 0` n'est pas nécessaire (coupé par défaut). Le log doit dire
   `Terrain World: breaking and placing given to <pion>` et, s'il y a des conflits, les lignes
   `Terrain Build: the key … is also mapped to …` (à me renvoyer, surtout pour la molette, le clic
   droit et 1/2/3 avec GASP).
2. **Fantôme** : un cube filaire de 1 m sur le sol visé à moins de 6 m de l'œil ; rien au-delà.
   Touches 2 et 3 (ou la molette) : une sphère de 2 m puis de 4 m ; `Tool: …` en haut de l'écran.
3. **Casser** (clic gauche, touche 1) : un creux d'environ 1 m dans l'image du clic ou la suivante.
   Tenir le bouton : 4 creux par seconde, pas plus. Puis `Terrain.EditStats` : « click -> tile
   shown » en millisecondes et en images (but : 0 ou 1 image).
4. **Poser** (clic droit) : un bloc sur la face visée (sur le sol : au-dessus ; sur la paroi : en
   avant d'elle). Viser ses propres pieds : refus, message « Cannot place: too close to the
   character ». Monter sur ce qu'on a posé (sauter) : la collision le porte.
5. **Tomber** : se placer sur la plaine, viser sous ses pieds (inclinaison −80), casser avec la
   sphère de 4 m : le personnage tombe dans le trou **sans passer à travers le fond**, et peut en
   ressortir en sautant ou en posant des blocs.
6. **Plantes** : en plaine, casser sous un arbre ou un buisson : la plante disparaît.
7. **Eau** (bord de lac) : casser vers l'eau : le fond se creuse sous la nappe, qui ne bouge pas ;
   casser au bord, sur le sec : un trou sec. Près de la chute : casser la pente, le rideau reste.
8. **Paroi** : casser plusieurs blocs dans la paroi : aucun trou vers le vide autour du creux (avec
   la roche 3D de l'agent 20 aussi).
9. **Pas d'action** : ouvrir la carte de dev (M) et cliquer : rien n'est cassé ; pendant le noir
   d'un `Terrain.Goto` : rien ; avec le pion volant (après `Terrain.Goto`, avant `Terrain.Pawn`) :
   ni fantôme ni modification ; fenêtre sans le focus (Alt-Tab) : rien.
10. **Sauvegarde** : `Terrain.EditSave 1`, casser et poser quelques blocs, arrêter le Play
    (« edits saved N times this play »), relancer avec `Building.bSaveEdits` coché : « … saved
    blocks of seed 20261019 read from …\Saved\TerrainEngine\Edits\seed_20261019 » et les trous
    sont là. Décoché : le monde est intact. Abîmer `world.meta` (et effacer `world.meta.bak`) :
    message clair, pas de crash, monde intact.
