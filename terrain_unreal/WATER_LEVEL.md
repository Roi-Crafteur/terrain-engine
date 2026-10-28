# Le niveau de l'eau dans Unreal (phase 11, tâche 7b)

Le core sait où est l'eau et à quel niveau (mer, lacs, fleuves, chutes) ; Unreal le reçoit
maintenant aux sommets des tuiles et dans les textures de l'horizon. **Aucun matériau ne le lit
encore** : l'image ne change pas tant que le matériau ne le branche pas. Il faut un fichier
planète (`PlanetFile`, section 5 du [README](README.md)) : sans lui, pas de fleuves ni de lacs,
seulement la mer.

**Tuiles et horizon CPU : le canal UV 0 de chaque sommet** (`TexCoord[0]` dans le matériau)
- `x` : la hauteur de l'eau au-dessus du sommet, en mètres ; négative quand le sommet est
  au-dessus du niveau qu'on lui donne (une berge entre une colonne mouillée et une sèche, un bloc
  posé dans un lac) ;
- `y` : 1 sous l'eau (mer, lac, fleuve), 0 au sec (et `x` vaut 0).
- Le niveau, en cm d'Unreal : le Z monde du sommet (avant tout World Position Offset) +
  100 × `x`. Pour une surface d'eau tirée du même maillage : WPO = (0, 0, 100 × `x`), masquée
  où `y` < 0,5.
- Pourquoi pas le niveau lui-même : les UV d'un ProceduralMeshComponent sont des demi-flottants,
  qui arrondiraient 1 725 m au mètre et 6 000 m par pas de 4 m. La hauteur au-dessus du sommet garde
  le centimètre près des rives (le mètre seulement au-delà de 1 000 m de fond).
- Sur une colonne du core (presque tous les sommets du sol) : exactement le niveau du core ; sur
  une arête entre deux colonnes : leur niveau interpolé, ou celui de la colonne mouillée. Avec les
  grottes : la surface seulement, jamais les parois sous un lac.
- Horizon CPU : les jupes, le carré de mer et le sommet des bornes restent au sec ; la bordure
  cousue au niveau plus grossier prend sa hauteur d'eau depuis l'altitude qu'elle montre.

**Horizon GPU : une texture d'eau par niveau**
- 257 × 257 texels `PF_G16` (R16 UNORM), au plus proche, bouclée et adressée comme la texture des
  texels (l'échantillon (i, k) de la fenêtre au texel (premier emplacement + (i, k)) mod 257),
  écrite par bandes en même temps qu'elle (`Bridge/horizon_textures.hpp`).
- Paramètres de texture des matériaux de l'horizon : `HorizonWater` (le niveau) et `CoarseWater`
  (le niveau plus grossier, pour le géomorphing), comme `HorizonTexels` et `CoarseTexels` : des
  Texture Object Parameters, sampler *Linear Color*, lus par `Load`.
- Lecture : `code = round(T.Load(int3(slot, 0)).r * 65535)` ; `code` 0 : pas d'eau ; sinon le
  niveau en mètres vaut −4 096 + (code − 1) × 0,25, de −4 096 à 12 287,5 m (la mer : code 16 385,
  0 m exactement), à 0,125 m au plus du core. En cm d'Unreal : 100 × le niveau, puis la même
  descente (`HorizonDropCm`) et la même courbure que la hauteur du texel.
- Les texels ne changent pas : leur 4e flottant garde le code d'eau (≥ 128 sous un lac ou un
  fleuve, le sol sous la mer, négatif, sur l'océan).

**Mémoire** (10 niveaux, K = 256) : les échantillons du core passent de 16 à 20 octets
(10,1 → 12,6 Mo côté CPU) ; les textures d'eau ajoutent 1,26 Mo sur le GPU, soit 2 octets pour
les 16 d'un texel (+12,5 % d'octets par bande envoyée). Aux sommets, rien de plus dans Unreal : le
ProceduralMeshComponent garde déjà 4 canaux UV par sommet (à confirmer avec `stat memory`) ; le
Bridge ajoute 8 octets par sommet pendant la conversion.

**Limites**
- Au bord de l'eau, un sommet sur une colonne sèche n'a pas de niveau : seuls les sommets d'arête
  prennent celui de la colonne mouillée voisine ; le matériau masque avec `y`.
- Sur l'horizon, les crêtes relevées (`HorizonCrestRatio`) peuvent poser un sol au-dessus de son
  lac ; les modifications des joueurs ne changent pas le niveau.
- Un chemin relatif dans `PlanetFile` part du dossier de travail du processus : mieux vaut un
  chemin absolu.
