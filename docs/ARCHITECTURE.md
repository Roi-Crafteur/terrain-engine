structure :
  terrain-engine/
    terrain_core/       ← génération pure, sans rendu
      generation/       ← relief, érosion, rivières, falaises, grottes, biomes
        planet/         ← niveau Planète : grille de 8 km, climat, lacs, fleuves
        water/          ← l'eau qui suit le relief : lits, vallées, chutes, lacs fins
        settlement/     ← peuplement (docs/PEUPLEMENT.md) : cartes d'analyse, énergies,
                          peuples, presets, capitales, expansion, frontières, dirigeants
      json/             ← lecture stricte et écriture canonique des fichiers de données
      meshing/          ← Marching Cubes
      streaming/        ← tuiles, LOD, coutures, horizon lointain, génération asynchrone
      storage/          ← modifications des joueurs, sauvegarde, planet.bin, settlement.json
      io/               ← exports PNG de contrôle
      tools/            ← exécutables d'export
    data/
      settlement/       ← world.json, resources/*.json, peoples/*.json (modifiables sans code)
    terrain_viewer/     ← mini-fenêtre OpenGL de debug
    terrain_unreal/     ← plugin UE5 (à partir de la phase 10)
      Source/TerrainEngine/Private/Bridge/      ← pont en C++ pur, testé par ctest
      Source/TerrainEngine/Private/TerrainCore/ ← un enrobage par .cpp du core (hors io/)
      Scripts/          ← scripts Python qui créent les matériaux
    tests/              ← tests unitaires et golden
    docs/
      ARCHITECTURE.md
      ROADMAP.md
      AI_RULES.md
      PEUPLEMENT.md     ← conception du peuplement ; section 9 : la V1 codée
    CMakeLists.txt
    README.md

dépendances :
  terrain_viewer → terrain_core
  terrain_unreal → terrain_core
  terrain_core ne dépend de rien de graphique, ni d'Unreal.

partage des rôles :
  terrain_core (déterministe, identique serveur et clients) :
    - forme exacte du monde : relief, falaises, grottes, meshes des tuiles
    - tracés des fleuves et lacs (splines, niveaux, lits)
    - identifiants de biome et de matériau
    - positions, types et tailles des objets (végétation, spéléothèmes, flore)
    - grilles de hauteur pour l'horizon lointain
    - modifications des joueurs et format de monde
    - état initial du peuplement : cartes d'analyse, peuples, royaumes et frontières,
      dirigeants et relations, avec la copie de sa configuration (settlement.json)
  Unreal (apparence et jeu) :
    - matériaux, displacement, rochers Nanite
    - eau (plugin Water depuis les splines)
    - végétation (PCG depuis les positions du core)
    - ciel, météo, lumière (Lumen)
    - rendu de l'horizon lointain (clipmap)
    - joueur, collisions, interface, réseau, effets de destruction
