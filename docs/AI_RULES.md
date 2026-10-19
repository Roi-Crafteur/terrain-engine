# Règles du projet

## Objectif
Créer un moteur de terrain procédural 3D continu, inspiré de Minecraft
mais sans blocs : terrain lisse généré par SDF et meshing.

## Architecture obligatoire
- terrain_core : génération, chunks, biomes, meshing, sauvegarde.
- terrain_viewer : rendu OpenGL, caméra, fenêtre.
- terrain_core ne doit JAMAIS dépendre d’OpenGL, Vulkan, DirectX ou d’une fenêtre.
- Le viewer dépend de terrain_core, jamais l’inverse.

## Règles de génération
- Toute génération est déterministe : même seed + mêmes coordonnées = même résultat.
- Le relief global utilise une heightmap multi-échelle.
- Les falaises, grottes, surplombs et arches utilisent un SDF 3D.
- Le bruit ridged ne doit jamais être appliqué sans masque de massif.
- Les biomes ne modifient jamais la hauteur du terrain.
- Les rivières sont générées après le relief.
- Les cascades apparaissent uniquement aux ruptures de pente des rivières.

## Qualité
- Chaque module doit avoir des tests unitaires.
- Aucun fichier ne dépasse 500 lignes.
- Aucune fonction ne dépasse 80 lignes.
- Aucune dépendance graphique dans terrain_core.
- Chaque tâche doit compiler et passer les tests.