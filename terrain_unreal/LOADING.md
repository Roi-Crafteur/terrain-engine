# Le chargement au spawn et à la téléportation (agent 38)

Le joueur apparaît (début du jeu, téléportation, point préparé) : le monde autour de lui doit se
montrer tout de suite et du plus proche au plus loin, sans jamais le laisser tomber dans le vide.

## Ce qui se passe

1. **Le sol d'abord.** Les tuiles sont demandées et intégrées du plus proche au plus loin
   (`selectTiles`, `loadsBefore`). Au spawn, les 3 × 3 tuiles LOD 0 sous le pion sont générées
   avant qu'il bouge. Après une téléportation, le pion est **tenu** (son mouvement coupé, pas de
   gravité) jusqu'à ce qu'un tracé vers le bas touche une tuile, aussi longtemps qu'il le faut.
2. **Le renfort.** Pendant le chargement, il y a plus de fils de tuiles et plus de morceaux
   d'horizon. Ensemble, ils ne dépassent pas un plafond : `min(fils matériels − 4, 12)` par défaut,
   soit 12 sur un Ryzen 9 9950X3D. Les envois par image suivent un budget de 6 ms au coût moyen d'un
   envoi (de 8 à 32 tuiles). Ensuite, retour au régime normal (`WorkerThreads`, `HorizonThreads`,
   `MaxTileUploadsPerFrame`).
3. **L'horizon niveau par niveau.** Le niveau 0 (jusqu'à 1 km) vient d'abord, puis les niveaux 1
   à 3 (jusqu'à 8 km), puis les autres. Chaque niveau apparaît en fondu tramé en 0,6 s. Tant que
   des niveaux plus lointains manquent, le bord extérieur du dernier niveau visible se dissout dans
   le ciel : pas de bord net. Après une téléportation, les niveaux de l'ancien lieu sont cachés
   aussitôt, et l'époque en cours pour l'ancien lieu est abandonnée sans attendre.
4. **Le fondu au noir.** L'écran reste noir jusqu'à ce que soient montrés le sol (avec sa
   collision), l'anneau de 64 m, toutes les tuiles et les niveaux 0 à 3 de l'horizon, **2 s au
   plus**. Si le plafond est atteint, le log dit ce qui manquait, avec les temps. Le retour à
   l'image dure 0,25 s.
5. **Le contrôle de déterminisme** tourne sur un fil dès le départ : plus d'image figée d'environ
   1 s avant le spawn. S'il trouve un autre monde, le streaming s'arrête, avec la raison dans le
   log.

Rien de tout cela ne change le monde : mêmes tuiles, mêmes échantillons, mêmes bits. Seuls l'ordre,
le rythme et l'affichage changent. Les preuves sont `stream_loading_test`,
`horizon_progressive_test`, `unreal_horizon_progressive_test`, les goldens, `mountain_freeze_test`
et la somme du niveau Planète.

## Réglages (`Terrain World` → `Loading Settings`)

| Réglage | Défaut | Rôle |
|---|---|---|
| `bBoost` | oui | renfort pendant le chargement |
| `ThreadCap` | 0 | plafond des fils de tuiles et des morceaux d'horizon ensemble ; 0 : `min(fils − 4, 12)` |
| `UploadMillisecondsPerFrame`, `MaxUploadsPerFrame` | 6 ms, 32 | envois d'une image pendant le chargement |
| `bFade`, `FadeMaxSeconds` | oui, 2 s | fondu au noir et son plafond |
| `TimeoutSeconds` | 30 s | fin du renfort, quoi qu'il reste à charger |
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

Le CSV des tuiles est dans `Saved/TerrainEngine/Loading/load_<date>_<N>_<raison>.csv`. Colonnes :
`chunk_x, chunk_z, lod, distance_m, thread, generations, generation_start_s, generation_end_s,
generation_ms, converted_s, shown_s, upload_ms, collision`.

Le banc du conteneur se lance ainsi :
`unreal_loading_bench --planet <fichier> --at x z --teleport x z --cap n [--all-at-once]`.
Pour l'horizon niveau par niveau sur un fil : `--levels`.

## Mesuré

**Avant (Unreal, 9950X3D, planète, réglages d'avant ; mesures de l'orchestrateur)**

| Chargement | sol | 64 m | toutes les tuiles | horizon | détail |
|---|---|---|---|---|---|
| spawn | 0,010 s | 0,111 s | 0,374 s | 15,3 s | 24,5 s |
| chute | 0,073 s | 0,167 s | 0,507 s | 24,4 s | 51,4 s |
| K2 | 0,060 s | 0,152 s | 0,683 s | 8,3 s | 17,8 s |
| chaîne | 0,029 s | 0,082 s | 0,338 s | 14,7 s | 25,1 s |
| plaine | 0,031 s | 0,084 s | 0,268 s | 3,7 s | 9,1 s |
| origine | 0,026 s | 0,076 s | 0,264 s | 12,9 s | 19,4 s |

L'envoi d'une tuile prend 0,05 à 0,13 ms en médiane, 9 ms au plus (au K2). La collision est donc
gardée synchrone : elle ne coûte pas cher, et le budget en ms par image amortit les tuiles lourdes.

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
- À la chute, même le niveau 1 coûte cher (5,6 s sur un fil) : le fondu peut y atteindre son
  plafond de 2 s avant les niveaux 0 à 3. Le log le dit ; ensuite, ces niveaux arrivent en fondu.
- Avec le personnage GASP, `Terrain.LoadTest … -59` a regardé le ciel. La conversion est juste et
  testée (`unreal_load_phase_test`) : c'est la caméra du personnage qui lit l'inclinaison
  autrement. Le pion des outils de l'agent 36 (`TeleportView`) pose l'œil exactement.
- L'horizon CPU (`HorizonRenderer` = CPU) montre toujours tous ses niveaux d'un coup.
