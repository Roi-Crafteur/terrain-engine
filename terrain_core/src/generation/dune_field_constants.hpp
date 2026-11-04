// The constants of the dunes of the realistic ergs (dune_field.cpp).
#pragma once

namespace terrain::dune_field {

// Heights: kHeightLow to kHeightLow + kHeightRange (sand^1.5), times kAmpBase + kAmpRange noise,
// within kHeightLow and kHeightHigh.
constexpr double kHeightLow = 20.0;
constexpr double kHeightRange = 280.0;
constexpr double kHeightHigh = 300.0;
constexpr double kAmpBase = 0.92;
constexpr double kAmpRange = 0.25;
// The bare floor between the dunes: kGapLow of the spacing where the sand is plentiful, kGapLow +
// kGapRange where it is scarce.
constexpr double kGapLow = 0.06;
constexpr double kGapRange = 0.35;
// The small dunes on the backs of the draas: spacing, height, angle to the wind, and the meters
// they keep clear of the crest and the slip face of the draa.
constexpr double kSmallSpacing = 320.0;
constexpr double kSmallHeightBase = 6.0;
constexpr double kSmallHeightShare = 0.04;
constexpr double kSmallTurn = 0.07;
constexpr double kSmallClear = 60.0;
// The troughs between the ridges and the chains sink this share of their height below the floor.
constexpr double kDig = 0.4;
constexpr double kDigStart = 0.5;   // of the half width of the plinth of the chains
constexpr double kDigSpacing = 0.08;
constexpr double kDigTan = 0.25; // the corridors at most 14 degrees
constexpr double kSmallGap = 0.15;
// Seifs: their spacing over that of the ridges, their width at most at their base (share of the
// spacing), their ends and the side of their slip faces.
constexpr double kSeifSpacing = 1.4;
constexpr double kSeifWidth = 0.8;
constexpr float kEndsLow = -0.25f;
constexpr float kEndsHigh = -0.05f;
constexpr float kSlipSoft = 0.25f;
constexpr double kSeifSinuous = 1.2;  // their crests wander a little more than the ridges
constexpr double kPlinthHeight = 0.45; // the plinth under their crest, share of their height
constexpr double kPlinthWidth = 0.38;  // and its half width, share of their spacing
constexpr double kSeifSand = 0.7;      // their height at least that of this sand
// Their offset from the crest in at most kStretched times their nominal spacing (seifLine).
constexpr double kStretched = 1.25;
// The second system of seifs (the branches): its spacing over the first, where it lies (a noise
// sampled elsewhere) and how much of it.
constexpr double kBranchSpacing = 1.27;
constexpr double kBranchShift = 3'333.0;
constexpr float kBranchLow = -0.1f;
constexpr float kBranchHigh = 0.25f;
// The branches wander kBranchSinuous times as much as the seifs (straight along the wind over
// kilometers, they read as lines) and join them over kBranchSoft of the height of the seifs.
constexpr double kBranchSinuous = 1.8;
constexpr double kBranchSoft = 0.05;
// The stars on the chains: their lattice, their height over the chain (share of its height),
// how many cells hold one, and how high on the chain their center must stand (within
// kOnChainSoft).
constexpr double kChainStarCell = 2'600.0;
constexpr double kChainStarShare = 0.65;
constexpr double kChainStarOccupancy = 0.75;
constexpr double kOnChain = 0.6;
constexpr double kOnChainSoft = 0.1;
constexpr float kChainCoherenceLow = 0.15f;
constexpr float kChainCoherenceHigh = 0.45f;
// Barchans: their cells across the wind (share of the spacing), their presence (an occupancy
// against a noise) and where their crest lies along the row.
constexpr double kBarchanCell = 0.75;
constexpr double kBarchanOccupancy = 0.85;
constexpr float kPresenceSoft = 0.35f;
constexpr double kScatterDraw = 1.6;   // the draw of a barchan: 0.5 + this times the scatter noise
constexpr double kScatterHeight = 0.5; // the shortest barchans: this share of the height
constexpr double kBarchanLeast = 30.0;
constexpr double kBarchanMost = 60.0;
constexpr double kCrestBase = 0.55;
constexpr double kCrestRange = 0.3;
constexpr double kScatterShift = 7'777.0; // meters: an independent sample of the scatter noise
// Each barchan its own size (a noise at the scale of one, Impl::sizes): from kSizeLeast of the
// usual width and height to all of them, about kSizeMean on average; and its own place across its
// cell, slid by up to kSlide of the room its width leaves there.
constexpr double kBarchanHalfShare = 0.26; // dune_forms: their half width over their cell
constexpr double kSizeLeast = 0.3;
constexpr double kSizeSpread = 1.4; // the size: kSizeMean + this times half the noise, clamped
constexpr double kSizeMean = 0.7;
constexpr double kSizeVolume = 0.414; // the mean of the cube of the size (width times height^2)
constexpr double kSlide = 0.8;
// Stars: their lattice, their height (the tallest dunes: at least kStarLeast) and their
// occupancy.
constexpr double kStarCell = 2048.0;
constexpr double kStarLeast = 150.0;
constexpr double kStarHigh = 250.0;
constexpr double kStarOccupancy = 0.35;
constexpr double kStarOccupancySand = 0.4;
// Around the defects of the phase (two crests joining), the dunes fade between these coherences.
constexpr float kCoherenceLow = 0.03f;
constexpr float kCoherenceHigh = 0.3f;
// The crests of the ridges break in saddles where the noise of the sides (sampled kBreakShift
// away, 3 km) is low, about a fifth of their length below half their height: no crest runs on
// for tens of kilometers (a straight band to the horizon, seen along it).
constexpr float kBreakLow = -0.45f;
constexpr float kBreakHigh = 0.05f;
constexpr double kBreakShift = 5'555.0;
// The breaks fade out from kBreakFull to kBreakNone meters of height: lowering a tall crest over
// a few hundred meters would make it steep along its length (the draas keep their long ridges
// and their small dunes); the crest falls by at most about 22 degrees along it.
constexpr double kBreakFull = 80.0;
constexpr double kBreakNone = 180.0;
constexpr double kBreakMean = 0.78; // the mean of the ramp of the breaks over the noise (measured)
// The dunes fade out over the first kEdgeFade of the mask.
constexpr float kEdgeFade = 0.2f;

} // namespace terrain::dune_field
