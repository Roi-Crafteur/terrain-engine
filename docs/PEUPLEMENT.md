# Peuplement, énergies et royaumes — document de conception

Statut : en cours d'affinage avec l'utilisateur (octobre 2026). Rien n'est encore codé. Les
points marqués **À décider** attendent une réponse ; tout le reste est validé.

Règles qui s'appliquent partout :

- déterministe : même seed + même preset de monde = même monde, sur toutes les machines ;
- tout ce document produit des données dérivées, rangées à part : la somme du niveau Planète
  ne change pas, et aucune étape ne change la forme du monde avant les villes et les routes
  (étape 6, modifications locales du relief) ;
- rien ne dépend du biome au point : on lit le climat et le relief du niveau Planète ;
- les peuples, les poids et les formules sont des fichiers de données modifiables, jamais des
  règles codées en dur : ajouter un peuple = ajouter un fichier.

## 1. Pipeline

1. **Génération physique** : elevation, temperature, rainfall, river, biome, coast basique (fait,
   sauf le type de côte).
2. **Géologie en données** : failles, anciens volcans, cratères (données seules, sans relief :
   le relief n'en est pas modifié). Validé en deux phases :
   - **phase 1 (avec le peuplement)** : failles = lignes ou zones sur la carte, volcans et
     cratères = points rares. Leur seul effet est de nourrir le Mana, l'Aura et le danger ;
   - **phase 2 (plus tard)** : un agent volcanique qui change la forme du monde, avec les
     caldeiras, les coulées et les îles volcaniques. Il passe dans la file des agents qui
     changent le monde, après l'agent 34.

   **Validé, nombres sur la planète (2 101 millions de km²)** :

   | Élément | Nombre |
   |---|---|
   | Volcans en tout | 3 000 à 6 000 |
   | dont volcans actifs | 300 à 800 |
   | dont grands volcans | 50 à 150 |
   | Cratères arcaniques | 30 à 80 |
   | dont cratères majeurs | 5 à 15 |

   Les volcans sont surtout des sources d'Aura, de chaleur et de Mana local. Les cratères sont
   rares, plus magiques et plus dangereux. Le générateur dit quels volcans sont actifs ; les
   éruptions en cours de partie, s'il y en a, relèvent du jeu.

   **Validé : on part des plaques tectoniques qui existent déjà.** La géographie (phase 11R)
   génère 48 à 72 plaques en Voronoi, avec leurs mouvements et trois types de frontières :
   - collision : chaînes de montagnes, hauts plateaux, fosses et arcs insulaires en mer ;
   - divergence : dorsales en mer, rifts continentaux ;
   - transformante : coulissement.

   Les continents viennent de noyaux elliptiques, pas des plaques. La géologie en données lit
   ces frontières, comme sur Terre :
   - **failles** : les frontières des plaques (les transformantes sont de grandes failles de
     coulissement), les bords des rifts ;
   - **volcans** : en arc derrière les fosses de subduction, côté plaque qui passe dessus ; le
     long des rifts et des dorsales qui touchent la terre ; quelques points chauds isolés,
     tirés de la seed ;
   - **cratères** : n'importe où, très rares ;
   Les **lignes telluriques ne suivent pas les plaques** (décision de l'utilisateur) : elles
   traversent le monde librement, pour ne pas concentrer la magie sur les terrains escarpés
   ni avantager les peuples des montagnes.

   La phase 2 doit reprendre exactement les positions de la phase 1, avec la même seed, pour
   que le Mana, les peuples et les royaumes déjà placés restent cohérents. Le moteur n'a encore
   aucun volcan ni cratère géologique : les « cratères » du code sont ceux que creusent les
   joueurs.
3. **Réseau magique** : lignes telluriques, nœuds, rifts, sanctuaires, zones mortes, corruption.
4. **Énergies ambiantes** : Prana, Aura, Mana, puis magicInstability et dangerMagique.
5. **Cartes d'analyse** : slope (rugosité dans la case de 8 km), waterDistance, arableLand,
   resources (par la géologie ; minerais rares sur la seed secrète), danger naturel,
   habitability.
6. **Peuples** : fichiers JSON, raceSuitability[peuple], graines et capitales potentielles.
7. **Expansion et frontières naturelles** : expansion pondérée par un coût (terrain, pente,
   fleuve, désert) ; les frontières en émergent, jamais tracées après coup ; nettoyage des
   seuls artefacts (les enclaves réalistes restent : îles, vallées isolées, peuples réfugiés).
8. **Guerres et alliances** : gouvernements et relations, puis une histoire courte et
   déterministe en un nombre fixe de tours (déplacement de frontières, zones disputées,
   ruines et forts, monde politique final).
9. **Villes, villages et routes** : capitales confirmées, villes du monde d'après-guerre,
   routes selon les frontières et le relief, ports et réseaux commerciaux.
10. **Plans parallèles** : plan céleste, plan infernal, portails selon le Mana, les rifts, les
    sanctuaires et les royaumes. Bien plus tard : chaque plan est un monde à part.

### Calendrier

| Quand | Quoi |
|---|---|
| Maintenant (aucune forme du monde changée) | étapes 2 à 5, format JSON des peuples, raceSuitability de 3 ou 4 peuples d'exemple |
| Après l'agent 31 (fleuves) | peuples, graines et capitales, expansion, frontières, guerres et alliances |
| Après l'agent 33 (côtes) | ports, villes côtières, routes maritimes, royaumes maritimes |
| Après l'agent 34 (dunes) | ajustement des déserts, oasis, routes caravanières, peuples du désert |

## 2. Deux passes pour les sites

- **Passe Planète (case de 8 km)** : habitabilité, raceSuitability, densité de peuplement,
  puis la décision « un village existe dans cette zone ».
  Exemple : « vallée fertile près d'une rivière, bonne zone pour un village humain ».
- **Passe région / site (chunks de 16 m)** : relief détaillé et micro-relief, rivières
  précises, gués, confluents, anses, pentes constructibles, puis le choix du site exact.
  Exemple : « le village s'installe sur l'éperon au-dessus du gué, à 300 m du confluent ».

Une case Planète fait 512 × 512 chunks. Le site exact se calcule à la volée dans les régions
streamées, de façon déterministe.

## 3. Presets de monde

L'utilisateur choisit le preset au moment de créer le monde. Il est enregistré avec le monde.
Par défaut : **monde sauvage**.

| Preset | Part civilisée | Part sauvage | Grands royaumes | Petits États |
|---|---|---|---|---|
| Monde sauvage | 10 à 20 % | 80 à 90 % | 10 à 20 | 50 à 150 |
| Monde équilibré | 25 à 40 % | 60 à 75 % | 30 à 60 | 200 à 500 |
| Monde civilisé | 50 à 70 % | 30 à 50 % | 80 à 150 | 500 à 1 000 |

**Validé** : le nombre exact est tiré depuis la seed, dans la plage du preset. Même seed =
même nombre :

```json
{ "worldPreset": { "majorKingdoms": { "min": 10, "max": 20 },
                   "minorStates":   { "min": 50, "max": 150 } } }
```

`totalMajorKingdoms = tirage(seed + "kingdoms", min, max)` et
`totalMinorStates = tirage(seed + "minorStates", min, max)`, avec le générateur déterministe
du projet (jamais `rand()` ni le hasard de la plateforme).

**Validé, valeur forcée** : la création du monde peut aussi imposer une valeur exacte.

```json
{ "majorKingdoms": { "mode": "range", "min": 10, "max": 20 } }
{ "majorKingdoms": { "mode": "exact", "value": 12 } }
```

Le nombre réellement utilisé est enregistré avec le monde :
`{ "resolvedMajorKingdoms": 12, "resolvedMinorStates": 85 }`.

Pour l'échelle : le monde par défaut fait 60 031 × 34 996 km, environ 2,1 milliards de km²,
dont environ 37 % de terres (777 millions de km², 5 fois les terres de la Terre).

## 4. Énergies

Six énergies, dont trois géographiques cartographiées :

| Énergie | Nature | Cartographiée |
|---|---|---|
| ❤️ Prana | énergie vitale ambiante | oui |
| 🟠 Aura | énergie de renforcement du milieu | oui |
| 🔵 Mana | énergie magique ambiante | oui (carte centrale) |
| 🟣 Anima | puissance de l'âme : liens, serments, contrats, familiers | non, personnelle |
| 🟡 Numen | énergie divine, symbiose avec une divinité | non, personnelle |
| ⚫ Entropie | énergie démoniaque, pacte avec un démon | non, personnelle |

Un lieu peut renforcer ou gêner une énergie personnelle (ruine ancienne, arbre sacré, zone
morte, temple, faille infernale), mais il ne la crée pas.

### Prana

`prana = végétation + eau douce + biodiversité + climat tempéré + soleil − aridité − froid
extrême − corruption − mort`

| Milieu | Prana |
|---|---|
| Forêt ancienne | très élevé |
| Vallée fluviale | élevé |
| Prairie fertile | moyen à élevé |
| Littoral tempéré | moyen |
| Montagne | faible à moyen |
| Désert, glace | très faible |
| Terre corrompue | négatif |

Effets :

- régénération plus rapide, faune et flore vigoureuses, agriculture plus productive ;
- habitabilité accrue (`habitability += prana × 0,3`) ;
- attire les elfes, les druides et les peuples de la nature ;
- repousse les morts-vivants et les créatures corrompues.

À l'échelle Planète, la végétation se lit sur le climat (température, pluie), pas sur la
végétation au point.

### Aura

`aura = altitude + vents + océans + volcans + falaises + terrains hostiles + Prana modéré`

| Milieu | Aura |
|---|---|
| Volcan | très élevé |
| Montagne, littoral venteux, océan | élevé |
| Désert, toundra | moyen |
| Forêt dense | faible à moyen |
| Plaine calme | faible |

Effets :

- récupération physique, progression de l'Aura, créatures plus robustes ;
- lieux d'entraînement et de rites martiaux ;
- attire les nains, les orcs, les géants et les hommes-bêtes ;
- rend habitables certains milieux hostiles (`raceSuitability += aura × auraPreference`).

### Mana

**Validé** : le Mana vient surtout des lignes telluriques et des nœuds. Les failles et les
volcans donnent surtout de l'Aura, de la chaleur et du danger, et seulement un petit bonus de
Mana. Une montagne volcanique est intéressante, mais pas automatiquement un haut lieu magique.

`mana = lignes telluriques × 1,0 + nœuds × 1,0 + cratères × 0,8 + failles × 0,2 + volcans × 0,15
− zones mortes` (poids dans le fichier de configuration du monde).

| Milieu | Mana |
|---|---|
| Nœud tellurique, intersection de lignes | très élevé |
| Ligne tellurique | élevé |
| Cratère arcanique | élevé à extrême |
| Volcan ancien | moyen à élevé |
| Faille géologique | moyen |
| Plaine ordinaire | faible |
| Zone morte | nul ou négatif |

Les **lignes telluriques** sont de longues courbes de bruit déterministes, **indépendantes des
plaques, des failles et du relief** (validé) : elles traversent indifféremment les plaines, les
forêts, les déserts, les montagnes et les océans. Leur intensité varie, et leurs intersections
forment les nœuds.

Garde-fou d'équité, vérifié par un test : la part des nœuds et des lignes sur chaque grand type
de terrain (plaine, forêt, désert, montagne…) reste proche de la part de ce terrain dans les
terres. Aucun type de terrain, donc aucun peuple, n'est avantagé par le réseau magique.

Le Mana rend la magie plus puissante. On y trouve des créatures magiques, des plantes
alchimiques, des cristaux, des ruines anciennes et des portails possibles. Il influence
habitability, dangerMagique, raceSuitability, les capitales magiques et les portails.

### Densité : rare et précieux (validé)

Si tout le monde a du Mana partout, la magie perd son intérêt géographique.

| Élément | Densité | Sur la planète (2,1 milliards de km²) | Sur les terres (777 millions de km²) |
|---|---|---|---|
| Grandes lignes telluriques | 10 à 30, longues | 10 à 30 | — |
| Nœuds magiques | 1 tous les 2 à 5 millions de km² | environ 420 à 1 050 | environ 155 à 390 |
| Cratères | 1 tous les 10 à 30 millions de km² | environ 70 à 210 | environ 26 à 78 |
| Zones mortes | 1 tous les 20 à 50 millions de km² | environ 42 à 105 | environ 16 à 39 |

Les nœuds naissent surtout aux intersections des lignes : ce sont les lieux de pouvoir que
cherchent les mages et les peuples magiques. 30 grandes lignes se croisent au plus environ
435 fois ; les nœuds secondaires des petites lignes complètent le compte.

Les densités comptent **toute la planète**, océans compris. `manaGlobal` couvre toute la
planète ; `manaUtilisable` est `manaGlobal` filtré par l'accessibilité (terres, côtes, zones
accessibles). C'est lui que lisent l'habitabilité et les peuples. Les nœuds sous l'océan
serviront aux tritons et aux sirènes de la phase 2.

### Trois niveaux de Mana connu (validé)

1. **Mana visible, connu de tous** : les lignes majeures et les grands nœuds.
2. **Mana partiellement connu** : les petites lignes et les nœuds secondaires.
3. **Mana caché** : tiré de la seed secrète ; on le découvre par l'exploration, les rituels ou
   les artefacts.

Deux mondes de même seed ont exactement les mêmes secrets, mais les joueurs doivent les
découvrir.

Part cachée de chaque élément (validé) :

| Élément | Part cachée |
|---|---|
| Lignes telluriques majeures | 10 à 20 % |
| Nœuds magiques | 20 à 30 % |
| Cratères arcaniques | 30 à 50 % |
| Zones mortes | 40 à 60 % |

**Attention, la seed secrète ne doit pas se déduire de la seed du monde.** Avec
`magicSeed = hash(worldSeed + "hidden_magic")`, tout joueur qui connaît la seed du monde
recalculerait les secrets. Or chaque client connaît la seed du monde pour générer le terrain.
Comme le prévoit déjà la ROADMAP, la seed secrète est tirée au hasard à la création du monde,
gardée par le serveur et jamais envoyée aux clients (**validé**).

- Le client peut connaître : la forme générale des lignes, les zones à fort Mana, les nœuds
  découverts.
- Il ne doit pas pouvoir recalculer : les nœuds cachés, les rifts non découverts, les zones
  mortes, les secrets magiques.
- Conséquence : la carte de Mana publique est calculée **sans** les éléments cachés. Sinon, une
  zone de fort Mana trahirait un nœud caché. Le serveur ajoute la part cachée de son côté. Les
  effets de jeu qui en dépendent (puissance d'un sort près d'un nœud caché) sont décidés par le
  serveur.
- La seed publique, la seed secrète et l'état généré sont enregistrés avec le monde, pour la
  reproductibilité et la persistance. Les mêmes secrets reviennent si l'on
recrée un monde avec les deux seeds. Le générateur calcule la couche cachée **sur le serveur
seulement** ; le serveur envoie à un client uniquement ce qu'il a découvert.

### Instabilité et danger magique

Un Mana élevé n'est pas forcément dangereux : c'est un Mana **instable** qui l'est.

- `magicInstability = variationLocale(mana) + anomalies + cratères + rifts + conflits
  Numen / Entropie`
- `dangerMagique = magicInstability × 2 + rifts + anomalies + cratères − sanctuaires`

| Situation | Danger |
|---|---|
| Mana faible et stable | très faible |
| Mana élevé et stable | faible à moyen |
| Sanctuaire céleste stable | faible pour les justes |
| Mana instable | élevé |
| Nœud magique instable | très élevé |
| Rift infernal, faille démoniaque | extrême |

Les rifts, les failles infernales et les sanctuaires sont placés à l'étape 3, avant le danger et
le Prana qui les utilisent. Les plans parallèles s'y accrochent plus tard.

Tous les poids des formules (0,3 ; × 2…) sont dans un fichier de configuration du monde.

## 4 bis. Monstres et danger (validé)

Le danger est une carte, mais les monstres ont de vrais territoires, qui sont prioritaires.

`danger = dangerNaturel + dangerMagique + dangerMonstres − protectionCivilisation`

Un monstre est un fichier de données (`"type": "monster"`), comme un peuple :

```json
{ "id": "dragon_rouge", "type": "monster",
  "territory": { "preferredBiomes": ["volcano", "mountain"], "nest": true,
                 "radius": 300, "aggression": 0.9 } }
{ "id": "loup_geant", "type": "monster",
  "territory": { "preferredBiomes": ["forest", "taiga"], "nest": true,
                 "radius": 80, "aggression": 0.5 } }
```

Un dragon a un territoire énorme, un loup géant un territoire plus petit.

**Validé** : les monstres utilisent le même système que les peuples (`physicalPreferences` en
trapèze, `baseTerrainModel`), et non `preferredBiomes`. « Volcan » devient une distance aux
volcans de la géologie en données (`volcanoDistance`, en km), jamais un biome :

```json
{ "id": "dragon_rouge",
  "physicalPreferences": {
    "altitude":        { "min": 500, "idealMin": 1500, "idealMax": 3000, "max": 6000, "floor": 0.02 },
    "localHeat":       { "min": 10,  "idealMin": 35,   "idealMax": 70,   "max": 120,  "floor": 0.02 },
    "volcanoDistance": { "min": 0,   "idealMin": 0,    "idealMax": 200,  "max": 1000, "floor": 0.02 } } }
```

- **Territoires** : nid, zone de chasse, zone de reproduction, zone d'influence.
- **Danger local** : proximité du nid, densité du territoire, agressivité.
- **Effets sur le peuplement** :
  - repousse les villages ;
  - attire les chasseurs, les aventuriers et les ordres militaires ;
  - crée des forts ;
  - bloque certaines routes.

**Validé, chaleur locale** : la température du climat est une moyenne annuelle, qui dépasse
rarement 30 °C. Une carte de chaleur locale s'y ajoute :
`localHeat = température climatique + chaleur des volcans proches + chaleur des failles +
chaleur géothermique`.
Un dragon rouge l'utilise comme critère :

```json
"localHeat": { "min": 10, "idealMin": 35, "idealMax": 70, "max": 120, "floor": 0.02 }
```

Elle sert aussi aux kobolds, aux salamandres, aux élémentaires de feu et aux créatures
volcaniques. La distance aux volcans reste un critère utile, mais la chaleur locale est plus
expressive.

**Validé, densité selon le preset du monde** :

| Preset | Densité des monstres | Multiplicateur |
|---|---|---|
| Monde sauvage | très élevée | 1,0 |
| Monde équilibré | moyenne | 0,6 |
| Monde civilisé | faible, concentrée dans les zones sauvages | 0,3 |

```json
{ "worldPreset": { "monsterDensity": { "wild": 1.0, "balanced": 0.6, "civilized": 0.3 } } }
```

**Validé, rayon des territoires en km** :

| Monstre | Rayon de territoire |
|---|---|
| Loup géant | 50 à 150 km |
| Troll | 100 à 300 km |
| Harpie | 100 à 400 km |
| Dragon mineur | 200 à 600 km |
| Dragon majeur | 500 à 1 500 km |
| Léviathan (en mer) | 1 000 à 5 000 km |

Le rayon est une zone d'influence, pas un cercle parfait : le terrain la déforme. Le
territoire s'étend depuis le nid avec un coût de terrain propre à l'espèce, avec le même
mécanisme que l'expansion des royaumes. Un loup est arrêté par une haute chaîne, alors qu'un
dragon la survole.

**Validé, coûts de déplacement par espèce** (`movementCosts`), même format que les royaumes :

```json
{ "id": "dragon_rouge", "movementCosts": { "plains": 2, "forest": 4, "hills": 2,
                                           "mountain": 1, "volcano": 1, "ocean": 20 } }
{ "id": "loup_geant",   "movementCosts": { "plains": 2, "forest": 1, "hills": 2,
                                           "mountain": 8, "volcano": 30, "ocean": 999 } }
```

Le dragon survole les montagnes, le loup y est ralenti, le léviathan reste en mer.

**Validé, détails des coûts** :
- **modèle volcanique** : le volcan n'est pas un biome, c'est un point ou une zone. Le modèle
  `volcanic` couvre les cases à moins de 50 km d'un volcan actif ou d'un grand volcan. Il
  augmente aussi la chaleur locale, le Mana, l'Aura et le danger ;
- **`"forbidden"`** au lieu de 999 (`"ocean": "forbidden"`) : une case interdite est exclue
  de l'expansion, au lieu d'être traitée comme un coût très élevé. Un très long chemin ne
  traverse donc jamais l'océan par erreur ;
- **même règle que les royaumes** : `coût = coût du modèle de terrain le plus proche +
  pénalités d'altitude, de rugosité, d'aridité, de fleuve et de danger − préférence physique`.
  Chaque espèce a son propre tableau de coûts et ses propres préférences.

Exemples :
- dragon rouge : montagne facile, volcan très facile, océan coûteux ou interdit ;
- loup géant : forêt facile, montagne difficile, volcan interdit ou très coûteux ;
- léviathan : mer facile, terre interdite.

Deux conventions pour l'agent :
- les coûts sont rangés **dans le fichier de chaque espèce** (`movementCosts`), y compris
  pour `volcanic` : ajouter un monstre ne touche aucun autre fichier ;
- un coût reste toujours **positif** (au moins 0,1, même avec une forte préférence
  physique). Un coût nul ou négatif casserait le calcul du plus court chemin.

**Validé, chevauchements et prédation** : les territoires peuvent se chevaucher, avec des règles
de prédation. Un dragon peut chasser les loups de son territoire. Quand deux territoires se
chevauchent :
- si la force du premier dépasse celle du second multipliée par un seuil, le second est
  repoussé ou réduit ;
- sinon, ils coexistent sous tension.

Les nids et repaires principaux restent espacés. Pour rester déterministe, le générateur place
les espèces de la plus forte à la plus faible.

**Validé, force et espacement** :

```json
{ "id": "dragon_rouge", "power": 95, "minNestSpacing": 1200, "dominationThreshold": 1.5 }
{ "id": "loup_geant",   "power": 20, "minNestSpacing": 120,  "dominationThreshold": 1.5 }
```

- `power` : force, de 0 à 100 ;
- `dominationThreshold` : 1,5 par défaut pour tous, modifiable par espèce ;
- `minNestSpacing` (km) : espacement minimal entre deux nids de la même espèce ;
- un espacement minimal entre espèces concurrentes, en plus (voir ci-dessous) ;
- si `powerA > powerB × dominationThreshold`, A domine B, et B est repoussé, réduit ou éliminé.

**Validé, espèces concurrentes.** Deux espèces sont concurrentes si :
- elles visent les mêmes milieux ;
- elles chassent les mêmes proies ;
- l'une liste l'autre dans `competitors` (la concurrence vaut alors dans les deux sens) ;
- ou leurs territoires se chevauchent fortement.

```json
{ "id": "dragon_rouge",   "competitors": [ "wyverne_de_feu", "troll_des_volcans", "manticore" ],
  "minNestSpacing": 1200 }
{ "id": "wyverne_de_feu", "competitors": [ "dragon_rouge" ], "minNestSpacing": 600 }
```

`distanceMinimale = max(minNestSpacing[A], minNestSpacing[B])` : un dragon rouge (1 200 km) et
une wyverne (600 km) gardent au moins 1 200 km entre leurs nids.

**Validé, proies dès la V1** (`prey`), avec une liste de catégories communes à tous les fichiers,
jamais des espèces précises :

`small_herbivores`, `large_herbivores`, `carnivores`, `apex_predators`, `livestock`,
`humanoids`, `humanoid_settlements`, `ships`, `sea_creatures`, `flying_creatures`, `spirits`,
`undead`, `dragons`, `magical_creatures`.

```json
{ "id": "dragon_rouge", "prey": [ "large_herbivores", "livestock", "humanoids", "carnivores" ] }
{ "id": "loup_geant",   "prey": [ "small_herbivores", "large_herbivores", "livestock" ] }
{ "id": "kraken",       "prey": [ "sea_creatures", "ships", "humanoids" ] }
```

Deux prédateurs sont concurrents s'ils partagent au moins une catégorie de proie et un milieu
favorable :
`concurrence = chevauchement des territoires + proie commune + milieu commun + force comparable`.

| Monstre | Proies | Milieu | Concurrent possible |
|---|---|---|---|
| Dragon rouge | grands herbivores, humanoïdes, carnivores | volcan, montagne | wyverne de feu |
| Loup géant | petits et grands herbivores | forêt, taïga | ours géant |
| Kraken | créatures marines, navires | océan profond | léviathan |
| Harpie | humanoïdes, créatures volantes | falaises, montagnes | griffon |
| Mort-vivant | humanoïdes | ruines, cryptes | autre mort-vivant |

En V1, la relation proie-prédateur reste simple : elle sert à la concurrence, à la présence de
gibier et à la pression sur les territoires. Disponibilité des proies, proposée pour l'agent :
- herbivores : dérivés du Prana et du climat ;
- bétail et peuplements humanoïdes : connus seulement après le peuplement, donc utilisés par la
  seconde passe des nids ;
- navires : plus tard, avec les routes maritimes.

La concurrence peut produire :
- un repoussement ;
- une réduction du territoire ;
- un nid abandonné ;
- une guerre territoriale ;
- une coexistence tendue, si les forces sont proches.

**Validé, densité de base par espèce** (`nestDensity`, en nids par million de km² de surface
favorable) : `{ "id": "dragon_rouge", "nestDensity": 0.05 }`,
`{ "id": "loup_geant", "nestDensity": 5.0 }`.

`nidsAttendus = surface favorable (millions de km²) × nestDensity × facteur du preset`

Proposition : la surface favorable est la surface de chaque case pondérée par le score de
l'espèce (une case à 0,5 compte pour moitié), sans seuil arbitraire.

**Validé, seconde passe après le peuplement** :
1. placer les nids selon les milieux favorables ;
2. générer les peuples et les royaumes ;
3. dans un monde civilisé :
   - retirer ou déplacer les nids trop proches des villes ;
   - remplacer certains nids par des ruines, des légendes ou des repaires abandonnés ;
   - garder les nids lointains, difficiles d'accès ou volontairement laissés comme menaces.

Dans un monde sauvage, au contraire, les nids peuvent rester près des zones habitées : ils
expliquent des villages fortifiés ou disparus.

Ordre dans le pipeline : les territoires des monstres sont placés avant les peuples, qui les
évitent ou s'en servent. `protectionCivilisation` n'existe qu'après le placement des villes :
le danger final est recalculé à ce moment-là.

## 4 ter. Terres arables, nourriture et ressources (validé)

### Terres arables

Par case, de 0 à 1 :
`arable = température × humidité × pente × sol + bonusPlaineAlluviale` (plafonné à 1).

| Facteur | Idéal | Nul |
|---|---|---|
| Température moyenne | 10 à 22 °C | sous 0 °C ou au-dessus de 35 °C |
| Humidité / pluie | 500 à 1 500 mm par an | sous 200 mm (désert) |
| Pente | 0 à 5 % | au-delà de 25 % |
| Sol | plaines, vallées, deltas | roche nue, glace, sable |

- Chaque facteur utilise le score en trapèze de la section 6.
- `bonusPlaineAlluviale` = +0,2 le long des rivières larges (seuils de débit de la section 6 bis).
- Tous les chiffres sont dans le fichier de configuration du monde.

### Nourriture

`nourriture = arable × 1,0 + pêche × 0,4 + élevage × 0,4 + chasseCueillette × 0,3`

- pêche : côtes, lacs, grandes rivières ;
- élevage : prairies, collines, steppes ;
- chasse et cueillette : forêts, selon le Prana.

Une région sans champs (steppe, côte rocheuse) peut ainsi nourrir un peuple.

### Ressources : une liste qui grandira

Les ressources sont des **données**, comme les peuples : un fichier par ressource (ou une liste
dans un fichier), avec sa règle de placement. **La liste grandira** (charbon, gemmes, épices,
argile, bois précieux…) sans changer le code : ajouter une ressource = ajouter un fichier.

Pour un monde déjà créé, une nouvelle ressource suit la règle des étapes ajoutées plus tard
(section 6 bis) : couche manquante, donc ajoutée ; les ressources déjà utilisées ne bougent pas
sans migration explicite.

Liste de départ (V1) :

| Ressource | Où (règle) | Cachée ? |
|---|---|---|
| Bois | forêts (couvert végétal) | non |
| Pierre | montagnes, collines, falaises | non |
| Métaux communs (fer, cuivre) | chaînes de collision, vieilles montagnes, failles | non |
| Métaux précieux (or, argent) | rares : arcs volcaniques, grandes failles | en partie |
| Sel | côtes arides, lacs asséchés, bassins fermés | non |
| Cristaux de mana | près des nœuds et des cratères arcaniques | oui (seed secrète) |

- Chaque ressource vaut 0 à 1 par case ; un bruit déterministe forme des **gisements**, pas une
  couche uniforme.
- Usages : tension entre royaumes (ressources convoitées), choix des capitales, expansion.

Exemple de fichier (format à finaliser avec celui des peuples) :

```json
{
  "id": "metaux_communs",
  "hidden": false,
  "sources": [
    { "kind": "plateBoundary", "type": "collision", "weight": 1.0 },
    { "kind": "fault", "weight": 0.5 },
    { "kind": "relief", "type": "oldMountain", "weight": 0.6 }
  ],
  "depositNoise": { "scaleKm": 80, "threshold": 0.6 }
}
```

## 4 quater. Périmètre de la V1 (validé)

**V1** : cartes d'analyse (arable, nourriture, ressources, eau, pente, danger naturel), Prana et
Mana simple, peuples en JSON, raceSuitability, graines et capitales, expansion et frontières,
un dirigeant par royaume, relations de base.

**Plus tard** (écrit dans ce document, pas jeté) : écologie complète des monstres, Mana caché,
histoire courte avec guerres, villes et routes, vie souterraine détaillée, rifts, sanctuaires et
corruption, cultures, plans parallèles.

### Validation de la V1

Cartes à montrer : arable, nourriture, ressources, habitabilité, raceSuitability de trois peuples,
royaumes et frontières.

Tests d'un bon monde :

- chaque peuple a au moins un État ;
- aucun royaume ne dépasse 15 % des terres ;
- aucun peuple ne dépasse 40 % des terres peuplées ;
- au moins 50 % des frontières suivent une rivière, une crête ou une côte ;
- les capitales sont dans les 20 % de cases les plus habitables, près de l'eau douce ;
- déserts profonds, glaces et hautes montagnes presque vides (sauf peuples qui les aiment) ;
- même seed et même preset : résultat identique au bit près ;
- temps de calcul sur toute la planète mesuré et raisonnable.

## 5. Peuples

### Liste (validée)

| V1 (premier monde) | Phase 2 |
|---|---|
| 13 peuples : Humains, Elfes, Nains, Halfelins, Gnomes, Orcs, Gobelins, Hobgobelins, Kobolds, Hommes-bêtes, Dragonides, Géants, Tieflings | Tritons / Sirènes, Hommes-lézards, Fées / Dryades, Aasimars, Morts-vivants, Anges, Démons, Constructes, Aberrations |

Liens de la phase 2 avec le reste :

- Tritons et sirènes : habitabilité marine, après le relief côtier (agent 33).
- Anges, démons et aasimars : liés aux plans céleste et infernal, aux sanctuaires et aux rifts.
- Morts-vivants : liés à la corruption et aux zones mortes.

### Peuple ou profession (validé)

Un peuple (`"type": "people"`) est une population qui s'installe sur la carte. Une profession,
une classe ou une culture (`"type": "profession"`) ne s'installe pas seule : un mage peut être
humain, elfe, nain, gnome ou tiefling. Une profession peut demander un lieu, par exemple
`"requires": { "mana": 0.5 }` pour une école de mages.

```json
{ "id": "mage", "type": "profession", "requires": { "mana": 0.5 } }
```

```json
{ "id": "elfes", "type": "people",
  "ambientEnergyPreferences": { "prana": 0.9, "aura": 0.2, "mana": 0.8 } }
```

### Relations (validé)

Elles sont **asymétriques** : chaque peuple a sa propre opinion de l'autre, de −1 (haine) à
+1 (amitié).

```json
{ "id": "nains", "relations": { "orcs": -0.9 } }
{ "id": "orcs",  "relations": { "nains": -0.4 } }
```

Les nains détestent les orcs, tandis que les orcs les voient surtout comme des rivaux riches en
métal.

- Une relation absente du fichier vaut 0 (neutre).
- La relation d'un peuple avec lui-même vaut +1 par défaut, et se modifie avec `selfRelation`.
  Par exemple, `{ "id": "orcs", "selfRelation": 0.4 }` : relation interne faible à cause des
  rivalités entre clans. Les nains, au contraire, gardent une relation interne forte grâce à la
  cohésion de leurs clans.
- Deux royaumes du **même peuple** peuvent se faire la guerre : deux royaumes humains entre eux,
  deux royaumes nains pour une montagne, deux tribus orcs. La relation des peuples est une base,
  pas une loi absolue :
  Pour deux royaumes du même peuple, la relation de départ est la `selfRelation` de ce peuple.
- **Validé, deux valeurs entre royaumes** :
  - `relation` : opinion durable, de −1 à +1 ;
  - `tension` : pression immédiate, de 0 à 1.

  `{ "relation": -0.6, "tension": 0.8 }`

  La tension augmente la probabilité de guerre, sans forcément changer l'opinion profonde.
  Deux royaumes humains peuvent se tolérer, puis entrer en guerre pour une mine ou un fleuve.

  `tension += ressources convoitées + frontière commune + histoire guerrière + ambition du
  dirigeant + pression démographique`

  `relation −= trahisons + guerres passées + atrocités + rivalités` (la relation part de celle
  des peuples).
- **Validé, pas de religions en V1.** La V1 garde : relation, tension, alliances, guerres,
  dirigeants. Les rivalités religieuses sont à 0, remplacées par des rivalités :
  - raciales ;
  - territoriales ;
  - commerciales ;
  - magiques ;
  - dynastiques.

  Plus tard : religions, divinités, cultes du Numen et de l'Entropie, schismes, guerres
  saintes. Elles se lieront au Numen, aux plans célestes, aux sanctuaires et aux divinités.
- **Validé, alliances** : une alliance est un **état diplomatique**. La relation, elle, est une
  opinion plus lente à évoluer. Une alliance agit sur les deux, avec des effets différents :
  `tension −= 0,5 à 0,9` et `relation += 0,1 à 0,3`.

  | Effet | Intensité |
  |---|---|
  | Baisse de tension | forte |
  | Hausse de relation | faible à moyenne |
  | Protection mutuelle | forte |
  | Coordination militaire | forte |
  | Commerce | moyen |

  Une alliance ne rend pas automatiquement deux peuples amis : deux royaumes peuvent s'allier
  contre une menace commune tout en restant méfiants. Si l'alliance dure, la relation monte peu
  à peu. Une alliance purement opportuniste peut se briser dès que la menace disparaît.
- **Validé, histoire courte V1 : un seul règne.**
  - durée : 40 à 80 ans, **60 ans par défaut** (un tour par an) ;
  - un dirigeant initial par royaume ;
  - aucun vieillissement, aucune succession ;
  - les dirigeants de départ sont ceux en place au début de la partie.

  Assez long pour quelques guerres, des alliances qui se forment et se brisent, des frontières
  qui bougent, des ruines et des forts, des rancunes durables. Assez court pour éviter les
  successions, les dynasties, les héritiers et les révoltes.

  | Durée | Effet |
  |---|---|
  | 10 à 20 ans | trop court : peu de guerres, peu de traces |
  | 40 à 80 ans | bon équilibre : plusieurs conflits, histoire crédible |
  | 100 à 200 ans | demande les successions et le vieillissement |
  | 300 ans et plus | demande dynasties, héritiers, révoltes, cultures qui évoluent |

  Exemple sur 60 ans :
  - an 0 : fondation des royaumes ;
  - an 8 : première guerre frontalière ;
  - an 15 : alliance contre un dragon ;
  - an 27 : trahison, fin de l'alliance ;
  - an 34 : grande guerre ;
  - an 47 : traité de paix ;
  - an 58 : dernière crise frontalière ;
  - an 60 : début de la partie.

  **Plus tard, une histoire longue en option** (100 à 300 ans) : vieillissement, morts,
  successions, dynasties, héritiers et révoltes simulés chaque année. Le générateur ne garde
  que l'état final : le dirigeant actuel, la dynastie au pouvoir, la mémoire des guerres, les
  frontières finales, les ruines et cicatrices historiques.
- **Validé, dirigeant initial** : le générateur crée un dirigeant de départ par royaume.

  ```json
  { "id": "ruler_042", "kingdom": "karth_dum", "name": "Roi Thorgrim III",
    "personality": { "ambition": 0.85, "aggression": 0.65, "greed": 0.75,
                     "prudence": 0.25, "piety": 0.3 },
    "goals": [ "secure_iron_mines", "expand_into_mountain_pass", "weaken_rival_clan" ] }
  ```

  Il sert seulement à rendre crédible l'histoire courte déterministe : il explique pourquoi un
  royaume attaque, attend, s'allie ou reste passif. La succession, les morts, les coups d'État
  et les nouveaux dirigeants appartiennent ensuite au jeu, côté serveur. Les noms viennent de
  la culture du royaume, de façon déterministe. Le générateur garde les deux valeurs : la diplomatie, la propagande, les guerres et les
alliances s'en servent séparément. Quand une seule valeur est utile :
`relationEffective(A, B) = (relation[A][B] + relation[B][A]) / 2`.

## 6. Format des peuples (proposition, À décider)

Un fichier JSON par peuple. Exemple de départ donné par l'utilisateur, avec les ajustements
proposés en commentaire :

```json
{
  "id": "nains",
  "name": "Nains",
  "plane": "material",
  "populationShare": 0.08,
  "kingdomShare": 0.1,
  "baseTerrainModel": "mountain",
  "physicalPreferences": { "temperature": { "min": -30, "ideal": 5, "max": 20 } },
  "underground": 0.9,
  "ambientEnergyPreferences": { "prana": 0.4, "aura": 0.8, "mana": 0.5 },
  "needs": { "metal": 1.0, "stone": 0.9, "freshwater": 0.6, "farmland": 0.4 },
  "fears": { "highMagic": 0.2, "volcano": 0.1, "deepOcean": 1.0, "monsterDensity": 0.6 },
  "government": [ "clan_monarchy", "council_of_clans", "hereditary_republic" ],
  "relations": { "humains": 0.6, "elfes": 0.2, "orcs": -0.8, "gobelins": -0.9 }
}
```

Décisions :

- **validé** : un seul champ de préférences de terrain. `preferredBiomes` et `avoidBiomes`
  sont supprimés, car ils pouvaient se contredire. Ce champ unique est `physicalPreferences`
  (voir plus bas).

- **validé** : des préférences physiques plutôt que des biomes. Un peuple ne dit pas
  « j'aime la forêt », il donne des plages mesurées sur le niveau Planète. Si le biome change
  plus tard, la préférence reste valide :

  ```json
  "physicalPreferences": {
    "altitude":      { "min": 200,  "ideal": 800, "max": 3000 },
    "temperature":   { "min": -10,  "ideal": 12,  "max": 30 },
    "rainfall":      { "min": 300,  "ideal": 900, "max": 2500 },
    "roughness":     { "min": 0.2,  "ideal": 0.6, "max": 1.0 },
    "waterDistance": { "max": 30 }
  }
  ```

  Unités proposées : altitude en m, température moyenne annuelle en °C, pluie en mm/an,
  rugosité de 0 (plat) à 1 (très accidenté) dans la case de 8 km, distance à l'eau douce en km.
- **validé** : `physicalPreferences` remplace `terrainWeights`, qui est supprimé.
- **validé** : des modèles de terrain nommés (`terrainModels` : `mountain`, `forest`…) dans
  la configuration du monde. Ce ne sont que des presets : un peuple en reprend un
  (`baseTerrainModel`) et l'ajuste. Préférences finales = modèle de base, puis ajustements du
  peuple, critère par critère.

  ```json
  { "id": "nains", "baseTerrainModel": "mountain",
    "physicalPreferences": { "temperature": { "min": -30, "ideal": 5, "max": 20 } } }
  ```

- **validé** : score d'un critère en **trapèze** avec plancher souple (`floor`, 0,05 par
  défaut). Il vaut 1 sur toute la plage idéale (`idealMin` à `idealMax`), baisse linéairement
  jusqu'à `floor` aux bornes `min` et `max`, et vaut `floor` hors des bornes. `ideal` est
  **remplacé** par `idealMin` et `idealMax` dans tous les fichiers (les exemples plus haut qui
  écrivent encore `ideal` se lisent `idealMin` = `idealMax` = `ideal`). Une plage idéale peut commencer à
  la borne (`idealMin` = `min`) : c'est le cas de la distance à l'eau, où la rive vaut 1. Un
  peuple peut très rarement fonder une colonie hors de sa zone, mais jamais s'y installer en
  masse.

  ```json
  "waterDistance": { "min": 0, "idealMin": 0, "idealMax": 15, "max": 100, "floor": 0.05 }
  ```

  **Validé, deux niveaux de limites** : `hardMin` et `hardMax` (facultatifs) donnent 0, c'est
  interdit ; `min` et `max` donnent le plancher souple.

  ```json
  "altitude": { "hardMin": 0, "min": 500, "idealMin": 1000, "idealMax": 2000,
                "max": 4000, "hardMax": 7000, "floor": 0.05 }
  ```

  | Zone | Score |
  |---|---|
  | sous `hardMin` | 0 |
  | entre `hardMin` et `min` | plancher (0,05) |
  | entre `min` et `idealMin` | montée vers 1 |
  | entre `idealMin` et `idealMax` | 1 |
  | entre `idealMax` et `max` | descente vers le plancher |
  | entre `max` et `hardMax` | plancher (0,05) |
  | au-delà de `hardMax` | 0 |

  Le plancher souple permet les réfugiés, les colonies isolées et les peuples marginaux. La
  limite absolue sert aux impossibilités biologiques ou magiques : un peuple marin ne fonde pas
  de capitale au cœur d'un désert, un peuple sans résistance au froid ne vit pas dans les glaces
  éternelles. Le 0 universel, valable pour tous, ne vient que de conditions impossibles pour
  tous : océan profond pour un peuple terrestre, vide magique total, zone stérilisée, absence
  totale d'eau.

  Rive ou lac : 1. De 0 à 15 km : 1. Au-delà : baisse progressive. Au-delà de 100 km : plancher.
  Une case au bord d'une rivière n'est jamais pénalisée pour être trop proche de l'eau.

  ```python
  def preference_score(value, pref):
      lo, hi = pref["min"], pref["max"]
      ideal_lo, ideal_hi = pref["idealMin"], pref["idealMax"]
      floor = pref.get("floor", 0.05)
      if value < pref.get("hardMin", float("-inf")) or value > pref.get("hardMax", float("inf")):
          return 0.0
      if ideal_lo <= value <= ideal_hi:
          return 1.0
      if value <= lo or value >= hi:
          return floor
      t = (value - lo) / (ideal_lo - lo) if value < ideal_lo else (hi - value) / (hi - ideal_hi)
      return floor + (1.0 - floor) * t
  ```

  Exemple pour l'altitude (500 / 1 500 / 4 000, plancher 0,05) : 0 m → 0,05 ; 1 000 m →
  0,525 ; 1 500 m → 1 ; 2 750 m → 0,525 ; 6 000 m → 0,05.
