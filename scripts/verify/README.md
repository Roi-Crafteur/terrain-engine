# Le lanceur de vérification (agent 37)

Une commande pour vérifier une version dans Unreal, ou la comparer à une autre. Elle :
- compile la bonne version dans un dossier neuf ;
- charge la planète ;
- place la caméra sur des lieux connus et prend les images ;
- fait marcher le personnage ;
- écrit une planche avant / après et un résumé.

La sonde du core (`terrain_probe`) et le fichier des lieux (`data/dev/places.json`) sont décrits
dans [`docs/dev/PROBE_AND_PLACES.md`](../../docs/dev/PROBE_AND_PLACES.md).

## Une fois par machine

1. Python 3.9 ou plus récent, git, CMake 3.20 ou plus récent, Visual Studio (le compilateur des
   outils du core), Unreal 5.8. Le projet hôte doit avoir le plugin **Python Editor Script
   Plugin** activé, déjà nécessaire aux scripts de matériaux. Pillow n'est pas nécessaire.
2. Copier `scripts/verify/machine.example.json` en `scripts/verify/machine.json` (ignoré par
   git). L'exemple est déjà rempli pour la machine de l'orchestrateur :
   - Unreal `C:/Program Files/Epic Games/UE_5.8` ;
   - projet hôte `C:/Projets/TerrainSandbox/TerrainSandbox.uproject`, cible
     `TerrainSandboxEditor Win64 Development` ;
   - carte `/Game/TerrainTest/TerrainGasp` ;
   - planètes réutilisables `C:/TerrainEngine/planet.bin` et
     `C:/Projets/TerrainSandbox/Saved/TerrainEngine/planet_20261019.bin` ;
   - dossier de travail `C:/TerrainVerify` ;
   - `linkContent` : `StylescapeHD` et `Characters`, liés au lieu d'être copiés (11 Go de
     moins par commit dans le cache).
3. `workRoot` ne doit pas être sous `%TEMP%` (le lanceur le refuse) ni contenir d'espace.
4. **cmake** : si `cmake` n'est pas dans le PATH, mettre son chemin dans la clé `cmake` de
   `machine.json`, par exemple le `cmake.exe` installé par pip. Le lanceur essaie d'abord cette
   clé, puis le PATH, puis le cmake de pip (`import cmake`).

## Vérifier une PR contre `main`

```bat
python scripts\verify\verify.py run --machine scripts\verify\machine.json --before main --after #57 --places chute388_bassin,fleuve_plaine,k2_paroi --sun falls --walk k2_paroi:225:80
```

- `--before`, `--after` : une branche, une étiquette, un commit ou `#<numéro de PR>`. Sans
  `--after`, une seule version est vérifiée.
- `--places` : des noms de `data/dev/places.json` séparés par des virgules (`all` : tous).
- `--walk lieu[:cap[:mètres]]` : une marche depuis le lieu, au cap du lieu par défaut, sur
  100 m par défaut. Elle s'arrête à la première chute.
- `--sun falls` : élévation 50°, yaw 116 (le soleil des vérifications de chutes). On peut aussi
  donner `élévation,yaw` ou `none`.
- `--dry-run` : imprime toutes les commandes sans rien lancer ni écrire. Le SHA est résolu
  quand même, en lecture seule : une branche, une étiquette ou une PR par `git ls-remote` du
  dépôt, un commit dans le miroir. Les dossiers du cache affichés sont les vrais. Une `NOTE` dit
  si le miroir est en retard ; un vrai lancement le met à jour d'abord.

**Code de sortie** :
- 1 pour une vraie panne, en tête de `resume.txt` sous « ATTENTION » : session tuée ou fermée
  avant la fin du pilote, résultat absent, vue ou marche absente, API manquante, erreur du pilote,
  plantage, version non compilée, projet de l'utilisateur modifié ;
- 0 sinon, même avec des avertissements (« AVERTISSEMENT » : contrôle de l'inclinaison douteux,
  planète reconstruite par Unreal) ;
- 2 pour une erreur de la commande (lieu inconnu, machine absente).

