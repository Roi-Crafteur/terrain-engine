# Le chargement au spawn et à la téléportation (agent 38)

Le joueur apparaît (début du jeu, téléportation, point préparé) : le monde autour de lui doit se
montrer tout de suite et du plus proche au plus loin, sans jamais le laisser tomber dans le vide.

## Ce qui se passe

1. **Le sol d'abord.** Les tuiles sont demandées et intégrées du plus proche au plus loin
   (`selectTiles`, `loadsBefore`). Au spawn, les 3 × 3 tuiles LOD 0 sous le pion sont générées
   avant qu'il bouge. Après une téléportation, le pion est **tenu** (son mouvement coupé, pas de
   gravité) jusqu'à ce qu'un tracé vers le bas touche une tuile, aussi longtemps qu'il le faut. Le
   sol du point d'arrivée est lu sur un fil (jusqu'à 202 ms la première fois dans une région, à
   cause de l'hydrologie) ; le pion est posé dès qu'il est lu, au-dessus du sol ou de l'eau du
   core s'il y en a (lac, rivière, mer).
2. **Le renfort.** Pendant le chargement, il y a plus de fils de tuiles et plus de morceaux
   d'horizon. Ensemble, ils ne dépassent pas un plafond : `min(fils matériels − 4, 12)` par défaut,
   soit 12 sur un Ryzen 9 9950X3D, et jusqu'à 32 envois par image. Ensuite, retour au régime normal
   (`WorkerThreads`, `HorizonThreads`, `MaxTileUploadsPerFrame`). Toujours, les envois d'une image
   tiennent dans 6 ms : le vrai temps est lu après chaque envoi, et l'image s'arrête dès qu'il
   atteint le budget, ou dès que l'envoi suivant, estimé, le ferait dépasser. Il y a au moins un
   envoi par image pour ne jamais bloquer : une estimation fausse ne fait dépasser que d'une tuile.
   L'estimation part d'une valeur prudente à chaque chargement (6,2 ms pour 830 sommets), monte
   tout de suite avec un envoi lourd et ne redescend que lentement ; les tuiles avec collision ont
   leur propre estimation. Une mise à jour pas finie continue à l'image suivante ; ses retraits
   attendent que tous ses envois soient faits, donc jamais de trou.
