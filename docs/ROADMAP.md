# Roadmap

Vision : un monde si beau qu'on peut passer des heures à contempler son immensité.
Une planète de 60 000 × 35 000 km, cohérente à toutes les échelles, en chunks de 16 × 16 m,
avec un relief type Terralith / TerraForged, des grottes type YUNG's, un horizon lointain
type Distant Horizons / Voxy, et une destruction au bloc de 1 m dans un monde lisse et réaliste.

Principe : `terrain_core` décide de la forme du monde (déterministe, identique sur toutes les
machines), Unreal décide de son apparence (rendu, joueur, interface, réseau).
Le viewer OpenGL reste un outil de debug : plus aucun ajout visuel.

Référence : le mod Minecraft Big Globe (Builderb0y, github.com/Builderb0y/BigGlobe ; copie
locale hors dépôt dans `C:\Projetseferences\BigGlobe`). Licence CC BY-NC 4.0 et souhait de
l'auteur de ne pas reprendre de grandes parties de son code : on s'inspire des principes et on
réécrit tout nous-mêmes, sans copier ni traduire son code.

## Réalisé

### Phase 1 — Base
- CMake, tests, fenêtre OpenGL, caméra libre, cube

### Phase 2 — Marching Cubes
- grille de densité, meshing Marching Cubes, normales, chunk 16x16x16

### Phase 3 — Heightmap
- continentalité, masque de montagne, domain warping, ridged noise, export PNG
- couche macro-continentale et plaques tectoniques (Voronoi)

### Phase 4 — Érosion et rivières
- érosion hydraulique, accumulation d'eau, rivières, cascades (régions finies)

### Phase 5 — SDF et falaises
- 5A : plateaux, murs verticaux, gorges
- 5B : surplombs, arches

### Phase 6 — Biomes
- climat, biomes, matériaux de surface, végétation

### Phase 7 — Streaming et LOD
- chunks autour du joueur, génération asynchrone, LOD 0 à 3 avec coutures
- modifications persistantes (blocs de 1 m, plantes), sauvegarde

### Phase 8 — Grottes
- 8A : spaghetti, noodles, cheese, ravines, entrées
- 8B : aquifères, lacs, glace, spéléothèmes, matériaux
- 8C-1 : régions de grottes (districts), profils, flore
- 8C-2 : grands réseaux de galeries, rivières souterraines en pente douce (sèches au-delà
  de 10 %), cavernes à lave sous les galeries, parois de magma

### Phase 9 — Fondations
- CI multiplateforme : Windows (MSVC), Linux (GCC, Clang), macOS (AppleClang, ARM)
- déterminisme multiplateforme : pas de FMA, options flottantes figées, fonctions
  mathématiques maison ; golden identiques sur les 3 plateformes
- monde bouclé de 60 030 976 × 34 996 224 m : est ↔ ouest et nord ↔ sud, bords polaires,
  tous les bruits périodiques, raccord exact au bit près
- format de monde v1 : world.meta, fichiers de région, dossiers de mondes, écritures
  durables ; version du générateur 2
- eau statique et blocs suspendus pour commencer

### Phase 10 — Tranche verticale Unreal (partie 1)
- plugin `terrain_unreal/` qui compile les sources de terrain_core, testé dans UE 5.8
  (Windows, VS 2026) : autocontrôle du déterminisme au bit près dans Unreal