- **validé** : score physique d'un peuple, avec des poids par critère (`physicalWeights`) et
  un « minimum mou », pour qu'un seul critère catastrophique pénalise fortement un site sans
  l'annuler :
  `physicalScore = 0,7 × moyenne pondérée + 0,3 × score le plus bas`
- **validé** : `raceSuitability[peuple][case] = habitability + physicalScore × poidsPhysique
  + prana × préférencePrana + aura × préférenceAura + mana × préférenceMana − danger ×
  sensibilitéAuDanger + bruit déterministe` (le bruit vient de la seed).
- **À décider** :
  - un critère à un seul côté, comme la distance à l'eau : avec `min` 0 et `ideal` 10, une
    case au bord de la rivière tomberait au plancher. Il faut permettre `ideal` = `min` (le
    score vaut 1 dès la borne) ;
  - un vrai plateau, avec une plage idéale (`idealMin`, `idealMax`) au lieu d'un seul point ;
- **validé** : le Prana compte deux fois, avec deux rôles différents. Tous les scores sont
  normalisés entre 0 et 1, pour éviter une inflation :
  - `habitability` répond à « ce lieu peut-il nourrir et abriter une civilisation ? » :
    eau + nourriture + climat + terrain + Prana − danger. Ici, le Prana rend le lieu vivable ;
  - `raceSuitability` répond à « ce peuple précis aime-t-il ce lieu ? » : habitabilité
    normalisée + préférences physiques + préférences énergétiques − sensibilité au danger.
    Ici, le Prana rend le lieu attirant pour un peuple qui l'aime ;
  - `habitabilityNormalisée = clamp(habitability / habitabilityMax, 0, 1)` et
    `raceSuitability = clamp(valeurFinale, 0, 1)` ;
  - **validé** : `habitabilityMax` est le **maximum théorique**, la somme des poids positifs
    multipliés par la valeur maximale de chaque critère. Chaque case est normalisée seule : son
    score ne change pas parce qu'une autre région du monde est devenue plus fertile. C'est plus
    stable, plus prévisible et compatible avec une génération par chunks.