3. **L'horizon niveau par niveau.** Le niveau 0 (jusqu'à 1 km) vient d'abord, puis les niveaux 1
   à 3 (jusqu'à 8 km), puis les autres. Chaque niveau apparaît en fondu tramé en 0,6 s. Tant que
   des niveaux plus lointains manquent, le bord extérieur du dernier niveau visible se dissout dans
   le ciel : pas de bord net. Après une téléportation, les niveaux de l'ancien lieu sont cachés
   aussitôt, et l'époque en cours pour l'ancien lieu est abandonnée sans attendre.
4. **Le fondu au noir.** L'écran reste noir jusqu'à ce que soient montrés le sol (avec sa
   collision), l'anneau de 64 m, toutes les tuiles et les niveaux 0 à 3 de l'horizon, **2 s au
   plus**. Si le plafond est atteint, le log dit ce qui manquait, avec les temps. Le retour à
   l'image dure 0,25 s.
5. **Le monde se construit sur un fil au démarrage.** L'écran est noir dès la première image du
   Play : un voile noir couvre la fenêtre de jeu dès `BeginPlay`, avant toute autre chose, et
   reste jusqu'à la fin du fondu du spawn (le fondu de la caméra prend le relais pour le retour à
   l'image). La carte planétaire, le niveau Planète, l'hydrologie, les tuiles et leurs fils sont
   construits hors du fil du jeu, comme avant mais ailleurs. Pendant ce temps, le pion est tenu,
   l'éditeur ne gèle plus, et
   « Chargement du monde… » s'affiche sur le noir après `MessageDelaySeconds` (0,5 s). Ensuite,
   l'horizon, l'eau et le ciel démarrent sur le fil du jeu (environ 0,2 s pour les grilles de
   l'horizon), puis le chargement du spawn commence : son fondu et son plafond de 2 s comptent à
   partir de là. Le contrôle de déterminisme reste : il tourne sur un fil une fois la session prête
   et arrête le streaming s'il trouve un autre monde. Il prend 0,004 s, car la carte planétaire
   (0,93 s), qu'il partage avec la session (`Geography::shared`), est déjà là.

Rien de tout cela ne change le monde : mêmes tuiles, mêmes échantillons, mêmes bits. Seuls l'ordre,
le rythme et l'affichage changent. Les preuves sont `stream_loading_test`,
`horizon_progressive_test`, `unreal_horizon_progressive_test`, les goldens, `mountain_freeze_test`
et la somme du niveau Planète.

## Le démarrage, avant / après

Avant, tout le démarrage tenait le fil du jeu : l'éditeur gelait environ 2,3 s au premier Play
(mesure dans Unreal : contrôle de déterminisme 0,97 s, c'est-à-dire la carte planétaire, lecture de
la planète 0,6 s, hydrologie, grilles de l'horizon) et environ 1,2 s aux suivants (carte déjà en
mémoire). Après, seuls l'horizon, l'eau et le ciel (environ 0,2 s) restent sur le fil du jeu, une
fois la session prête ; avant, les images continuent, noires.

Dans le conteneur (Linux, 4 cœurs), la session construite sur son fil prend 2,9 s avec la planète
(5,1 s à la toute première lecture du fichier) et 0,9 s sans elle. Dans Unreal, deux lignes du log
le mesurent : `the world built on a thread in … s …, worst frame … ms` (la pire image pendant la
construction) et `start-up in … s: …, then the horizon, the water and the sky in … s on the game
thread`.

| Premier Play (9950X3D, planète) | Fil du jeu gelé | Pire image pendant la construction |
|---|---|---|
| avant | environ 2,3 s | 2 495 ms (l'image du démarrage) |
| après | aucun gel : session prête en 2,16 s (1,04 s aux Play suivants), puis une image de 271,7 ms (horizon, eau, ciel, et 32 envois en 11,2 ms) | 128 ms |

Au premier Play, une image de ciel vide passait encore avant le noir (vers 0,43 s) : le noir
n'était posé qu'au premier tick du terrain. Le voile noir posé dans `BeginPlay` la supprime :
vérifié par une capture du bureau toutes les 30 ms, noir dès la première image du Play (0,165 s),
stable, puis un fondu de 0,25 s vers un décor complet.

## Réglages (`Terrain World` → `Loading Settings`)

| Réglage | Défaut | Rôle |
|---|---|---|
| `bBoost` | oui | renfort pendant le chargement |
| `ThreadCap` | 0 | plafond des fils de tuiles et des morceaux d'horizon ensemble ; 0 : `min(fils − 4, 12)` |
| `UploadMillisecondsPerFrame` | 6 ms | les envois d'une image tiennent dans ce temps, mesuré après chaque envoi (au moins 1), toujours ; 0 : le nombre seul |
| `MaxUploadsPerFrame` | 32 | envois d'une image au plus pendant le chargement |
| `bFade`, `FadeMaxSeconds` | oui, 2 s | fondu au noir et son plafond |
| `TimeoutSeconds` | 30 s | fin du renfort, quoi qu'il reste à charger |
| `MessageDelaySeconds` | 0,5 s | « Chargement du monde… » sur le noir si le monde se construit encore |
| `bHorizonLevelByLevel` | oui | horizon niveau par niveau ; non : tous les niveaux d'un coup, comme avant |
| `HorizonRevealSeconds` | 0,6 s | durée de l'apparition d'un niveau |

Le fondu tramé et la dissolution du bord demandent de relancer
`Scripts/create_horizon_gpu_material.py`. Sans cela, l'ancien matériau montre chaque niveau d'un
coup, sans bord dissous ; le reste marche.

## Fonctions

- `TeleportWhenReady(Pawn, X, Z, Cap, Inclinaison)` : écran noir, pion posé et tenu, chargement,
  puis retour de l'image. Cap 0 = nord, 90 = est ; inclinaison négative = vers le bas. Le lacet
  d'Unreal vaut cap − 90, comme les lieux des outils de l'agent 36.
- `PrepareLocation(X, Z)` : les tuiles et l'horizon sont calculés autour du point. Le délégué
  `OnTerrainLocationReady(X, Z, Secondes)` est appelé une fois les tuiles et les niveaux 0 à 3
  montrés. `ReleasePreparedLocation()` rend le foyer au joueur.
- Une téléportation faite autrement (un simple `SetActorLocation`, à plus de `ViewDistanceChunks`
  d'un coup) est reconnue : renfort, fondu et pion tenu de la même façon.
- `Terrain.LoadTest x z [cap] [inclinaison]` (`LoadTest`) : `TeleportWhenReady` du pion du
  joueur, avec mesure.

## Les mesures

Chaque chargement écrit dans `LogTerrainEngine` :
- `load N (spawn|teleport|prepared point) started at (x, z)` au départ ;
- une ligne par jalon dès qu'il est atteint : `ground`, `ground collision`, `64 m`, `256 m`,
  `all tiles`, `horizon 0-3`, `horizon all`, `horizon detail` ;
- `back to the normal regime after …`, puis `load N report` : jalons, courbe par tranche de
  distance, fils, envois et images.

Le CSV des images est à côté, `…_frames.csv`, une ligne par image du chargement : `t_s,
game_thread_ms` (l'image entière, lue par `stat unit` une image plus tard), `terrain_ms` (le tick
du `Terrain World`), `tiles_ms` (mise à jour du streamer : intégrations, conversions), `uploads,
uploads_ms` (maillages et collisions), `horizon_ms` (mise à jour de l'horizon : époques,
texels), `textures_ms` (textures envoyées, niveaux placés), `horizon_levels`. Le résumé du log
donne la pire image et ses parts (`worst frame: …`).

Le CSV des tuiles est dans `Saved/TerrainEngine/Loading/load_<date>_<N>_<raison>.csv`. Colonnes :
`chunk_x, chunk_z, lod, distance_m, thread, generations, generation_start_s, generation_end_s,
generation_ms, converted_s, shown_s, upload_ms, collision`.

Le banc du conteneur se lance ainsi :
`unreal_loading_bench --planet <fichier> --at x z --teleport x z --cap n [--all-at-once]`.
Pour l'horizon niveau par niveau sur un fil : `--levels`.

## Mesuré

**Unreal, Ryzen 9 9950X3D, planète, avant → après** (mesures de l'orchestrateur ; après : renfort à
12, matériau corrigé ; secondes depuis l'apparition, « noir » : durée du fondu)

| Chargement (x, z ; sol) | sol | 64 m | toutes les tuiles | horizon 0-3 | horizon entier | détail | noir |
|---|---|---|---|---|---|---|---|
| spawn (0, 0 ; −0,4 m) | 0,010 → 0,019 | 0,111 → 0,067 | 0,374 → 0,907 | 15,32 (bloc) → 1,45 | → 9,50 | 24,48 → 10,87 | 1,45 |
| chute (−6 185 124, 155 984 ; 260,1 m) | 0,073 → 0,135 | 0,167 → 0,179 | 0,507 → 0,333 | 24,43 → 4,43 | → 13,80 | 51,36 → 27,05 | 2,0 (plafond) |
| vallée du K2 (416 857, −11 398 966 ; 4 268,7 m) | 0,060 → 0,028 | 0,152 → 0,049 | 0,683 → 0,120 | 8,32 → 0,81 | → 2,72 | 17,78 → 9,37 | 0,81 |
| chaîne (9 618 697, −11 025 112 ; 996,3 m) | 0,029 → 0,060 | 0,082 → 0,081 | 0,338 → 0,142 | 14,71 → 1,04 | → 9,89 | 25,12 → 11,17 | 1,04 |
| plaine (−827 392, −4 333 568 ; 455,9 m) | 0,031 → 0,028 | 0,084 → 0,050 | 0,268 → 0,113 | 3,69 → 0,48 | → 2,47 | 9,05 → 5,59 | 0,48 |
| origine (0, 0 ; −0,4 m) | 0,026 → 0,026 | 0,076 → 0,046 | 0,264 → 0,101 | 12,86 → 0,55 | → 8,30 | 19,42 → 9,35 | 0,55 |

Avant, l'horizon arrivait d'un bloc : « horizon 0-3 » y est l'horizon entier. Les altitudes du sol
sont celles du core avec la planète : la chute est à 260 m, la vallée du K2 à 4 269 m. Au spawn,
l'anneau de 64 m était à 0,602 s dans un passage et à 0,067 s dans l'autre : un à-coup. Le pion
n'est jamais tombé : la collision arrive dans la même image que le sol. Au film du spawn, les
montagnes sont complètes dès que le noir se lève (1,5 s), sans saut, trou ni bord d'anneau ensuite.

L'envoi d'une tuile prend 0,05 à 0,13 ms en médiane avant, 0,13 à 0,37 ms après (jusqu'à 32 par
image), 9 ms au plus (au K2). La collision est donc gardée synchrone : elle ne coûte pas cher, et
le budget en ms par image amortit les tuiles lourdes. Les pires images (299 ms au pied de la
chaîne) restent à expliquer avec le CSV des images.

À la chute, une image a fait 32 envois en 56,8 ms : le budget comptait alors des tuiles au coût
moyen d'un envoi. Une estimation par sommet seule ne suffisait pas non plus (essai de 7dcc52d) :
les premières images de chaque chargement dépassaient encore, 10,5 ms (32 envois) au spawn,
10,8 ms à la chaîne, 37,6 ms (22 envois) à la chute, car le coût appris sur les images d'avant
était trop bas pour les tuiles LOD 0 lourdes (environ 830 sommets, jusqu'à 6 ms chacune). Les
images suivantes restaient entre 1,5 et 2,9 ms. Désormais, le vrai temps arrête l'image
(`Bridge/upload_pacer`) : `unreal_upload_pacer_test` vérifie qu'une estimation 10 fois trop
basse ne fait jamais dépasser le budget de plus d'une tuile.

**Après, dans le conteneur** (Linux, 4 cœurs, donc 2 morceaux d'horizon seulement, planète) :
instant où chaque niveau de l'horizon est montré.

| Chargement | toutes les tuiles | niveau 0 | niveaux 0-3 | horizon entier |
|---|---|---|---|---|
| spawn (origine) | 0,12 s (avant 0,45) | 0,12 s | 1,7 s | 33,6 s (avant 26,8, d'un bloc) |
| chute | 0,62 s | 1,04 s | 18,2 s | 46,9 s |
| K2 | 0,15 s | 0,14 s | 2,5 s | 29,0 s |

L'horizon entier finit plus tard qu'avant dans le conteneur, pour deux raisons. Le détail des
niveaux déjà montrés démarre tout de suite et partage les 4 cœurs avec les morceaux. Et chaque
époque d'un seul niveau laisse un emplacement vide à sa fin. Sur 16 cœurs, avec le renfort à
12, ni l'un ni l'autre ne devrait compter : c'est à vérifier dans Unreal.

**Où part le temps de l'horizon** (`--levels`, un fil, avec la planète) : 74 s à l'origine et
111 s à la chute pour toute l'époque, dont 65 s pour les niveaux 8 et 9 (mailles de 2 et 4 km).
Sans la planète, 3,5 s. La cause est `Hydrology::courseOf` : il construit le tracé de chaque
tronçon de fleuve, et les niveaux grossiers (fenêtre de 1 000 km) les demandent presque tous.
C'est l'objet d'une tâche à part, sans changement de bits.

## Limites

- Le code Unreal n'est compilé que par Unreal.
- À la chute, même le niveau 1 coûte cher (5,6 s sur un fil) : le fondu y atteint son plafond de
  2 s avant les niveaux 0 à 3 (mesuré : 2 niveaux sur 10 à ce moment-là, les niveaux 0 à 3 à
  4,4 s). Le log le dit ; ensuite, ces niveaux arrivent en fondu.
- Avec le personnage GASP, `Terrain.LoadTest … -59` a regardé le ciel. La conversion est juste et
  testée (`unreal_load_phase_test`), et rien ici ne relit l'inclinaison : elle est donnée à
  `SetControlRotation` dans [−89,9 ; 89,9]. Unreal la garde ensuite entre 0 et 360° (−59 devient
  301) ; un code qui la borne à [−90 ; 90] sans la ramener d'abord dans [−180 ; 180] en fait +90 :
  c'est sans doute la caméra du personnage. Le pion des outils de l'agent 36 (`TeleportView`)
  pose l'œil exactement.
- Pendant la construction du monde au démarrage, `PrepareLocation` et `TeleportWhenReady`
  répondent « non » (pas encore de session) ; une construction du niveau Planète absent du disque
  (plusieurs minutes) se fait derrière le même écran noir, message affiché.
- L'horizon CPU (`HorizonRenderer` = CPU) montre toujours tous ses niveaux d'un coup.
- L'image d'une téléportation était longue à cause de la lecture du sol au point d'arrivée, sur
  le fil du jeu : 202,6 ms sur 202,8 au pied de la chaîne, 32,3 ms sur 32,5 à la chute (la
  première lecture d'une région construit son hydrologie). Elle est maintenant faite sur un fil :
  `the teleport took … ms` ne compte plus que le déplacement et le début du chargement, et
  `the ground of (x, z) m read on a thread in … ms` dit quand le pion est posé.
- La chute (−6 185 124 ; 155 984) est au fond d'un bassin : son sol (260 m) est sous l'eau. Le pion
  est maintenant posé au-dessus de l'eau du core. Mais l'eau des tuiles n'a ni collision ni volume
  d'eau : une fois relâché, le personnage s'enfonce jusqu'au fond. Pour qu'il reste à la surface ou
  nage, il faut une collision ou un volume d'eau (le rendu de l'eau), ou un point d'arrivée au sec
  (la recherche de sol de `TeleportView`).
