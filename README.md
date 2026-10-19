# Terrain Engine

Moteur de terrain procédural 3D continu conçu pour générer des mondes immenses :
continents, océans, chaînes de montagnes, vallées, rivières, cascades, falaises,
plateaux, grottes et biomes cohérents.

Contrairement à Minecraft, le terrain n’est pas voxel : il est généré à partir
d’un champ de densité 3D (SDF), puis converti en mesh continu avec Marching Cubes,
Surface Nets ou Dual Contouring.

## Objectifs

- Terrain 3D lisse et continu
- Chunks de 16 × 16 m
- Montagnes de plusieurs milliers de mètres
- Grandes vallées et rivières cohérentes
- Falaises, plateaux, surplombs et cascades
- Biomes basés sur température, humidité, altitude et pente
- Monde immense avec streaming et LOD
- Génération déterministe à partir d’une seed

## Architecture

- `terrain_core/` : génération, chunks, biomes, meshing, sauvegarde
- `terrain_viewer/` : rendu OpenGL, caméra, fenêtre
- `tests/` : tests unitaires

`terrain_core` ne dépend jamais d’OpenGL, Vulkan, DirectX ou d’une fenêtre.
