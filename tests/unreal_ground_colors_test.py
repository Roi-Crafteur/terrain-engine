"""The color of the ground of each biome, as the temporary terrain material draws it far away
(agent 49): a copy in Python of its formula (temporary_terrain_hlsl.py: SHADE_TOP, LAYER_FAR and
SHADE_BOTTOM, the path of the horizon and of the tiles beyond their detail: the mean color of each
texture, the fallback means of temporary_terrain_look.LAYERS), on flat ground, its noises at their
mean, without the rare patches, for one texel of a biome and a material (Layers).

Checks what Unreal showed wrong at 19b6f8f: the gravel of the polar region and of the cold desert
drawn as the warm orange of the Sahara.
- the gravel (material 8) of the cold biomes is grey to grey-brown (sRGB red under 1.4 times
  blue), the reg of the rocky desert warm (over 1.6 times; the orange of 19b6f8f: 1.8);
- the grounds Unreal drew alike are apart: the garrigue, the shrubland, the sand desert and the
  cold desert two by two, the polar region and the sand desert (sRGB distance over 20), the two
  cold gravels (over 12).
Prints the color of the ten biomes of agent 49 at their views (data/dev/places.json), linear and
sRGB, for the comparison with the images of Unreal.

Usage: unreal_ground_colors_test.py <terrain_unreal>
"""

import pathlib
import sys

failures = []


def check(passed, what):
    if not passed:
        failures.append(what)
        print(f"check failed: {what}")


class Anything:
    """Stands for the module unreal, absent outside the editor."""

    def __getattr__(self, name):
        return Anything()

    def __call__(self, *args, **kwargs):
        return Anything()


GROUND = ["Grass", "DryGrass", "Forest", "Alpine", "Dirt", "Sand", "Desert", "Scree"]


def mul(a, b):
    return tuple(x * y for x, y in zip(a, b))


def lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def clamp01(x):
    return min(max(x, 0.0), 1.0)


def saturate3(c, s):
    """Look.Saturate: toward the luminance (Rec. 709) by s."""
    lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    return tuple(max(lum + (x - lum) * s, 0.0) for x in c)


def texel_layers(look, biome, material):
    """Layers of temporary_terrain_hlsl.py: the ground weights and (cold, rock) of one texel."""
    a, b, c, d = (list(part) for part in look.BIOMES[biome])
    blend = look.hlsl.blend_code
    zero = [0.0] * 4
    if material == 1:
        a, b = zero, [0, 0, 1, 0] if biome == 2 else [0, 1, 0, 0]
    elif material == 8:
        a, b = zero, list(lerp(blend.GRAVEL_COLD, blend.GRAVEL_WARM, d[0]))
    elif material in (3, 6, 10):
        a, b = [x * 0.3 for x in a], [x * 0.3 + y for x, y in zip(b, (0.7, 0, 0, 0))]
    elif material == 4:
        a, b, c = zero, [0, 0, 0, 1], [c[0], 1.0]
    elif material == 5:
        c = [1.0, c[1]]
    elif material in (7, 9):
        a, b = zero, [0, 0, 0, 1]
    return a + b, c