Ajustements encore proposés :

- **validé** : `kingdomShare` (part relative des royaumes) au lieu de `majorKingdoms` et
  `minorStates`. Chaque peuple donne sa part, et le preset du monde fixe le total :

  ```json
  { "id": "humains", "kingdomShare": 0.35 }
  { "id": "elfes",   "kingdomShare": 0.10 }
  { "worldPreset": { "totalMajorKingdoms": 40, "totalMinorStates": 180 } }
  ```

  `royaumesHumains = totalMajorKingdoms × kingdomShare[humains]` (ici 14).
  **Validé, deux parts par peuple** : `kingdomShare` devient `majorKingdomShare` (grands
  royaumes) et `minorStateShare` (petits États, tribus, cités-États), chacune normalisée et
  arrondie à part, avec le total de son preset :

  ```json
  { "id": "gobelins", "majorKingdomShare": 0.05, "minorStateShare": 0.20 }
  ```

  Les gobelins ont peu de grands royaumes mais beaucoup de tribus. Les humains ont beaucoup de
  royaumes et de cités-États. Les elfes ont peu d'États, mais de grands royaumes anciens.
  **Validé** : les parts sont normalisées automatiquement
  (`partNormalisée[peuple] = kingdomShare[peuple] / somme des kingdomShare`). On peut donc
  écrire des parts simples (humains 35, elfes 10, nains 8, orcs 8, gobelins 7), et ajouter un
  peuple n'oblige pas à retoucher les autres fichiers.
  **Validé, arrondis** :
  1. `royaumesExact = total × partNormalisée` ;
  2. arrondi à l'entier ;
  3. tout peuple dont la part est supérieure à 0 reçoit au moins 1 royaume ;
  4. la différence avec le total cible est corrigée en ajoutant ou en retirant des royaumes aux
     peuples dont la fraction était la plus proche du seuil d'arrondi. On ne retire jamais un
     royaume à un peuple qui n'en a qu'un. En cas d'égalité, on départage par l'`id` du peuple,
     pour rester déterministe.

  **Validé, le minimum porte sur le total** (grands royaumes + petits États), pas sur chaque
  catégorie. Par exemple, avec 10 grands royaumes et 13 peuples :
  - un peuple avec `majorKingdomShare` = 0 peut n'avoir aucun grand royaume ;
  - s'il a `minorStateShare` > 0, il a au moins 1 petit État ;
  - un peuple sans aucune part ne reçoit aucun État.

  Si plus de peuples ont une part de grands royaumes qu'il n'y a de grands royaumes, les plus
  petites parts n'en reçoivent pas, pourvu que chaque peuple garde au moins 1 État au total ;