Le résultat va dans `C:\TerrainVerify\runs\<date>_main_vs_57\` :

| Fichier | Contenu |
|---|---|
| `resume.txt` | les alertes d'abord, puis le contrôle du projet de l'utilisateur, puis pour chaque version : build, planète (source, somme), scripts de matériaux, erreurs de shader et Python, contrôle du déterminisme, sessions, bruit de chaque vue avec elle-même, marches, sol et eau du core à chaque lieu ; puis les écarts de pixels avant / après |
| `planche.png` | une ligne par vue : avant, après, écarts ×4, avec la vue et ses chiffres au-dessus |
| `planche.html` | la même planche avec les images en taille réelle |
| `avant/`, `apres/` | `views/` (les images), `walks/` (les journaux CSV des marches), `logs/` (UBT, éditeur, sondes), `result_<n>.json` |
| `run.json` | tout ce que le résumé a lu |

## Ce que fait la commande

1. **Chaque version** est clonée depuis un miroir du dépôt, dans `<workRoot>/cache/<sha>/` :
   - une copie neuve du projet hôte, sans `Binaries`, `Intermediate`, `Saved` ni DDC ;
   - le clone du commit dans `Plugins/terrain-engine` ;
   - la compilation par UBT (`Build.bat`) ;
   - les outils du core, compilés par `scripts/verify/tools/CMakeLists.txt` : `terrain_core`
     seul, sans viewer, glad ni tests.
2. **Cache par commit** : un commit déjà compilé (marqueur `build_ok.json`) n'est pas recompilé.
   Une vérification « main contre branche » ne recompile pas `main` si son commit n'a pas changé.
   `--fresh` efface le cache des versions et recompile. Une compilation qui échoue n'est jamais
   gardée. On peut effacer `<workRoot>/cache/` à la main pour faire de la place.
3. **La planète, à côté** :
   - `terrain_probe --planet-check` de la version vérifie les planètes de `machine.json`, puis
     celles déjà construites dans `<workRoot>/planets/` : version, seed, monde, somme ;
   - la première acceptée est **copiée** dans le projet de la version ;
   - sinon, `terrain_make_planet` de la version la construit dans `<workRoot>/planets/`
     (environ 2 min). Sa somme est gardée à côté (`<fichier>.checksum`), pour le run suivant ;
   - **les planètes de l'utilisateur ne sont jamais écrites**.
   
   Le pilote règle toujours `PlanetFile` du Terrain World de la carte lui-même : la copie, ou
   vide pour un lieu sans planète.
4. **Les sondes** : le sol et l'eau du core à chaque lieu donnent la hauteur de l'œil dans
   Unreal, et le sol le long de chaque marche sert à la comparer. Pour une version antérieure à
   `terrain_probe`, le lanceur lit la sortie de `terrain_probe_water`.
5. **Les sessions d'Unreal**, courtes : l'éditeur s'ouvre sur la carte de test, 6 lieux au plus
   par session (`--places-per-session`).
   - **Le démarrage** : le lanceur écrit `Content/Python/init_unreal.py` dans la copie du projet
     (jamais dans le tien), le script que le plugin Python lance à chaque ouverture de l'éditeur.
     Il ne fait rien sans les variables du lanceur ; avec elles, il démarre le pilote
     (`ue_driver.py`) sur le tick de l'éditeur, et l'éditeur reste ouvert jusqu'à ce que le
     pilote le ferme. Un `init_unreal.py` du projet est gardé en `init_unreal_project.py` et lancé
     d'abord. `-ExecutePythonScript` n'est jamais utilisé, car il ferme l'éditeur après son
     script. En secours, `--start execcmds` démarre le même code par `-ExecCmds="py …"`.
   - **L'API vérifiée d'abord** : toutes les fonctions de l'API Python dont la session a besoin
     sont vérifiées d'un coup, et celles qui manquent sont toutes nommées ensemble dans
     `result_<n>.json` et en tête du résumé. Sans celles des vues, la session s'arrête avant la
     PIE. Sans celles des marches ou du soleil, les vues sont prises quand même.
   - **L'éditeur prêt** : `init_unreal.py` tourne très tôt, avant que la carte soit ouverte. Le
     pilote attend donc le Terrain World dans le monde de l'éditeur, puis quelques ticks, avant
     les scripts de matériaux (phase `editor-ready` du battement de cœur, 10 min au plus).
   - Les scripts de matériaux de la version sont relancés dans l'ordre de
     `terrain_unreal/WATER_LEVEL.md`.
   - Pour chaque lieu, en Play In Editor :
     - **la vue est placée** :
       - si la version a les outils de l'agent 36 (détectés dans ses sources), par
         `Terrain.Goto x z <œil>` et `Terrain.Look cap inclinaison`, avec `Terrain.View normal`
         avant chaque image, une PIE pour tous les lieux du même besoin de planète ;
       - sinon, par une `CameraActor` posée dans la carte avant la PIE, avec `CoreOriginX/Z` sur
         le lieu : à l'œil, yaw = cap − 90, l'inclinaison telle quelle, négative vers le bas.
         Les fonctions de spawn différé ne sont pas exposées à Python dans UE 5.8 : la PIE
         duplique cette caméra avec la carte, et le pilote retrouve la copie par son étiquette
         (`TerrainVerifyCamera`). Le Terrain World charge les tuiles autour d'elle
         (`FocusActor`). La caméra est retirée de la carte après chaque lieu, sans
         sauvegarder ; une PIE par lieu ;
     - **le chargement est attendu** : la ligne `Terrain World: load N report` (agent 38) si la
       version l'écrit, sinon des délais (`--settle`, `--first-settle`). C'est le rapport du
       dernier chargement commencé depuis la mise en place de la vue (`load N (raison) started
       at (x, z) m`) : une téléportation rapporte d'abord le chargement précédent tel quel, et ce
       rapport-là ne compte pas. Le numéro, la raison et le point du chargement vont dans
       `result_N.json` ;
     - **le jeu est mis en pause** avant les images : le temps de l'eau et des chutes s'arrête ;
     - **deux images** sont prises à 2 s d'écart. Leur écart est le bruit qui reste à monde
       identique ;
     - **les marches** se font jusqu'à la première chute : le mode `Falling` pendant 0,3 s, ou
       les pieds descendus de 2 m d'un échantillon au suivant, quel que soit le mode lu. Le
       résumé compare les pieds du pion au sol du core sondé sous chaque échantillon, avec le Z
       de départ et de fin. Il donne aussi le cap réellement suivi, et dit si le pion a dévié
       de plus de 20° ou s'il est resté bloqué.
   - Le pilote ne sauvegarde **jamais** la carte ni un asset, et remet le Terrain World comme il
     était.
   - **Les ticks qui arrivent pendant une étape** sont ignorés : la reconstruction d'un matériau
     fait tourner l'éditeur pendant son script. Seul le battement de cœur est écrit, et une étape
     ne démarre jamais dans une autre. Chaque script de matériaux a son étape à lui.
   - **La fermeture** : le pilote ferme l'éditeur au tick qui suit sa fin, jamais depuis une
     étape. Son résultat est alors écrit, et le chien de garde tue l'éditeur s'il n'est pas fermé
     15 s plus tard.
6. **Le contrôle du projet de l'utilisateur**, sans git (le projet hôte n'est pas un dépôt) :
   - avant et après le run, la taille et la date d'écriture de chaque fichier du projet,
     dossiers liés compris, et le SHA256 des planètes de `machine.json` ;
   - `Binaries`, `Intermediate`, `Saved` et la DDC ne sont pas comptés : ton éditeur peut y
     écrire ;
   - `resume.txt` dit « intact », ou met en alerte les fichiers changés.
7. **Le chien de garde** :
   - le pilote écrit un battement de cœur chaque seconde ;
   - sans battement pendant trop longtemps, après une session trop longue, ou si l'éditeur ne se
     ferme pas, le lanceur tue tout l'arbre des processus d'Unreal (`taskkill /T /F`) ;
   - **aucun réglage d'alimentation, de veille ou d'affichage de Windows n'est touché** ;
   - `--offscreen` lance Unreal en `-RenderOffScreen`. Cette option n'a jamais été essayée.

## Le contrôle de l'inclinaison

À un lieu de la session, le premier sans eau sous l'œil s'il y en a un, le pilote prend la même
vue deux fois en petit, à +30° puis à −30°, et note chaque fois l'inclinaison que la caméra dit
montrer. Le résumé mesure la part de ciel des deux images : ciel bleu net, lisse ; ni l'écume
blanche ni la brume ne comptent.

- Le résumé commence par l'avertissement « inclinaison inversée ? » dans deux cas : la caméra
  dit une inclinaison du signe opposé, ou la vue vers le bas ne montre pas nettement moins de
  ciel que la vue vers le haut (15 points de moins au moins). Une image seule ne suffit pas :
  l'eau reflète le ciel, et l'écume d'un bassin trompait le contrôle de b7a3f0e.
- Si l'inclinaison n'est pas celle demandée, le résumé le dit : la caméra du pion écrase peut-être
  la rotation.
- Ce sont des avertissements, pas des pannes : ils ne changent pas le code de sortie.

## Les jonctions

`linkContent` (dans `machine.json`) liste des dossiers de `Content` à lier par `mklink /J` plutôt
qu'à copier : de gros paquets de textures, par exemple. **Un dossier lié pointe vers le vrai
projet de l'utilisateur.**
- Le lanceur refuse donc de lier `TerrainEngine` (les scripts de matériaux y écrivent : leur
  `PACKAGE_PATH`, lu dans les scripts de la version), `Python` (le lanceur y écrit
  `init_unreal.py`) et le dossier de la carte (`TerrainTest`).
- `StylescapeHD` et `Characters` (liés par défaut) ne sont jamais écrits, ni par les scripts ni
  par le pilote.
- Un cache effacé retire les liens sans jamais toucher à ce vers quoi ils pointent.

## La sonde sur des lieux nommés

```bat
python scripts\verify\verify.py probe --probe build\terrain_core\Release\terrain_probe.exe --planet C:\TerrainEngine\planet.bin chute388_bassin k2_paroi
```

## Tests

Les modules qui ne lancent pas Unreal sont testés par ctest (`verify_*_test`, Python unittest,
une dizaine de secondes en tout). Le pilote est testé contre un faux module `unreal`
(`tests/fake_unreal.py`). Sans ctest :

```bat
cd scripts\verify\tests
python -m unittest discover -p "test_*.py"
```

## Limites

- Le pilote n'a pas été lancé dans Unreal ici. Les noms de l'API Python d'UE 5.8 viennent de la
  documentation ; une API absente est nommée dans `result_<n>.json` et dans le résumé.
- Sans la ligne de chargement de l'agent 38, l'attente est un délai fixe.
- Seul Windows est visé. Les commandes de Linux et Mac (`Build.sh`) sont prévues, jamais
  essayées.
