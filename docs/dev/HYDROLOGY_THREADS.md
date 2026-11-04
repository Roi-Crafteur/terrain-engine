# L'hydrologie sur plusieurs fils et ses arrêts (agent 39)

Une note pour qui touche à `terrain_core/src/generation/water/`, en premier l'agent 31 (réseau
hydrographique). L'API publique de `Hydrology` (`hydrology.hpp`) n'a pas changé.

## Les règles

1. **Toute boucle longue appelle `workStopPoint()`.**
   - L'en-tête est `terrain/work_stop.hpp`.
   - « Longue » : plus de quelques millisecondes, par exemple une boucle sur les sections d'un
     tracé, sur les nœuds d'une inondation ou sur les lèvres.
   - L'appel ne coûte presque rien : une lecture `thread_local`. Sans portée d'arrêt, il ne fait
     rien.
   - Il lance `WorkStopped` quand le drapeau du fil est levé.
2. **Rien d'à moitié écrit.**
   - Un objet partagé n'est publié qu'entier, après la fin de son calcul.
   - Une exception, dont `WorkStopped`, peut sortir de n'importe quel `workStopPoint()` ou de
     n'importe quelle attente de cache. Elle ne doit laisser aucun cache, mémo ou compteur
     partagé dans un état partiel.
   - Les mémos par fil (`floorDistance`) ne gardent que des valeurs finies.
3. **Aucun `catch` n'avale `WorkStopped`.**
   - Entre un `workStopPoint()` et la portée qui l'a installé (le fil de la tuile, du morceau
     d'horizon, de la carte), tout `catch (...)` relance (`throw;`).
   - Seuls les fils qui installent la portée attrapent `WorkStopped`, avant toute autre
     exception : le travail est abandonné, sans erreur.
   - Pour vérifier :
     `grep -rn "catch *(" terrain_core/src/generation terrain_core/src/streaming terrain_core/src/map`.
4. **Les caches (`water/hydrology_lazy.hpp`, `Lazy<T>`) :**
   - un objet est fait hors de tout verrou ; deux fils font deux objets en même temps ;
   - un fil qui attend un objet en cours de calcul attend par pas de 2 ms
     (`condition_variable::wait_for`), puis appelle `workStopPoint()` ;
   - si le calcul lance une exception, la case redevient vide et un autre fil le refait ;
   - `make()` ne demande jamais sa propre case. Les dépendances restent sans cycle :
     - un tracé lit les tracés d'aval (`courseOf(next)`) et les niveaux d'amont (`startLevel`) ;
     - un niveau ne lit jamais un tracé.

## Ce que lisent les requêtes du lointain

Les niveaux de l'horizon passent par `seenAt`, puis `riversAt`, `clearanceAt` et
`floorDistance`. Pour un tronçon, ils lisent :

- les étangs (`Course::ponds`) ;
- la boîte du tracé (`minX`…`maxZ`) ;
- les morceaux (`Course::chunks` : centre, rayon, `flat`, `minLevel`, `maxDepth`, `minWall`) ;
- puis, seulement dans les morceaux qui ne se sautent pas, les points, les montées, les
  méandres, les fentes, les vasques, les lèvres et les blocs.

Un échantillon à 4 km qui tombe dans une vallée lit la vallée exacte : le tracé entier est
nécessaire au bit près.

## Où les fils sont arrêtés

| Fil | Drapeau | Levé par |
|---|---|---|
| tuiles (`StreamManager`) | `stop_` | son destructeur, avant les `join` |
| horizon CPU (`HorizonStreamer`) | `stop_` | son destructeur |
| horizon GPU (`HorizonEpochRunner`, `HorizonNormalStream`) | `stop_` | leur destructeur, avant d'attendre les jobs |
| carte de dev (`MapTileCache`) | `cancel_` | son destructeur (rendus, atterrissages, sondes) |
| sol d'une téléportation (`GroundRead`) | `stop_` | son destructeur, avant que les `future` n'attendent |
| boucles parallèles (`planet::parallelBlocks`) | celui du fil appelant | — |

Ne sont pas interrompus :

- le fil des modifications (`EditThread`) : la modification d'un joueur va au bout ;
- la construction de la session (lecture de `planet.bin`) ;
- une construction de planète (`PlanetJob`, à la fin de son étape).

## Les tests

- `hydrology_bits_test` : les bits d'un échantillon (tracés, requêtes sur les grilles de 2 et
  4 km, morceaux des niveaux 8 et 9), figés avant le travail de l'agent 39.
- `hydrology_stop_test` (seul, `RUN_SERIAL`) :
  - les arrêts sous charge ;
  - la médiane ≤ 100 ms ;
  - le plus long < 1/4 du temps de la fin des tracés en cours ;
  - un fil qui attend ;
  - rien d'à moitié fait ;
  - les autres fils continuent.
- `hydrology_threads_test` : mêmes bits sur 1 et 4 fils, et après des arrêts.
- `unreal_work_stop_test` (seul) : chaque fil du plugin, puis les quatre dans l'ordre de
  `ReleaseWorld`.