- **validé** : vie souterraine. Les grottes ne se voient pas sur une case Planète, il faut
  donc une habitabilité souterraine à part, calculée sur les régions de grottes que le moteur
  génère déjà :

  ```json
  "underground": { "level": "partial", "preferredDepth": 0.6, "needsSurfaceAccess": true }
  ```

  `level` vaut `none` (surface seulement), `partial` (surface et sous terre), `mostly` (surtout
  sous terre) ou `full` (sous terre seulement). `preferredDepth` est la profondeur préférée :
  0 juste sous la surface, 0,5 profondeur moyenne, 1 les grandes profondeurs.

  | Peuple | `level` | `preferredDepth` |
  |---|---|---|
  | Humains | none | 0 |
  | Gnomes | partial | 0,2 |
  | Nains | partial | 0,5 |
  | Gobelins | mostly | 0,6 |
  | Kobolds | mostly | 0,7 |
  | Drows (plus tard) | full | 0,9 |
  | Illithids (plus tard) | full | 1,0 |

  **Validé, habitabilité du sous-sol** :
  `sousSolHabitabilité = densité de grottes + roche stable + eau souterraine + minerais +
  pierre + accès à la surface − risques d'effondrement − danger des monstres − absence d'air
  ou d'eau`.
  **Validé** : les goûts propres à un peuple viennent de son fichier
  (`undergroundPreferences`), jamais du code. Aucune formule n'est écrite pour un peuple
  précis : le moteur applique la même formule à tous les peuples souterrains, pondérée par
  leurs goûts. Exemple des kobolds :

  ```json
  { "id": "kobolds",
    "undergroundPreferences": { "caveDensity": 0.9, "stableRock": 0.7, "groundwater": 0.6,
      "ore": 0.8, "stone": 0.7, "heat": 0.8, "volcanoProximity": 0.7,
      "dragonProximity": 0.6, "cold": -0.6, "surfaceAccess": 0.4 } }
  ```

  Peuples souterrains de la V1 :

  | Peuple | Mode de vie |
  |---|---|
  | Nains | surtout en surface et en montagne, avec des mines et des cités souterraines |
  | Gobelins | surtout souterrains, sortent pour piller et chasser |
  | Kobolds | surtout souterrains (`mostly`), proches des dragons et des volcans |
  | Gnomes | `partial`, `preferredDepth` 0,2 : surtout en surface, mais utilisent grottes, mines et tunnels |

  Plus tard : drows, hommes-rats, illithids, peuples des profondeurs (entièrement
  souterrains), trolls (grottes et montagnes, semi-souterrains).

