# Roadmap

Vision : un monde si beau qu'on peut passer des heures à contempler son immensité.
Une planète de 60 000 × 35 000 km, cohérente à toutes les échelles, en chunks de 16 × 16 m,
avec un relief type Terralith / TerraForged, des grottes type YUNG's, un horizon lointain
type Distant Horizons / Voxy, et une destruction au bloc de 1 m dans un monde lisse et réaliste.

Principe : `terrain_core` décide de la forme du monde (déterministe, identique sur toutes les
machines), Unreal décide de son apparence (rendu, joueur, interface, réseau).
Le viewer OpenGL reste un outil de debug : plus aucun ajout visuel.

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
- 11A : fait (voir « Réalisé »)
- 11R : relief planétaire, avant de creuser les fleuves (les cartes de la planète entière
  montrent un monde par défaut en archipel uniforme, et des continents planétaires qui
  ont la forme des plaques) :
  - côtes découpées indépendantes des plaques, plateaux continentaux, mers intérieures
  - chaînes de montagnes en larges ceintures (centaines de km) aux convergences, piémonts
    et hauts plateaux derrière (type Himalaya / Tibet, Andes / Altiplano), rifts aux
    divergences
  - provinces à l'intérieur des continents : grandes plaines, vieux massifs usés,
    bassins, plateaux
  - le monde planétaire devient le monde du jeu (version du générateur 3)
- 11B : mer décidée par la géographie (reliée à l'océan) et non plus par l'altitude :
  dépressions sous le niveau de la mer, sèches ou en lacs salés (type mer Morte, vallée de
  la Mort)
- 11B : creusement des lits, lacs, canyons et chutes dans le relief, nouveau climat
  branché sur les biomes ; niveau Planète gardé sur disque (au lieu d'être recalculé) et
  pic de mémoire de sa construction réduit (1 Go aujourd'hui)
- liaison avec les rivières souterraines (pertes, résurgences) : plus tard

### Phase 12 — Monde streamé unifié
- régions et chunks générés à la volée, mis en cache, contraints par le niveau Planète
- érosion hydraulique, glaciaire (vallées en U, cirques, fjords), thermique (éboulis)
  et côtière (falaises marines, arches, stacks)
- reliefs emblématiques : chaînes alpines / himalayennes, plateaux et mesas,
  karst et pitons, volcans
- mers de dunes (ergs) à l'échelle réelle (20 à 300 m de haut, 1 à 3 km d'espacement),
  province du relief au cœur des continents secs, jamais placée par le biome
- falaises, surplombs et arches dans le monde infini
- fin des deux mondes séparés (régions finies / monde streamé)

### Phase 13 — Horizon lointain
- vue jusqu'à l'horizon réel (300 km et plus depuis un sommet)
- core : fait (voir « Réalisé »)
- modifications des joueurs visibles de loin : toutes si possible, sinon les grosses
  (format du résumé à définir avec la phase 15)
- Unreal : rendu en clipmap (une texture par niveau, envoi des seules bandes changées,
  géomorphing), époques sur les tâches d'Unreal moins prioritaires que les tuiles, jupe
  sous le bord du niveau 0, tuiles arrêtées vers 850 m, courbure, atmosphère et mer
- plus tard si besoin : préfiltrage des crêtes lointaines (silhouette aujourd'hui au plus
  0,2° trop basse)

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
  jamais la hauteur
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
  - climat et biome → matériaux locaux et cultures agricoles (bois, torchis, chaume en
    plaine ; pierre et ardoise en montagne ; argile et pierre au désert ; pilotis au marais),
    lus sur le climat du niveau Planète, pas sur le biome au point
  - culture → style architectural, défenses, noms ; territoires propres qui peuvent
    traverser plusieurs biomes et s'y adapter, transitions progressives aux frontières
  - site → fonction (port, ville minière, ville caravanière, ville fluviale, oasis, place
    forte de col)
- le core décide du plan (sites, routes, parcelles, emprise, hauteur, type, style, matériaux),
  Unreal assemble les bâtiments depuis des kits modulaires (PCG) : aucune géométrie de
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
- à décider avant de commencer : densité du peuplement (monde surtout sauvage avec de rares
  royaumes, ou peuplé partout), époque et styles, kits d'assets ou bâtiments générés,
  intérieurs, bâtiments destructibles (phase 15), villes vues depuis l'horizon lointain
  (silhouettes, lumières la nuit)

### Phase 15 — Jeu solo
- modes : exploration / contemplation, survie, créatif réservé aux administrateurs
- nouveau monde : nom, seed, aperçu de la carte et choix du point d'apparition
- sauvegarde et chargement
- destruction de bloc en jeu
- modifications en différences, sous une forme qui tient les grosses destructions
  (à essayer et mesurer ; si ça ne convient pas, on cherche autre chose) :
  - chaque chunk découpé en cubes de 16 × 16 × 16 blocs : inchangé, entièrement vidé,
    entièrement rempli, ou masque compressé
  - opérations volumiques (sphère, boîte, explosion) : quelques octets, appliquées de façon
    déterministe par chaque machine puis enregistrées dans les masques
  - critères : un trou de 10 m ≈ 1 Ko, une montagne rasée (1 km × 1 km × 500 m) ≤ ~1 Mo,
    rechargement d'une tuile modifiée sans coût visible ; comparaison avec des chunks
    complets compressés (méthode Minecraft)
  - résumé des modifications pour l'horizon lointain (phase 13) tiré des masques

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
