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

# Workflow obligatoire

Pour chaque tâche :

1. Lire CLAUDE.md, docs/ARCHITECTURE.md, docs/ROADMAP.md et docs/AI_RULES.md.
2. Proposer un plan court avant toute modification.
3. Lister les fichiers à créer ou modifier.
4. Lister les tests à ajouter ou mettre à jour.
5. Attendre une validation explicite de l’utilisateur.
6. Implémenter uniquement le plan validé.
7. Lancer cmake configure, cmake build et ctest.
8. Ne jamais modifier un module non concerné.
9. Ne jamais introduire de dépendance graphique dans terrain_core.
10. Si un test échoue, corriger avant de proposer un commit.

# Modèle et responsabilités

- Opus : architecture, plans, décisions complexes, débogage difficile.
- Sonnet : implémentation, tests, refactors simples, fonctionnalités définies.
- Haiku : formatage, commentaires, petites corrections, tâches répétitives.