## 6 bis. Fichier de configuration du monde (validé)

Tous les poids sont dans un fichier de configuration du monde, modifiable sans agent :

```json
{
  "habitabilityWeights": { "water": 1.0, "food": 1.0, "climate": 0.8, "terrain": 0.6,
                           "prana": 0.3, "aura": 0.2, "mana": 0.4, "danger": -1.0,
                           "coast": 0.2, "river": 0.5 },
  "dangerWeights": { "naturalDanger": 1.0, "magicInstability": 2.0,
                     "monsterTerritory": 1.5, "civilizationProtection": -1.0 },
  "expansionCosts": { "plains": 1, "hills": 2, "forest": 2, "swamp": 5, "desert": 6,
                      "mountain": 10, "ocean": 50 }
}
```

On peut ainsi tester rapidement :

- un monde plus ou moins magique ;
- des royaumes plus ou moins expansionnistes ;
- des montagnes plus ou moins infranchissables ;
- des monstres plus ou moins dangereux ;
- une magie plus ou moins rare.

Le même fichier contient aussi les `terrainModels`, les presets de monde et les poids du score
physique.

**Validé, coût d'une case pour l'expansion** : une base rapide donnée par le modèle de terrain
le plus proche, affinée par les valeurs physiques :
`coûtCase = coût du modèle de terrain le plus proche + pénalité d'altitude + pénalité de
rugosité + pénalité d'aridité + pénalité de fleuve + pénalité de danger`.

