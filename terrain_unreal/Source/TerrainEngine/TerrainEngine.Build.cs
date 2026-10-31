using System.IO;
using UnrealBuildTool;

// The module of the plugin: the Unreal side (ATerrainWorld and its mesh backends), the pure C++
// layer of Private/Bridge, and terrain_core itself, compiled from its sources through the
// wrappers of Private/TerrainCore (one per source, tests/unreal_core_sources.cmake keeps them in
// step). A single module: a second one would need export macros in terrain_core.
public class TerrainEngine : ModuleRules
{
	public TerrainEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		// The sources of terrain_core must see no Unreal header: no PCH, which Unreal would force
		// into every file, with its macros and, on Windows, windows.h whose near and far macros
		// break voronoi.cpp and aquifers.cpp.
		PCHUsage = PCHUsageMode.NoPCHs;
		// One translation unit per source: two sources of the core may hold file-local helpers
		// of the same name.
		bUseUnity = false;
		// terrain_core throws std::invalid_argument, and its workers' errors are rethrown.
		bEnableExceptions = true;
		CppStandard = CppStandardVersion.Cpp20;
		// No fast math: the plugin must generate the world terrain_core generates (the check of
		// ATerrainWorld::BeginPlay). Phase 9 adds an #error to the core under __FAST_MATH__ and
		// _M_FP_FAST.
		FPSemantics = FPSemanticsMode.Precise;

		// The plugin stays in its checkout of terrain-engine: <repository>/terrain_unreal.
		string RepositoryRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", ".."));
		string CoreInclude = Path.Combine(RepositoryRoot, "terrain_core", "include");
		if (!Directory.Exists(CoreInclude))
		{
			throw new DirectoryNotFoundException(
				"TerrainEngine: terrain_core not found at " + CoreInclude + ". The plugin " +
				"must stay in its checkout of terrain-engine (terrain_unreal/): see " +
				"terrain_unreal/README.md.");
		}
		PrivateIncludePaths.Add(CoreInclude);
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		// The GPU horizon: static meshes built in game, textures updated by regions, frame times.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"ProceduralMeshComponent", "MeshDescription", "StaticMeshDescription", "RenderCore", "RHI" });

		// The tools of the developers (terrain_unreal/DEV_TOOLS.md): the screen of F3 and the
		// Terrain.* commands of views, places, view modes and seed. Never in a Shipping build: every
		// file of them holds its code under #if TERRAIN_DEV_TOOLS (tests/unreal_dev_tools_guard).
		bool bDevTools = Target.Configuration != UnrealTargetConfiguration.Shipping;
		PrivateDefinitions.Add(bDevTools ? "TERRAIN_DEV_TOOLS=1" : "TERRAIN_DEV_TOOLS=0");
		// Their keys (InputCore) and the folder of the plugin, where the places lie (Projects).
		PrivateDependencyModuleNames.AddRange(new string[] { "InputCore", "Projects" });
	}
}
