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

## À venir

### Phase 10 — suite
- Character qui marche, saute et tombe sur le terrain (pack Third Person)
- type de mesh choisi par mesure : Dynamic Mesh, ProceduralMeshComponent ou
  RealtimeMeshComponent
- compilation du plugin sous Linux et Mac (risque `std::jthread`)
- ramener le joueur d'un monde entier au-delà d'un tour (coordonnées Unreal ≈ 88 000 km)

### Chantier parallèle — Performance du bruit
- retrouver le coût d'avant la phase 9 (heightAt ≈ 2,5 fois plus lent depuis les maths
  déterministes), sans changer un seul bit du monde

### Phase 11 — Hydrographie continentale (approche TerraForged)
- niveau Planète précalculé à la création du monde (attente longue acceptée)
- climat continental : les fleuves en dépendent (pas de source dans un désert,
  des fleuves qui peuvent traverser un désert)
- graphe des grands fleuves par continent, taille réaliste jusqu'à l'Amazone
- deltas, estuaires, grands lacs, méandres, bras morts
- canyons et gorges fluviales, grandes chutes aux escarpements (type Niagara)
- sorties : splines (Water d'Unreal) et contraintes de hauteur
- 11A : niveau Planète, climat et graphe des fleuves, calculés et exportés sans toucher
  au relief ; 11B : creusement des lits, lacs, canyons et chutes dans le relief
- liaison avec les rivières souterraines (pertes, résurgences) : plus tard

### Phase 12 — Monde streamé unifié
- régions et chunks générés à la volée, mis en cache, contraints par le niveau Planète
- érosion hydraulique, glaciaire (vallées en U, cirques, fjords), thermique (éboulis)
  et côtière (falaises marines, arches, stacks)
- reliefs emblématiques : chaînes alpines / himalayennes, plateaux et mesas,
  karst et pitons, volcans
- falaises, surplombs et arches dans le monde infini
- fin des deux mondes séparés (régions finies / monde streamé)

### Phase 13 — Horizon lointain
- vue jusqu'à l'horizon réel (300 km et plus depuis un sommet)
- core : grilles de hauteur et de matériau à résolution libre
- modifications des joueurs visibles de loin : toutes si possible, sinon les grosses
- Unreal : rendu en clipmap, courbure simulée, atmosphère

### Phase 14 — Rendu Unreal
- style photoréaliste avec un étalonnage stylisé (esprit shaders Minecraft),
  ambiance chaude ou froide selon le lieu et la météo
- matériaux depuis les identifiants de matériau du core
- Water branché sur les splines de rivière
- végétation PCG depuis les positions du core
- jour / nuit, météo régionale, saisons, aurores, éclipses et phénomènes rares
- PC large : un mode de rendu sans Lumen ni Nanite pour les anciennes machines

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
