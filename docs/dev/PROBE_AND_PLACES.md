# Formats de la sonde et des lieux (agent 37)

Deux formats partagés par les outils de développement : la sortie de `terrain_probe` (ce que le
core sait d'un point) et le fichier des lieux `data/dev/places.json` (où placer une vue). Le
lanceur de vérification (`scripts/verify/`) lit les deux.

## Unités et repères

- Distances et altitudes en **mètres**. x vers l'est, z vers le sud, y l'altitude du core (la
  mer à 0 m).
- **Cap** (`heading`) en degrés dans le sens des aiguilles d'une montre depuis le nord (−z du
  core, −Y d'Unreal), dans [0, 360). **Yaw d'Unreal = cap − 90**.
- **Inclinaison** (`pitch`) en **degrés**, positive vers le haut, dans [−90, 90] : −90 regarde
  droit en bas. Les outils de l'agent 20 (`terrain_export_rock --at`, `--steps`) donnent une
  *tangente* (`tilt`) : pour en faire un lieu, `pitch = atan(tilt)` en degrés
  (`places.tilt_to_pitch`).
- **Œil** (`eye`) selon `eyeMode` :
  - `altitude` : l'altitude absolue du core ;
  - `ground` : la hauteur au-dessus du sol, ou au-dessus de l'eau là où il y a de l'eau.
- **Repère d'Unreal** (`terrain_unreal/.../Bridge/unreal_frame.hpp`) : avec `CoreOriginX/Z` sur
  (ox, oz), le point (0, 0, 0) d'Unreal est le sol du core sous (ox, oz), d'altitude oy (le
  `ground` de la section `relief` de la sonde). Un point (x, y, z) du core est en
  `X = 100 (x − ox)`, `Y = 100 (z − oz)`, `Z = 100 (y − oy)` centimètres.

## Le fichier des lieux : `data/dev/places.json`

```json
{ "version": 1,
  "places": [
    { "name": "chute388_bassin", "x": -6185104, "z": 155924,
      "heading": 227, "pitch": 30, "eye": 182.03, "eyeMode": "altitude",
      "planet": true, "note": "chute de 388 m vue depuis son bassin" } ] }
```

- `name` : un mot, unique dans le fichier.
- `x`, `z`, `heading`, `pitch`, `eye`, `eyeMode` : comme ci-dessus.
- `planet` : la vue a besoin du fichier planète (fleuves, lacs, chutes, climat des biomes).
- `note` : facultative.
- Qui lit et écrit :
  - l'agent 36 a le lecteur strict et l'écrivain (`terrain_core/dev/places_file`, avec
    `Terrain.Save` en jeu) ;
  - le lanceur le lit en Python (`scripts/verify/places.py`).

## La sortie de `terrain_probe`

```
terrain_probe [--planet <fichier>] [--json] [--sections a,b] <x> <z> [<x> <z>]...
terrain_probe [--planet <fichier>] [--json] --line <x1> <z1> <x2> <z2> <pas>
terrain_probe --planet-check <fichier> [--json]
terrain_probe --list-sections
```

- **En-tête** :
  - `tool` (`"terrain_probe"`) et `format` (1) ;
  - `world` : `seed`, `x`, `z`, `generatorVersion` ;
  - `planet` : `file`, `status` (`none`, `read` ou `built`), `checksum` (`"0x…"`, 16 chiffres).
- Puis l'un des deux :
  - `points` : une entrée par point, avec `x` et `z` ramenés dans le monde, puis une section par
    module, dans l'ordre de la table ;
  - `line` : `from`, `to`, `step`, `length` et `points`, chaque point avec sa `distance` le long
    de la ligne. Les points sont tous les `pas` mètres, puis l'arrivée exacte ; en texte, un
    tableau.
- **Valeurs** :
  - chaque section donne **les mêmes clés à tous les points** ; une valeur absente vaut `null`
    (`-` en texte) ;
  - les nombres ont un nombre fixe de décimales ;
  - les sections qui ont besoin de la planète disent `available: false` sans elle.
- Les sections (`--list-sections`), dans leur ordre :

  | Section | Ce qu'elle donne |
  |---|---|
  | `relief` | relief seul, sol avec l'eau (le sol du repère d'Unreal), pente, couches |
  | `geography` | distance signée à la côte, ceintures, rifts, intensité du relief |
  | `planet` | case du niveau Planète : climat, lac, case de fleuve, débit, vent |
  | `water` | type, niveau, profondeur, vallée (`valley`), lac (`lake`), courant (`flow`) |
  | `falls` | chutes proches (`near[]`) et couloir de marches (`corridor`) |
  | `rock` | pente la plus forte, intervalles pleins de la colonne (`solid[]`), `faces` |
  | `coast` | côte à la mer ou non, distance, cap vers la mer, `type` |
  | `surface` | biome, matériau, plantes, climat local, poids du fondu |
  | `render` | ce que reçoit Unreal : sommets des tuiles LOD 0 à 3, horizon |

- `--planet-check` :
  - il lit le fichier **sans jamais l'écrire ni le construire** ;
  - il répond `ok`, `missing`, `stale` (autre seed, monde, version du générateur ou
    paramètres), `damaged`, `newer` ou `unreadable`, avec la somme du niveau si c'est `ok` ;
  - code de sortie 0 si `ok`, 2 sinon, 1 pour des arguments invalides.
- Le schéma (la liste des clés) est figé dans `tests/probe_test.cpp`.

## Règle de version

- **Ajouter** une clé ou une section reste compatible : `format` (sonde) et `version` (lieux)
  ne changent pas.
- **Renommer ou supprimer** une clé ou une section, ou changer son unité ou son sens, en fait une
  **nouvelle version** : `format` 2, ou `version` 2. Les lecteurs refusent une version qu'ils ne
  connaissent pas.

## Clés inconnues

- Un **lecteur** ignore les clés qu'il ne connaît pas : celles d'un autre agent, ou d'une version
  compatible plus récente. Par exemple, `scripts/verify/places.py` les ignore.
- Un **écrivain** les **garde telles quelles** quand il réécrit un fichier : l'écrivain des lieux
  de l'agent 36, et tout outil qui modifie un lieu.

## Ajouter une section à la sonde

1. Écrire sa fonction dans `terrain_core/tools/probe_<nom>.cpp`. Elle prend
   `Record fn(const ProbeWorld&, const Point&)` et donne toujours les mêmes clés (`none(...)`
   pour une valeur absente).
2. La déclarer dans `terrain_core/tools/probe_sections.hpp`, et ajouter une ligne à la table de
   `probe_sections.cpp` : nom, description, fonction, et si la section est sondée par défaut
   le long d'une ligne.
3. Ajouter le fichier à `terrain_probe_lib` dans `terrain_core/CMakeLists.txt`.
4. Ajouter ses clés au schéma figé de `tests/probe_test.cpp` : une section nouvelle reste
   compatible.

Ce qui est prévu :
- **agent 20** (roche 3D, 12A) : `rock.faces`, aujourd'hui `null`, devient la liste des faces de
  la colonne ;
- **agent 33** (relief côtier) : `coast.type`, aujourd'hui `null`, devient le type de côte ;
- **agent 25** (peuplement) : une section à lui (royaume, peuple, Mana…), ajoutée à la fin de
  la table.
