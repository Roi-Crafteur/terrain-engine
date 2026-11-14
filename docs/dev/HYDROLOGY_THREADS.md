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
   - un fil qui attend un objet en cours de calcul appelle `workStopPoint()`, puis attend par pas
     de 2 ms (`condition_variable::wait_for`) ;
   - un drapeau levé par `raiseWorkStop(flag)` réveille tout de suite ces fils :
     - chaque cache s'inscrit tant qu'il vit (`WorkStopWaking`) ;
     - la levée fait `notify_all` sur chacune de ses tranches, sous le verrou de la tranche, pour
       qu'aucun fil ne la manque entre sa vérification et son attente ;
     - ce verrou n'est jamais tenu pendant un calcul ;
     - mesuré : le fil qui attend sort en 0,05 à 0,1 ms, au lieu de la fin de son pas
       (2 ms demandées, 15,6 ms par défaut sous Windows, jusqu'à 89 ms en CI sous charge) ;
     - un drapeau levé par un simple `store(true)` arrête aussi, mais au pas suivant ;
   - si le calcul lance une exception, la case redevient vide et un autre fil le refait ;
   - `make()` ne demande jamais sa propre case. Les dépendances restent sans cycle :
     - un tracé lit les tracés d'aval (`courseOf(next)`) et les niveaux d'amont (`startLevel`) ;
     - un niveau ne lit jamais un tracé.
5. **La mémoire des caches est bornée** (après l'audit de l'agent 53) :
   - `HydrologyParams::cacheBytes`, 256 Mio par défaut, borne les octets tenus : 70 % pour les
     tracés, 20 % pour les chemins, 10 % pour les lacs fins, jamais moins de 4, 1 et 4 Mio. Il ne
     change que la mémoire, jamais un résultat ;
   - au-delà de sa part, un cache lâche les objets lus il y a le plus longtemps, jusqu'aux trois
     quarts de sa part, mais jamais celui qu'il vient de faire ;
   - `get` rend un `Ref` compté (`CourseRef`, `PathRef`, `LakeRef`) : **le garder tant qu'on lit
     l'objet**, jamais un pointeur brut au-delà. Un objet lâché vit tant qu'un `Ref` le tient :
     - `makeCourse` garde ses chemins et les tracés qu'il rejoint jusqu'à la fin de
       `buildCourse` (les `Join` tiennent des pointeurs bruts) ;
     - `Region::Data::Reach::course` garde le tracé pour tout le lot ;
   - un objet lâché est refait, aux mêmes bits, quand on le redemande : il ne dépend que de son
     index. `hydrology_cache_test` le vérifie avec un budget de 1 Mio ;
   - les fonds (`floors`) et les niveaux de départ (`startLevels`) ne sont jamais lâchés : un
     petit objet par tronçon demandé, borné par le nombre de tronçons du monde ;
   - l'horloge des lectures n'avance qu'à chaque objet fait : une lecture n'écrit que le tic de
     son objet, sans contention mesurable sur l'horizon ;
   - `coursesComputed()` compte les tracés faits depuis le début, refaits compris ;
     `courseBytes()` compte les octets tenus maintenant ;
   - mesuré avec le bench (`planet.bin` du monde par défaut), pic du processus, `main` contre la
     branche :
     - marche de 20 km le long d'un fleuve : 565 contre 572 Mio. Elle reste sous le budget
       (92 Mo de tracés tenus), rien n'est lâché ;
     - tour de 6 lieux lointains : 951 à 957 contre 734 à 740 Mio. Les mêmes 11 380 tracés sont
       faits avec et sans budget : aucun n'est refait, le temps est le même.

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

## Le coût du lointain, pour l'agent 31

**Mesure** (callgrind et `unreal_loading_bench --levels`, monde par défaut avec sa planète, au
spawn) :
- les niveaux 8 et 9 de l'horizon prennent 47,5 s sur un fil, sur 62 s pour tout l'horizon ;
- ils demandent 4 631 tracés (76 Mo) ;
- `courseOf` prend 97 % de leur temps, dont 88 % par `startLevel`, puis `floorOf`, des tronçons
  d'amont ;
- `lowestPath` (l'échelle de Viterbi, environ 2 000 hauteurs du relief par tronçon) en prend
  63 %, `lowestAcross` 19 %, les lacs fins 10 %, `buildCourse` 9 % ;
- les bandes mouillées des lèvres (`wetLips`) sont négligeables.

**Sans gain au bit près** :
- différer `wetLips` ;
- ne plus calculer sous verrou : 13,9 s contre 13,8 s sur 3 fils ;
- lire l'échelle par lots : dans le bruit.

Il n'y a pas de raccourci au bit près : un échantillon de 2 ou 4 km qui tombe dans une vallée
lit la vallée exacte au point, donc le tracé entier.

**Refusé par l'orchestrateur** : pas de vallée, ou une vallée simplifiée, aux niveaux ≥ 8.
C'était 4 à 9 fois plus rapide, mais :
- les fleuves s'arrêteraient net à la limite du niveau 7 (environ 132 km), une limite qui bouge
  avec le joueur ;
- les gorges de 2 km à travers les crêtes creuseraient des créneaux de 0,2 à 0,6° entre 130 et
  530 km.

C'est un détail qui surgit devant le joueur.

**La voie** : réduire ce coût à la source, avec les mêmes vallées à toutes les distances :
- des tracés plus économes : moins de hauteurs par tronçon, ou des hauteurs partagées entre
  tronçons voisins ;
- ou le graphe de la planète réutilisé pour le lointain, si les vallées restent les mêmes.

`hydrology_bits_test` fige les tracés, les requêtes sur les grilles de 2 et 4 km et des morceaux
des niveaux 8 et 9. Le bench affiche les tracés calculés, leurs octets et le pic du processus.

## La couture du monde, pour l'agent 31

Relevée par l'audit (agent 53), hors du champ de l'agent 39 :

- `Heightfield::waterOf` (`heightfield_hydrology.cpp`, l'appel de `regionOf`) prend la boîte des
  points d'un lot une fois ramenés dans le monde canonique (`wrapCanonical`). Une tuile à cheval
  sur la couture (x = ±X/2 ou z = ±Z/2) a donc une boîte large de tout le monde.
- `Hydrology::regionOf` refuse une boîte de plus de 4 096 m : la tuile lit alors l'eau point par
  point, sans `Region`. Ce sont les mêmes bits, plus lentement, et sur une ligne du monde
  seulement.
- Mesuré sur un fleuve de 12 000 m³/s qui traverse la couture, les tracés déjà faits : une
  tuile à cheval coûte 1,1 à 1,7 fois sa voisine (32 m au pas de 1 m : 5 à 8 ms contre 5 à 7 ;
  256 m au pas de 4 m : 18 à 20 ms contre 15 à 17).
- La réparer change le contrat de `regionOf` (une boîte canonique) et de
  `Region::Data::partOf` : mieux vaut le faire dans la réécriture du réseau.
- Piste : prendre la boîte dans les images du premier point (chaque point ramené à moins d'une
  demi-largeur du monde de celui-ci), comme les tronçons le font déjà, et faire chercher à
  `partOf` l'image du point dans la boîte.

## Où les fils sont arrêtés

Chaque drapeau est levé par `raiseWorkStop`.

| Fil | Drapeau | Levé par |
|---|---|---|
| tuiles (`StreamManager`) | `stop_` | son destructeur, avant les `join` |
| horizon CPU (`HorizonStreamer`) | `stop_` | son destructeur |
| horizon GPU (`HorizonEpochRunner`, `HorizonNormalStream`) | `stop_` | leur destructeur, avant d'attendre les jobs |
| carte de dev (`MapTileCache`) | `cancel_` | son destructeur (rendus, atterrissages, sondes) |
| sol d'une téléportation (`GroundRead`) | un par lecture (`Read::stop`) | `forget()` à la téléportation suivante ; le destructeur |
| boucles parallèles (`planet::parallelBlocks`) | celui du fil appelant | — |

Ne sont pas interrompus :

- le fil des modifications (`EditThread`) : la modification d'un joueur va au bout ;
- la construction de la session (lecture de `planet.bin`) ;
- une construction de planète (`PlanetJob`, à la fin de son étape).

## Les tests

- `hydrology_bits_test` : les bits d'un échantillon (tracés, requêtes sur les grilles de 2 et
  4 km, morceaux des niveaux 8 et 9), figés avant le travail de l'agent 39.
- `hydrology_stop_test` et `hydrology_wait_test` : deux tests du même exécutable
  (`hydrology_stop_test stops` et `hydrology_stop_test waits`), seuls sur la machine
  (`RUN_SERIAL`, posé dans `tests/serial_tests.cmake` par l'agent 42).
  - `hydrology_stop_test` :
    - les arrêts sous charge, dans les tracés longs de l'échantillon (100 ms ou plus à faire),
      au milieu du premier ;
    - le plus long < 1/4 du temps de la fin des tracés en cours, mesurée comme un `join`
      l'attendrait : les quatre finis en même temps, sous la même charge ;
    - rien d'à moitié fait : l'échantillon relu sur l'hydrologie du premier essai donne les bits
      d'une hydrologie jamais arrêtée. Il est relu une fois la charge arrêtée, sur le même
      état : cette hydrologie n'est plus touchée après son essai, chaque essai ayant la sienne.
  - `hydrology_wait_test` :
    - un fil qui attend un tracé fait par un autre sort en moins du quart de ce que le tracé
      prend encore, sous charge : le tracé attendu (`kWaitCourse`) reste 250 ms ou plus chez son
      fabricant après la levée ;
    - les autres fils continuent.
  - Les médianes (arrêt, sortie du fil qui attend) ont un but de 100 ms. Il est imprimé
    (`time goal: …`) et vérifié seulement par `hydrology_stop_test_bench`, construit avec
    `TERRAIN_TIME_GOALS` et lancé à la main sur une machine calme (règle de l'agent 42).
  - Ce qu'ils attrapent (expériences non commitées, 3 fois chacune) :
    - `raiseWorkStop` sans effet : aucun fil ne sort par `WorkStopped` ;
    - tous les points d'arrêt d'un tracé retirés, entrées des chemins, fonds et tracés
      comprises : idem ;
    - l'attente d'un cache sans point d'arrêt : le fil qui attend ne sort qu'avec le tracé.
  - Ce qu'ils ne voient pas : un seul point d'arrêt retiré dans une boucle (les sections de
    `lowestPath`, ou tous ceux du corps d'un tracé). Les entrées des chemins et des fonds
    d'amont restent, et le trou ne fait qu'environ 10 ms, sous le quart de la fin des tracés.
- `lazy_wake_test` : le réveil des caches, sans temps mesuré.
  - Un `Lazy<int>` dont l'attente n'a pas de pas (`kLazyForever`) et dont le fabricant est
    retenu : seul le réveil par `raiseWorkStop` peut faire sortir le fil qui attend.
  - Sans ce réveil (`wakeAll` vide), le fil attend encore après 30 s (un filet, pas une mesure),
    et le test le dit : « the waiting thread was not woken by raiseWorkStop ».
- `hydrology_threads_test` : mêmes bits sur 1 et 4 fils, et après des arrêts.
- `hydrology_cache_test` : avec un budget de 1 Mio, les tracés lâchés puis refaits donnent les
  mêmes bits, les octets tenus restent sous la part des tracés, et 4 fils qui lâchent et lisent
  en même temps donnent les bits d'un seul.
- `unreal_work_stop_test` (seul, `RUN_SERIAL` dans `tests/serial_tests.cmake`) : chaque fil du
  plugin, puis les quatre dans l'ordre de `ReleaseWorld` ; une lecture de sol oubliée à la
  téléportation suivante sort aussitôt. Ses médianes ont le même but de 100 ms, vérifié par
  `unreal_work_stop_test_bench`.
