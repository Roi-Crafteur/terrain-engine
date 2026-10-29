# Peuplement — questions à revoir

Questions ouvertes de `docs/PEUPLEMENT.md`, à traiter avec l'utilisateur. Chaque réponse est
reportée dans `PEUPLEMENT.md`, puis la question est retirée d'ici.

Déjà répondu : la liste des peuples (V1 et phase 2), la différence entre peuple et profession,
un seul champ de terrain, des préférences physiques à la place des biomes, `physicalPreferences`
à la place de `terrainWeights`, les modèles de terrain nommés, le score en triangle avec
plancher souple, le score physique, la formule de raceSuitability, `kingdomShare` à la place
d'un nombre fixe de royaumes, le champ `underground` et les peuples souterrains de la V1, les relations asymétriques, failles et volcans en données seules d'abord, les plaques tectoniques existantes comme source des
failles et des volcans, les lignes telluriques indépendantes des plaques, la densité rare des
lignes, des nœuds et des cratères, les trois niveaux de Mana connu, les monstres à territoires, tous les poids dans un fichier de
configuration du monde, le score en trapèze (plage idéale, rive à 1),
`ideal` remplacé par `idealMin` et `idealMax`, le Prana compté deux fois avec des scores
normalisés entre 0 et 1, la normalisation automatique des parts de royaumes, les arrondis avec un
minimum d'un royaume, deux parts par peuple (grands royaumes et petits États), le nombre de
royaumes tiré depuis la seed dans la plage du preset, les kobolds en V1 (13 peuples), les gnomes
`partial`, `depth` renommé `preferredDepth` (profondeur préférée), le tableau des profondeurs,
la formule de l'habitabilité du sous-sol, une relation absente vaut 0,
`selfRelation` (+1 par défaut), la guerre possible entre royaumes du même peuple, le nombre de volcans et de
cratères, les densités magiques sur toute la planète (Mana utilisable filtré par l'accès), la
part cachée de chaque élément, le nombre de zones mortes, la seed secrète
gardée par le serveur, la formule du Mana (petit bonus des failles et des volcans), les monstres avec
les préférences physiques et la distance aux volcans, la densité des monstres selon le preset, les chevauchements avec prédation, le rayon des territoires
en km, le coût d'expansion (modèle de terrain + pénalités physiques) et les coûts de traversée
des fleuves, un monde qui garde sa configuration (seeds, preset, version, état de génération), deux niveaux
de limites (`hardMin`/`hardMax` à 0, `min`/`max` au plancher), `habitabilityMax` = maximum théorique, le minimum d'un État par peuple
compté sur le total, un nombre de royaumes forcé possible (`mode` `range` ou `exact`), les goûts
souterrains en données (`undergroundPreferences`), deux valeurs entre royaumes (`relation` et
`tension`), la densité de base des nids (`nestDensity`), la seconde passe des nids
après le peuplement, la force et l'espacement des nids (`power`, `minNestSpacing`,
`dominationThreshold`), les coûts de déplacement par espèce (`movementCosts`), la chaleur locale
(`localHeat`), les seuils de débit des cours d'eau, le gué probable, les ponts après les
routes, le bonus de confluent limité, la copie complète de
la configuration avec une empreinte, les étapes ajoutées plus tard (couche manquante ajoutée,
couche utilisée intouchable sauf migration), un dirigeant initial par royaume, les alliances (forte baisse de tension, légère
hausse de relation), pas de religions en V1 (cinq types de rivalités à la place), les espèces
concurrentes (`competitors`, distance minimale = max des deux espacements), le modèle
volcanique, `"forbidden"`, la même règle de coût pour les monstres que pour les royaumes, les côtes
détaillées sur un monde déjà joué (migration explicite avec sauvegarde), l'histoire courte V1 sur un seul règne
(60 ans par défaut), les proies (`prey`, catégories communes).

## Scores des préférences


## Peuples



## Géologie et magie

Les questions 9 et 10 sont **hors V1** : elles ne bloquent pas le lancement du peuplement.

**9. Rifts et sanctuaires (à reprendre de zéro).** Malentendu probable : l'utilisateur les voit
autrement que ce qui est écrit dans `PEUPLEMENT.md`. Il faut d'abord qu'il explique comment ils
fonctionnent dans sa tête. Attention aussi au mot : « rift » désigne aussi un fossé
géologique entre deux plaques (déjà généré), sans lien avec un rift magique.
Ensuite seulement : combien, où, et quel lien avec les plans céleste et infernal.

**10. Corruption (à reprendre de zéro, avec la 9).** Même malentendu probable que pour les
rifts et les sanctuaires : l'utilisateur expliquera d'abord comment il voit la corruption.
Ensuite seulement : d'où elle vient, où elle se trouve, et ses liens avec le Prana, les
morts-vivants et l'Entropie.

## Danger




## Réglages