- tuiles streamées en ProceduralMeshComponent, LOD 0 à 3, collisions, sol prêt sous le pion
- tuiles montrées à leur image la plus proche du joueur : pas de trou au coin du monde
- coût mesuré : environ 0,6 ms de génération et 0,5 ms d'envoi par tuile LOD 0
  (0,25 ms de génération après l'optimisation du bruit)
- le joueur ramené d'un monde entier après la majeure partie d'un tour (coordonnées Unreal
  bornées), sans saut visible ni rechargement des tuiles

### Performance du bruit
- relief environ 2 fois plus rapide, sans changer un seul bit du monde (référence gelée de
  l'ancien code) : octaves précalculées, bouclage sans division, pas de bruit de masque en
  mer, lecture du relief par lots
- grilles de hauteur plus rapides qu'avant la phase 9

### Phase 11A — Niveau Planète
- grille de toute la planète (cellules de 8 km), calculée depuis la seed en environ 1 min,
  identique au bit près quel que soit le nombre de threads ; 220 Mo gardés
- climat continental (vents, pluie, ombres orographiques, glace), lacs et bassins
  (Fill-Spill-Merge), débits, graphe des fleuves, sites pour 11B (deltas, estuaires,
  chutes, gorges)
- mesures du relief actuel : il ne draine pas (lacs sur 27 % des terres du monde
  planétaire, fleuves qui traversent surtout des lacs) : objectifs chiffrés de 11R

### Phase 11R — Relief planétaire
- la géographie planétaire devient le monde par défaut (version du générateur 3) : carte de
  toute la planète calculée une fois, identique au bit près quel que soit le nombre de threads
- continents indépendants des plaques (37 % de terres, 5 à 10 continents), côtes à toutes les
  échelles, plateaux continentaux, fonds océaniques, altitude régionale qui draine vers la mer
- chaînes en larges ceintures aux convergences, hauts plateaux derrière, rifts en marches aux
  divergences, dorsales, arcs insulaires ; provinces (plaines, bassins, vieux massifs, plateaux)
- outils de cartes et de chiffres du relief (`terrain_export_relief`)

### Phase 11R bis — Relief naturel
- intensité du relief continue, sans bord ; montagnes ridged aiguës, ravins en V et roche sur
  les pentes ; frontières de plaques courbées, chaînes coupées, bords de plateaux
- parois rocheuses, vallées en U et sommets dominants (jusqu'à 8 392 m, vus à plus de 15° du
  fond des vallées) ; montagnes gelées depuis (`mountain_freeze_test`)
- plaines plates ou ondulées, collines isolées, réseau de vallons (formes des plaines,
  seulement hors du masque des montagnes)
- vues en perspective à hauteur d'homme (`terrain_export_view`), vérifiées dans Unreal avec le
  personnage
- coût : montagnes ×2,6 et plaines ×1,9 de l'ancien relief en patchs

### Phase 11B — L'eau suit le relief
- mer décidée par la géographie (reliée à l'océan) et non plus par l'altitude ; dépressions sous
  le niveau de la mer, sèches (fond en relief, falaise du côté raide, buttes, croûtes de sel) ou
  en lacs
- niveau Planète gardé sur disque (`terrain_make_planet` : 247 Mo, environ 100 s sur 4 cœurs),
  relu en flux (273 Mo au plus pendant le chargement, 372 Mo en jeu) ; fonds des cellules,
  petites dépressions percées
- fleuves creusés après le relief, sans le déformer : lits, vallées ouvertes en plaine (berges à
  7° en médiane, 13,5° au 90e centile), gorges de roche dans les montagnes, affluents qui
  descendent dans la vallée qu'ils rejoignent, levées, lacs fins au niveau des lacs de la planète
- chutes aux seules ruptures de pente : à-pic jusqu'à 400 m ou gradins irréguliers (hauteurs,
  espacements, faces de 70 à 88°, marches doubles), dans une fente au fond de la vallée dont la
  largeur, les bords et l'axe varient, creusée aussi aux lèvres des marches, avec des avancées
  de roche et des blocs dans les bassins ; plus de dents de scie le long de la fente
  (`river_falls_test`)
- sorties pour Unreal : type et niveau de l'eau dans les échantillons, Bridge
- climat de la planète branché sur les biomes (humidité tirée de la pluie et de l'évaporation)
- strates et joints des parois de la 11R bis d'épaisseurs irrégulières, plus de motif régulier
  en peigne : montagnes gelées refaites (`mountain_freeze_test`)
- coût de l'eau, du relief seul au relief avec l'eau, sans changer un bit : en patchs, terres
  ×1,08, montagnes ×1,04, à moins de 1 km d'un fleuve ×1,78 ; en tuiles ×1,03, ×1,05 et ×1,71
  (`water_cost_test` : au plus ×1,3 loin de l'eau, ×2 près des fleuves)

### Phase 13 — Horizon lointain (core)
- 10 niveaux de grilles de hauteur et de matériau autour du joueur (8 m à 4 km, rayon
  520 km), mis à jour par bandes en arrière-plan, identiques au bit près sur toutes les
  plateformes
- raccord exact avec les tuiles LOD 3 ; panorama 360° de contrôle : relief visible
  jusqu'à 400 km depuis un sommet

## À venir

### Phase 10 — suite
- Character qui marche, saute et tombe sur le terrain : vérifié avec le Character du
  moteur (0 passage sous le sol sur 1,7 km à 108 km/h) ; reste le pack Third Person
- type de mesh choisi par mesure : Dynamic Mesh, ProceduralMeshComponent ou
  RealtimeMeshComponent
- compilation du plugin sous Linux et Mac (risque `std::jthread`)
- meshing environ 7 fois plus lent sous Clang que sous GCC : corrigé (même maillage au
  bit près)

### Phase 11 — Hydrographie continentale (approche TerraForged)
- principe : le relief d'abord, l'eau le suit. Le relief monte et descend librement
  (failles, rifts, bassins fermés, dépressions sous le niveau de la mer) ; puis l'eau naît
  aux sources, descend toujours, remplit les creux jusqu'à leur point de débordement (lacs,
  ou lacs salés et déserts si le climat est sec), creuse son lit, tombe aux ruptures de
  pente et finit dans la mer. Le relief n'est jamais déformé pour que l'eau coule bien
- niveau Planète précalculé à la création du monde (attente longue acceptée)
- climat continental : les fleuves en dépendent (pas de source dans un désert,
  des fleuves qui peuvent traverser un désert)
- graphe des grands fleuves par continent, taille réaliste jusqu'à l'Amazone
- deltas, estuaires, grands lacs, méandres, bras morts
- canyons et gorges fluviales, grandes chutes aux escarpements (type Niagara)
- sorties : splines (Water d'Unreal) et contraintes de hauteur
- 11A, 11R, 11R bis et 11B : faits (voir « Réalisé »)
- tâche suivante, dès le merge de la 11B (7b) : le niveau de l'eau pour Unreal, `waterLevel` aux
  sommets des tuiles et dans `HorizonCell`, pour dessiner les lacs et les fleuves à leur niveau
  (aujourd'hui toute l'eau est dessinée à 0 m)
- côté Unreal (7a, 7c) : fait. Le carré de mer sous les tuiles n'est dessiné que là où le core
  met l'océan (matériau Masked ; chemin CPU : le carré entier, ou rien s'il n'a aucun
  échantillon de mer), et la couleur du fond marin seulement sous le drapeau mer
- côté Unreal : damier de carrés sombres sur les collines : fait. Ce n'était pas le tirage `Bomb`
  (le damier restait avec `DetailNearM = 0`) mais les mailles de 8 m du niveau 0 de l'horizon,
  qui traversaient les tuiles au-delà de la bande où sa descente s'efface ; le sol du niveau 0
  est repoussé de 1 m en profondeur là où les tuiles peuvent le couvrir
- création du niveau Planète en 500 Mo : phase 11D
- liaison avec les rivières souterraines (pertes, résurgences) : plus tard
- sans les cols du relief fin entre les cellules du niveau Planète, les fleuves de la 11B
  traversent les crêtes des montagnes en gorges (un tronçon de montagne sur trois creuse plus
  de 300 m) ; avec eux, l'eau remplit les vallées fermées des montagnes gelées jusqu'à leurs
  cols (essai du point d'arrêt 3 : 1 à 49 % de lacs dans les fenêtres de haute montagne avec
  un lit de 300 m, 19 % encore avec 1 000 m). C'est la phase 11C qui règle cela

### Phase 11C — Vallées de montagne qui descendent (prochaine phase, après la tâche 7b)
- dégel des montagnes : `mountain_freeze_test` est levé pour cette phase, puis refait
- refaire les vallées des montagnes en un vrai réseau qui descend vers les plaines : un arbre
  de vallées qui draine, leurs fonds toujours en pente vers l'aval, de la crête à la plaine. Les
  vallées de la 11R bis sont des tronçons fermés (boucles des lignes de bruit, fonds qui montent
  et descendent avec les cols, fins en cirques) : l'eau ne peut en sortir qu'en franchissant un
  col ou une crête
- les fleuves de la 11B suivent alors ces vallées : plus de gorges à travers les crêtes, peu
  de lacs (les lacs de haut bassin seulement), des lits de moins de 300 m
- nouvelle validation des montagnes à l'œil, dans Unreal, avec le personnage

### Phase 11D — Création du niveau Planète en 500 Mo
- aujourd'hui la création du monde (`terrain_make_planet`) monte à 1 080 Mo sur 4 cœurs
  (1 050 Mo sur un cœur), quand le jeu chargé n'en demande que 300 (372 Mo en jeu)
- mémoire après chaque étape : relief 258 Mo, climat 450, ordre 603, hiérarchie 848,
  lacs 725, écoulement 848, fleuves 1 022
- but : 500 Mo au plus, les mêmes bits (somme du niveau et goldens identiques)
- pistes : libérer les tableaux de chaque étape dès qu'elle est finie, des tableaux plus
  compacts pour l'ordre et la hiérarchie, le fichier écrit en flux comme il est lu

### Phase 12 — Monde streamé unifié
- régions et chunks générés à la volée, mis en cache, contraints par le niveau Planète
- érosion hydraulique, glaciaire (vallées en U, cirques, fjords), thermique (éboulis)
  et côtière (falaises marines, arches, stacks)
- reliefs emblématiques : chaînes alpines / himalayennes, plateaux et mesas,
  karst et pitons, volcans
- mers de dunes (ergs) à l'échelle réelle (20 à 300 m de haut, 1 à 3 km d'espacement),
  province du relief au cœur des continents secs, jamais placée par le biome
- falaises, surplombs et arches dans le monde infini
- érosion par ondes alignées sur la pente (sillons et ravins ramifiés à toutes les tailles,
  de 4 m à 256 m, type Big Globe) si elle n'est pas déjà venue avec la 11R bis
- reliefs fantastiques, rares et spectaculaires (médiéval-fantasy), placés par le niveau
  Planète, jamais par le biome :
  - îles flottantes au-dessus des mers et des plaines basses, jamais au-dessus des montagnes
    ni dans le froid : un dessous qui pend en cuvette et un dessus avec relief et érosion,
    dans le SDF ; types variés (montagne en spirale, plateformes étagées, plaines fissurées,
    plaines percées) ; nuages en dessous
  - monolithes (obélisques) : prismes très hauts, parfois creux, raccordés au terrain par
    une courbe (le sol monte ou descend jusqu'à leur pied)
  - visibles depuis l'horizon lointain (phase 13)
- fin des deux mondes séparés (régions finies / monde streamé)

### Phase 13 — Horizon lointain
- vue jusqu'à l'horizon réel (300 km et plus depuis un sommet)
- core : fait (voir « Réalisé »)
- modifications des joueurs visibles de loin : toutes si possible, sinon les grosses
  (format du résumé à définir avec la phase 15)
- Unreal : fait — rendu en clipmap (une texture par niveau, envoi des seules bandes
  changées, géomorphing), époques sur les tâches d'Unreal moins prioritaires que les tuiles,
  jupe sous le bord du niveau 0, tuiles arrêtées vers 850 m, courbure ; ciel qui suit la
  caméra partout sur la planète (perspective aérienne jusqu'au bout de l'horizon), mer
  colorée selon la profondeur avec vagues, reflet du ciel et du soleil
- préfiltrage des crêtes lointaines : fait — à partir du niveau 1, le coin le plus haut de
  chaque maille monte au sommet de la maille, cherché 2 fois plus finement (les fonds de vallée
  ne montent jamais) ; silhouette trop basse de 0,10° au lieu de 0,17° en moyenne depuis la
  vallée du K2, pour ×1,4 sur l'époque. Plus tard : 4 fois plus finement (0,08°), presque
  gratuit avec une grille commune aux normales de détail, calculée en arrière-plan

### Phase 14 — Rendu Unreal
- références : Enshrouded, shaders Minecraft (Complementary, BSL, SEUS),
  Red Dead Redemption 2, photos réelles
- couleurs naturelles saturées et chaleureuses, ambiance chaude ou froide selon le lieu
  et la météo
- ciel : couchers spectaculaires et rayons de lumière, nuages volumétriques qui ombrent
  le relief, nuits étoilées (Voie lactée, phases de la lune), brume dans les vallées et
  perspective atmosphérique
- eau : transparente avec caustiques, vagues et écume, couleur selon le lieu (turquoise,
  vert glacier, brune, noire), cascades spectaculaires (embruns, arcs-en-ciel)
- végétation PCG depuis les positions du core, densité selon le biome : forêts denses ou
  clairsemées, clairières, monts, vallons
- transitions entre biomes (aujourd'hui un seul biome et un seul matériau par point, à
  seuils nets) : frontières irrégulières par du bruit sur les seuils (avancées, bosquets
  isolés, clairières), poids des 2 ou 3 biomes et matériaux dominants calculés par le core
  de façon déterministe, fondus de textures et densité de végétation progressive dans
  Unreal ; largeur réaliste selon le couple (quelques km entre forêt et prairie, quelques
  mètres entre plage et roche). Le relief n'en dépend pas : les biomes ne modifient
  jamais la hauteur. Unreal : fondu des textures fait sur les tuiles (matériau temporaire,
  texels de chaque tuile dans une texture commune) ; l'horizon garde le biome dominant de ses
  échantillons, les tuiles s'y fondent sur leur bande au-dessus du niveau 0. Reste : les poids
  dans les échantillons de l'horizon (core), la densité de la végétation
- matériaux depuis les identifiants de matériau du core ; très haut détail par des
  rochers, falaises et arbres Nanite placés par PCG et du relief fin dans le matériau
  (Nanite ne s'applique pas directement aux tuiles générées en jeu)
- réglages graphiques jusqu'à « Ultra » / « Natif » (Nanite et détail partout), et un
  mode sans Lumen ni Nanite pour les anciennes machines
- Water branché sur les splines de rivière
- jour / nuit, météo régionale, aurores, éclipses et phénomènes rares ; saisons et
  aspect de l'horizon lointain (brumeux, net ou selon la météo) à décider plus tard

### Peuplement — royaumes, villes, villages et routes (après la 11B et la 12)
- hiérarchie de peuplement générée de haut en bas, jamais une structure posée seule :
  royaume → capitale → villes → bourgs → villages → hameaux ; puis ville → quartiers →
  rues → parcelles → bâtiments
- sites et routes calculés sur le niveau Planète (comme les fleuves, déterministes, identiques
  sur toutes les machines), le détail (quartiers, parcelles) à la volée dans les régions
  streamées : une capitale de plusieurs km n'est jamais générée d'un coup
- choix des sites par le terrain : gués, confluents, estuaires, ports abrités, éperons
  défendables, vallées fertiles, cols, ressources (mines) ; climat, pente, altitude, eau
- routes : liaisons pondérées par la population et la distance (modèle de gravité), tracées
  par coût (pente, rivières à franchir par pont ou gué, vallées, cols, lacets en montagne),
  regroupées, avec des boucles et pas seulement un arbre
- villes qui poussent depuis leur site (le long des routes, de la rivière, autour d'un
  château ou d'un marché, faubourgs hors les murs) : pas d'anneaux au compas ni de quartiers
  Voronoi réguliers, pas de zones strictes
- le peuplement modifie le relief localement (routes creusées ou en remblai, terrasses,
  plateformes, éperon aplani pour un château, quais) : une étape dédiée, déterministe, après
  les rivières ; les biomes, eux, ne modifient toujours pas la hauteur
- trois axes indépendants (une seule table biome → style referait des zones strictes) :
  - climat → matériaux locaux, agriculture et densité (bois, torchis, chaume en plaine ;
    pierre et ardoise en montagne ; argile et pierre au désert ; pilotis au marais), lus sur
    le climat du niveau Planète, pas sur le biome au point
  - culture → style architectural, défenses, noms ; territoires propres qui peuvent
    traverser plusieurs biomes et s'y adapter, transitions progressives aux frontières
  - site → fonction (port, ville minière, ville caravanière, ville fluviale, oasis, place
    forte de col)
- le core décide du plan (sites, routes, parcelles ; pour chaque bâtiment : emprise, hauteur,
  type, style, orientation, matériaux), Unreal assemble les bâtiments depuis des kits modulaires (PCG) : aucune géométrie de
  bâtiment dans `terrain_core`
- économie : seulement des étiquettes de ressources et de production pour le gameplay plus
  tard (pas de simulation)
- étapes :
  1. données seules : sites, hiérarchie et routes sur le niveau Planète, vérifiés sur des
     cartes de la planète (aucun bâtiment)
  2. un village complet dans Unreal, une culture, un kit modulaire : style et adaptation
     au terrain
  3. bourgs et villes, une deuxième culture et un deuxième climat
  4. capitale : château sur une hauteur, remparts et portes, cathédrale, marché, port,
     faubourgs
  5. cultures et archétypes supplémentaires (désert, montagne, côte, marais, toundra, jungle)
- décisions de game design :
  - monde sauvage, royaumes rares ; par royaume : rayon de 2 000 à 5 000 km, 1 capitale,
    3 à 8 villes, 10 à 30 bourgs, 30 à 120 villages
  - à vérifier sur les cartes de l'étape 1 : à ces chiffres, deux villages voisins sont à
    environ 300 à 1 600 km l'un de l'autre (plusieurs jours de marche) ; ajuster la taille
    des royaumes, le nombre de villages ou ajouter des hameaux et des haltes isolés si le
    monde paraît vide, et lier aux transports (« Plus tard »)
  - médiéval-fantasy
  - kits modulaires Unreal assemblés par PCG
  - intérieurs : plus tard ; bâtiments destructibles : plus tard (phase 15)
  - villes visibles depuis l'horizon lointain (phase 13) : silhouettes avec un LOD propre,
    lumières la nuit

### Phase 15 — Jeu solo
- modes : exploration / contemplation, survie, créatif réservé aux administrateurs
- nouveau monde : nom, seed, aperçu de la carte et choix du point d'apparition
- sauvegarde et chargement
- destruction de bloc en jeu
- modifications en différences, sous une forme qui tient les grosses destructions — core :
  fait (format de monde v2, l'ancien format v1 toujours lu) :
  - chaque chunk découpé en cubes de 16 × 16 × 16 blocs : inchangé, entièrement vidé,
    entièrement rempli, liste de blocs ou masque compressé (codeur arithmétique en entiers)
  - opérations volumiques (sphère, boîte, explosion) : 15 ou 19 octets, appliquées de façon
    déterministe par chaque machine dans l'ordre de leur numéro, puis enregistrées dans les
    masques
  - mesuré (`terrain_modification_sizes`) : un trou de 10 m fait 134 à 300 octets ; une
    montagne rasée (1 km × 1 km × 500 m) fait 294 Ko par une boîte, 833 Ko par 47 000
    explosions, contre 547 Ko et 1,4 Mo pour les chunks complets compressés de Minecraft
    (16 et 17 Mo dans ses secteurs de 4 Ko) ; une tuile sur un trou coûte 1,05 à 1,17 fois
    la même tuile intacte, une tuile au milieu d'une zone rasée 1,2 à 2 fois, une tuile sur la
    falaise qui la borde 4 à 6 fois (4 à 28 fois plus de surface)
  - les plantes générées dont le sol est retiré disparaissent, de près comme de loin
  - résumé des modifications pour l'horizon lointain (phase 13) tiré des masques : format
    seulement, pas encore branché sur l'horizon
  - reste : casser et poser en jeu dans Unreal, effets de destruction, branchement du résumé
    sur l'horizon, coutures entre LOD au bord des cratères (jusqu'à 0,26 m, le recousage
    interpole la face fine là où la face grossière est droite)

### Phase 16 — Multijoueur
- LAN, serveur dédié communautaire (Windows, Linux, Mac), serveurs officiels
- coop et serveurs de 16 à 100+ joueurs
- le serveur fait autorité : il génère lui aussi le terrain autour des joueurs (sans rendu),
  les clients n'envoient que des intentions (casser, poser, se déplacer), vérifiées par le
  serveur dans sa propre version du monde
- seed secrète côté serveur pour les secrets (minerais rares, trésors, ressources de
  survie) : la forme du monde reste calculée par les clients, les secrets jamais (anti X-ray)
- réplication des modifications, vérification du déterminisme entre clients

### Phase 17 — MMO
- serveurs officiels à très grande capacité
- monde réparti entre plusieurs serveurs (zones ou sharding)

### Chantier parallèle — Gameplay
- survie : ressources, artisanat, dangers
- outils d'administration (mode créatif)

### Plus tard
- eau physique complète (écoulement, remplissage, interaction avec la destruction)
- intégrité structurelle et effondrements
- liaison fleuves de surface / rivières souterraines
- consoles
- transports pour une planète de 60 000 × 35 000 km
