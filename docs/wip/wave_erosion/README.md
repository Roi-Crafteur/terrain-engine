# Sine-wave erosion (set aside at step 3b, for step 6)

The Big Globe sine-wave erosion (Builderb0y/BigGlobe, branch scriptable-generators,
`bigglobe_script_templates/sine_wave_erosion.json`), tried in place of the ravines of the
mountains at step 3b. The orchestrator chose the line ravines and the rock for step 3b: the
erosion looks clearly better, but costs too much for the streaming of the tiles. It is to be
proposed again at step 6, with an evaluation of several points at once or on the GPU.

## Code on this branch

- `terrain_core/src/generation/wave_erosion.{hpp,cpp}`: waves from the longest wavelength to the
  shortest, `perOctave` per octave, each turned by the golden angle; weight
  `((u x g) * rcp)^2`; phase shifted by `-8 * sum * (u . g) * rcp`; derivative added to the
  gradient. Adaptations: the waves are the nearest waves of the torus of the world (periodic);
  the sine is a polynomial of +, - and * (deterministic, no FMA); the waves of a group of 8 see
  the same state (same pattern, 20 % faster).
- `GeographyParams::waveErosion` (amplitude, times `waveLongest`), `wavesPerOctave`,
  `waveLongest` (4,096 m), `waveShortest` (64 m); `plainWaves` (diagnosis on the plains).
- `heightfield_geography.cpp`: the gradient is that of the first two octaves of the ridged noise
  (`ridged2DSlope`, analytic), the erosion fades out like the ravines on the flat, near the
  crests and on the creases (else the gradient that turns over at the crests makes steps).

## Figures (this machine; ratios against the legacy relief)

| Variant | Mountain point | Mountain tiles LOD 0-3 |
|---|---|---|
| Ridged alone | 1.32 us | - |
| Line ravines + rock (step 3b) | 1.82 us | x1.28 to x1.47 |
| Waves, 16 per octave (amplitude x2) | 3.8 us | x2.9 to x3.2 |
| Waves, 32 per octave | 6.0 us | x4.5 to x4.9 |
| Waves, 64 per octave | 10.7 us | x7.0 to x9.1 |

Without the finest waves (64 to 256 m) the slopes turn soft again: they cannot be dropped to
save time. On the plains (weaker, from the slope of the hills) there was no visible gain at human
height, for a plain point of 2.9 us instead of 0.55 us.

## Images

- `ravines_slope.jpg`, `ravines_above.jpg`: the fixed cameras of checkpoint 2, step 3 ravines,
  step 3b, waves 16, 32 and 64 per octave, legacy.
- `zoom20_cmp.jpg`: 20 km zoom at the fixed mountain place.
- `waves_without_fine_octaves.jpg`: waves from 4,096 to 256 m only.
- `uniform_slope_hillshade.jpg`: the erosion alone on a uniform slope (64 per octave in groups of
  4 and 8, 16 per octave x2, 32 per octave x1.4, 64 per octave from 128 m, 16 per octave).
- `plain_waves.jpg`: the weaker erosion on the plains.
- `tile_bench.txt`: the interleaved tile bench.