def ground_color(look, biome, material, erg=0.0):
    """The linear color of the ground of one texel, far away, on flat ground (erg: the share of a
    sea of dunes, its dunes full)."""
    means = {name: mean for name, _, _, mean, _ in look.LAYERS}
    tints, saturations, scalars = look.TINTS, look.LAYER_SATURATION, look.SCALARS
    weights, (cold_share, rock_share) = texel_layers(look, biome, material)
    desert = mul(means["Desert"], tints["Desert"])
    sand = mul(means["Sand"], tints["Sand"])
    scree = mul(saturate3(means["Scree"], saturations["Scree"]), tints["Scree"])
    dune_tint = lerp((1, 1, 1), tuple(x / max(y, 1e-4) for x, y in zip(desert, sand)), erg)
    reg_tint = lerp((1, 1, 1), tuple(0.55 * x / max(y, 1e-4) for x, y in zip(desert, scree)), erg)
    total = sum(weights)
    ground = (0.0, 0.0, 0.0)
    for k, name in enumerate(GROUND):
        if weights[k] <= 0.0:
            continue
        color = means[name]
        if name in saturations:
            color = saturate3(color, saturations[name])
        color = mul(color, dune_tint if name == "Sand" else reg_tint if name == "Scree" else
                    (1, 1, 1))
        ground = tuple(g + weights[k] / total * x for g, x in zip(ground, mul(color, tints[name])))
    hue = lerp((1.07, 1.02, 0.86), (0.9, 1.0, 1.08), 0.5)
    ground = mul(saturate3(mul(ground, hue), scalars["Saturation"]), look.WARMTH)
    rock_mean = lerp(mul(means["Rock"], tints["Rock"]), mul(means["RockAlt"], tints["RockAlt"]), 0.5)
    rock = tuple(x * scalars["RockAlbedo"] for x in saturate3(rock_mean, 1.0 - scalars["RockGrey"]))
    snow = mul(lerp(means["Snow"], (0.88, 0.91, 0.96), scalars["SnowWhiten"]), tints["Snow"])
    covered = lerp(ground, rock, rock_share * scalars["CoreRock"])
    return lerp(covered, snow, clamp01((cold_share - 0.5) * 2.5 + 0.5))


def srgb(color):
    def one(x):
        x = clamp01(x)
        return round(255 * (12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055))
    return tuple(one(x) for x in color)


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(srgb(a), srgb(b))) ** 0.5


# The views of the biomes of agent 49 (data/dev/places.json) with the material the core gives
# there (terrain_probe): 1 sand, 2 grass, 4 rock, 5 snow, 8 gravel; the share of a sea of dunes.
VIEWS = [("biome_desert_rocheux", 2, 8, 0.0), ("biome_foret_seche", 12, 2, 0.0),
         ("biome_steppe", 13, 2, 0.0), ("biome_desert_sable", 14, 1, 1.0),
         ("biome_desert_froid", 15, 8, 0.0), ("biome_haute_montagne", 16, 4, 0.0),
         ("biome_region_polaire", 17, 8, 0.0), ("biome_desert_glace", 18, 5, 0.0),
         ("biome_garrigue", 19, 2, 0.0), ("biome_broussailles", 20, 2, 0.0)]


def main(root):
    sys.path.insert(0, str(root / "Scripts"))
    sys.modules["unreal"] = Anything()
    import temporary_terrain_look as look  # noqa: E402  (after the fake module unreal)

    colors = {}
    for name, biome, material, erg in VIEWS:
        colors[name] = ground_color(look, biome, material, erg)
        c = colors[name]
        print(f"{name:22s} linear ({c[0]:.3f}, {c[1]:.3f}, {c[2]:.3f}) sRGB {srgb(c)}")
    # The gravel of the cold biomes (tundra 8, cold desert 15, polar region 17, ice desert 18):
    # grey to grey-brown, never the orange of the Sahara.
    for biome in (8, 15, 17, 18):
        c = srgb(ground_color(look, biome, 8))
        check(c[0] < 1.4 * c[2], f"the gravel of the biome {biome} is not grey: sRGB {c}")
    # The reg of the rocky desert stays warm.
    reg = srgb(ground_color(look, 2, 8))
    check(reg[0] > 1.6 * reg[2], f"the reg of the rocky desert is not warm: sRGB {reg}")
    # Grounds told apart (sRGB distance): what Unreal drew alike at 19b6f8f, the four beige
    # grounds two by two (garrigue and shrubland were 32 apart there, and still looked alike) and
    # the polar region against the sand desert; the two cold gravels less, both grey.
    beige = ("biome_garrigue", "biome_broussailles", "biome_desert_sable", "biome_desert_froid")
    pairs = [(beige[i], beige[j], 20.0) for i in range(4) for j in range(i + 1, 4)]
    pairs += [("biome_desert_sable", "biome_region_polaire", 20.0),
              ("biome_desert_froid", "biome_region_polaire", 12.0)]
    for first, second, least in pairs:
        gap = distance(colors[first], colors[second])
        check(gap > least, f"{first} and {second} are drawn alike (sRGB distance {gap:.1f})")
    print(f"ground colors: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(pathlib.Path(sys.argv[1])))