| Terrain | Coût de base |
|---|---|
| Plaine | 1 |
| Colline | 2 |
| Forêt | 2 |
| Marais | 5 |
| Désert | 6 |
| Montagne | 10 |
| Océan | 50 ou interdit |

Traversée d'un fleuve, selon sa taille :

| Cours d'eau | Coût de traversée |
|---|---|
| Ruisseau | 0 à 1 |
| Rivière | 2 à 4 |
| Grande rivière | 5 à 8 |
| Fleuve majeur | 8 à 15 |
| Fleuve infranchissable | 20 et plus |

`coûtFleuve = coût de base − bonus de gué − bonus de pont − bonus de confluent` : un gué, un
pont ou un confluent réduit fortement le coût.

**Validé, gués, ponts et confluents** :
- pendant l'expansion, un **gué probable**, estimé sur la case :
  `guéProbable = rivière large ou moyenne + pente faible + plaine ou vallée + courant lent −
  gorge − falaise`. Il réduit le coût de traversée, mais ne garantit pas un vrai gué à l'échelle
  du site ;
- les **ponts** n'existent pas encore pendant l'expansion : ils sont créés après les villes et
  les routes (étape 9), et comptent ensuite pour les routes et l'histoire ;
- **confluent** : `coûtConfluent = coût du fleuve le plus large − bonusConfluent`. Le bonus
  est gardé, mais il n'annule jamais la taille du fleuve. Un confluent facilite le passage, le
  commerce et la fondation d'une ville, mais un grand fleuve reste difficile à traverser.

