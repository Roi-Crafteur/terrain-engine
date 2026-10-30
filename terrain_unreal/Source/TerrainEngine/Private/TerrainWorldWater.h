#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformMemory.h"

#include "Bridge/planet_source.hpp"

#include <string>

// The planet level of the session in the log (agent 27): read from its file, or built — a
// warning with the reason (missing, stale, damaged) —, with the time it took and the memory of
// the process before and after (TerrainWorldWater.cpp).
void LogPlanet(const terrain::unreal::PlanetSource& Source, const std::string& File,
	const FPlatformMemoryStats& Before);
