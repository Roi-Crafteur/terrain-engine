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
   - dossier de travail `C:/TerrainVerify`.
3. `workRoot` ne doit pas être sous `%TEMP%` (le lanceur le refuse) ni contenir d'espace.

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
- `--dry-run` : imprime toutes les commandes sans rien lancer ni écrire.

Le résultat va dans `C:\TerrainVerify\runs\<date>_main_vs_57\` :

| Fichier | Contenu |
|---|---|
| `resume.txt` | les alertes d'abord, puis pour chaque version : build, planète (source, somme), scripts de matériaux, erreurs de shader et Python, contrôle du déterminisme, sessions, bruit de chaque vue avec elle-même, marches, sol et eau du core à chaque lieu ; puis les écarts de pixels avant / après |
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
     (environ 2 min) ;
   - **les planètes de l'utilisateur ne sont jamais écrites**.
   
   Le pilote règle toujours `PlanetFile` du Terrain World de la carte lui-même : la copie, ou
   vide pour un lieu sans planète.
4. **Les sondes** : le sol et l'eau du core à chaque lieu donnent la hauteur de l'œil dans
   Unreal, et le sol le long de chaque marche sert à la comparer. Pour une version antérieure à
   `terrain_probe`, le lanceur lit la sortie de `terrain_probe_water`.
5. **Les sessions d'Unreal**, courtes : l'éditeur s'ouvre sur la carte de test avec le pilote
   (`ue_driver.py`), 6 lieux au plus par session (`--places-per-session`).
   - Les scripts de matériaux de la version sont relancés dans l'ordre de
     `terrain_unreal/WATER_LEVEL.md`.
   - Pour chaque lieu, en Play In Editor :
     - **la vue est placée** : une `CameraActor` à l'œil, avec `CoreOriginX/Z` sur le lieu (yaw =
       cap − 90, l'inclinaison telle quelle, négative vers le bas). Si la version a les outils
       de l'agent 36, ce sont `Terrain.Goto` et `Terrain.Look`, avec `Terrain.View normal`
       avant chaque image ;
     - **le chargement est attendu** : la ligne `Terrain World: load N report` (agent 38) si la
       version l'écrit, sinon des délais (`--settle`, `--first-settle`) ;
     - **le jeu est mis en pause** avant les images : le temps de l'eau et des chutes s'arrête ;
     - **deux images** sont prises à 2 s d'écart. Leur écart est le bruit qui reste à monde
       identique ;
     - **les marches** se font jusqu'à la première chute.
   - Le pilote ne sauvegarde **jamais** la carte ni un asset, et remet le Terrain World comme il
     était.
6. **Le chien de garde** :
   - le pilote écrit un battement de cœur chaque seconde ;
   - sans battement pendant trop longtemps, après une session trop longue, ou si l'éditeur ne se
     ferme pas, le lanceur tue tout l'arbre des processus d'Unreal (`taskkill /T /F`) ;
   - **aucun réglage d'alimentation, de veille ou d'affichage de Windows n'est touché** ;
   - `--offscreen` lance Unreal en `-RenderOffScreen`. Cette option n'a jamais été essayée.

## Le contrôle de l'inclinaison

La première vue de chaque session est d'abord prise vers le bas (−30°). Le pilote note
l'inclinaison que la caméra dit montrer, et le résumé mesure la part de ciel de l'image.

- Si la caméra montre une inclinaison du signe opposé, ou surtout du ciel, `resume.txt` commence
  par « inclinaison inversée ? ».
- Si l'inclinaison n'est pas celle demandée, le résumé le dit : la caméra du pion écrase peut-être
  la rotation.

## Les jonctions

`linkContent` (dans `machine.json`) liste des dossiers de `Content` à lier par `mklink /J` plutôt
qu'à copier : de gros paquets de textures, par exemple. **Un dossier lié pointe vers le vrai
projet de l'utilisateur.**
- Le lanceur refuse donc de lier `TerrainEngine` (les scripts de matériaux y écrivent : leur
  `PACKAGE_PATH`, lu dans les scripts de la version) et le dossier de la carte (`TerrainTest`).
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
