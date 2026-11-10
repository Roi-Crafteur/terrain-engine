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

## Goldens recollés
- Un golden est une somme `0x…` figée dans un test (imprimée par `golden::report`, prête à
  recoller). On ne le recolle que si le monde change exprès ; sinon on corrige le code.
- Un golden recollé porte dans son commentaire, à côté de sa valeur :
  - le commit qui le recolle (son titre, puisque le hash n'existe qu'après ; ou le hash s'il est
    déjà poussé) ;
  - les parties changées (leurs noms dans le test) ;
  - la cause : ce qui a changé dans le monde, et pourquoi.
- Exemple :
  `// Recollé par « Ergs: seamless ripples and tile sides » (agent 34) : "world tiles LOD 0" à`
  `// "LOD 3" ; les normales des tuiles lisent l'anneau de colonnes autour d'elles (tile_sides).`
- La PR liste les goldens recollés avec leur cause. Si le monde généré change,
  `kGeneratorVersion` augmente aussi (`terrain_core/include/terrain/storage/world_meta.hpp`).