**Validé, taille d'un cours d'eau selon son débit** :

| Type | Débit |
|---|---|
| Ruisseau | moins de 10 m³/s (seulement à l'échelle du site, pas dans l'expansion planétaire) |
| Rivière | 10 à 100 m³/s |
| Grande rivière | 100 à 1 000 m³/s |
| Fleuve majeur | 1 000 à 10 000 m³/s |
| Fleuve infranchissable | plus de 10 000 m³/s |

**Règle de déterminisme (validé)** : un monde garde la configuration avec laquelle il a été
créé. Changer les poids plus tard affecte les nouveaux mondes, jamais rétroactivement les
mondes existants, sauf migration explicite. Chaque monde enregistre :

```json
{ "worldSeed": "...", "secretSeedId": "...", "worldPreset": "...", "configVersion": "1.0",
  "generationState": { "terrain": "done", "rivers": "done", "coasts": "pending",
                       "civilizations": "pending" } }
```

La seed secrète elle-même reste dans un fichier du serveur, jamais envoyé aux clients ;
`secretSeedId` sert seulement à la retrouver.

**Validé, copie complète de la configuration** : un numéro de version ne suffit pas. Le monde
garde une copie complète de sa configuration et une empreinte :

```json
{ "world": {
    "seed": "...", "secretSeedId": "...",
    "config": { "worldPreset": {}, "habitabilityWeights": {}, "expansionCosts": {},
                "peoples": [], "monsters": [], "magic": {} },
    "configHash": "...",
    "generationState": { "terrain": "done", "rivers": "done", "coasts": "pending",
                         "civilizations": "pending" } } }
```

**Validé, étapes ajoutées plus tard** :

| Étape | Effet sur les mondes existants |
|---|---|
| Côtes détaillées | peut être ajoutée, car elle affine des données déjà générées |
| Civilisations | peut être ajoutée si le monde n'a pas encore de peuples |
| Nouvelles règles de fleuves | ne modifie jamais un monde déjà peuplé |
| Nouvelle magie | peut être ajoutée comme couche, mais peut modifier le danger et l'habitabilité |
| Changement de preset | nouveaux mondes seulement, sauf migration explicite |

Règle simple :
- une étape qui complète une couche manquante est ajoutée aux mondes existants ;
- une étape qui modifie une couche déjà utilisée vaut pour les nouveaux mondes seulement, ou
  passe par une migration explicite avec sauvegarde.

**Validé, côtes détaillées sur un monde déjà joué.** Elles changent la forme du monde : elles
peuvent créer, supprimer ou déplacer des ports, des baies, des falaises et des zones
navigables.
- Monde sans civilisations : on ajoute les côtes détaillées.
- Monde avec civilisations, mais aucune modification de joueur dans les zones touchées : on
  ajoute les côtes, puis on recalcule les ports et les littoraux.
- Monde avec des modifications de joueurs dans ces zones : aucune modification automatique, on
  propose une migration explicite avec sauvegarde.

Migration des côtes, dans l'ordre :
1. sauvegarder l'état d'avant ;
2. identifier les zones touchées ;
3. vérifier qu'aucun joueur ne les a modifiées ;
4. recalculer les côtes ;
5. recalculer les ports ;
6. vérifier la cohérence des royaumes.

Si une ville, un port, une route ou une frontière dépend d'une côte modifiée, la migration doit
être validée avant d'être appliquée. Règle générale : une couche qui dépend d'une autre respecte
les données déjà générées, sinon elle est réservée aux nouveaux mondes.

L'empreinte détecte une configuration modifiée. La copie complète garantit que le monde reste
reproductible même si les fichiers globaux changent. L'empreinte peut réutiliser la somme de
contrôle déjà employée par le projet (celle du niveau Planète), puisque son rôle est de
détecter un changement, pas de protéger un secret.

## 7. Partage entre le générateur et Unreal

Même règle que `docs/ARCHITECTURE.md` :

- le générateur (`terrain_core`) **décide** tout ce qui doit être identique sur le serveur et
  sur tous les clients, de façon déterministe ;
- Unreal **montre et fait jouer** ce que le générateur a décidé ;
- `terrain_core` ne dépend jamais d'Unreal ni d'un moteur graphique.

| Sujet | Générateur (`terrain_core`) | Unreal (moteur de jeu) |
|---|---|---|
| Fichiers de données (peuples, professions, modèles de terrain, poids, presets) | les lit, les valide, les fusionne (modèle de base + ajustements), refuse un fichier invalide | éditeur ou outil pour les modifier, choix du preset à la création du monde, transmis au générateur |
| Cartes d'analyse (pente, eau, terres arables, nourriture, ressources, danger, habitabilité) | les calcule sur le niveau Planète, les donne à la demande | cartes du monde, minicarte, filtres de debug |
| Géologie en données, lignes telluriques, nœuds, rifts, sanctuaires, zones mortes | positions, tracés et intensités | effets visuels (lueurs, particules, brume, son), visibles ou cachés selon le gameplay |
| Mana caché (seed secrète) | calculé sur le serveur seulement | le serveur de jeu envoie à chaque client ce qu'il a découvert (exploration, rituels, artefacts) |
| Prana, Aura, Mana, instabilité, danger magique | valeurs par case et à la demande en un point | effets de jeu : régénération, progression, puissance des sorts, ambiance du lieu |
| Énergies personnelles (Anima, Numen, Entropie) | rien (seulement le bonus ou le malus du lieu, s'il en a un) | entièrement : personnages, liens, pactes, divinités || raceSuitability, graines, capitales | calcul complet | rien, sauf l'affichage de debug |
| Expansion, frontières, histoire courte (guerres, alliances, ruines, forts), dirigeants initiaux | calcul complet de l'état initial du monde | affichage des frontières ; la politique **en cours de partie** (nouvelles guerres, traités) est du gameplay, côté serveur de jeu |
| Villes, villages, routes | plan : sites exacts, hiérarchie, tracés, parcelles ; pour chaque bâtiment : emprise, hauteur, type, style, orientation, matériaux ; retouches du relief (routes, terrasses, quais) | bâtiments assemblés depuis des kits modulaires (PCG), décor, PNJ, vie des villes |
| Vie souterraine | habitabilité souterraine, sites des cités et des mines dans les régions de grottes, galeries creusées (retouches du volume, comme les routes en surface) | éclairage, décor et vie des cités souterraines |
| Monstres et danger | carte du danger ; état initial des territoires : nids, zones de chasse, de reproduction et d'influence, densité, agressivité | apparition des créatures, IA, combats ; ce qui change en cours de partie (un dragon tué, un nid détruit) est du gameplay, côté serveur de jeu |
| Plans parallèles et portails | génération de chaque plan et position des portails | rendu de chaque plan, passage d'un plan à l'autre, effets |

Aucune géométrie de bâtiment, aucun effet visuel et aucune règle de combat dans `terrain_core`.

## 8. Questions ouvertes

Elles sont dans `docs/PEUPLEMENT_QUESTIONS.md`. Chaque réponse est reportée ici, puis la
question est retirée de cette liste.
