// Breaking and placing in game (agent 28): Enhanced Input bound on an input component of its own, the
// trace of the
// camera, the ghost of the edit, the click queued on the edit thread of the session. Everything
// computed (the aim, the face, the tool, the capsule, the gate, the rate) is in the Bridge
// (block_targeting.hpp, build_rules.hpp), tested by ctest.
#include "TerrainBuildComponent.h"

#include "Bridge/block_targeting.hpp"
#include "Bridge/build_rules.hpp"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInterface.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"
#include "TerrainWorldBuild.h"
#include "TerrainWorldState.h"

#include <optional>

// What the component keeps between two frames.
struct FTerrainBuildRuntime
{
	terrain::unreal::EditRate Rate;
	bool bBreakHeld = false;
	bool bPlaceHeld = false;
	TWeakObjectPtr<APlayerController> MappedController;
	bool bDefaultInput = false; // the context and the actions made by MakeDefaultInput
};

namespace
{
	using TerrainWorldConversions::ToUnrealPoint;

	constexpr TCHAR CubeMesh[] = TEXT("/Engine/BasicShapes/Cube.Cube");       // 100 cm, centered
	constexpr TCHAR SphereMesh[] = TEXT("/Engine/BasicShapes/Sphere.Sphere"); // 100 cm across
	constexpr TCHAR WireframeMaterial[] =
		TEXT("/Engine/EngineDebugMaterials/WireframeMaterial.WireframeMaterial");

	terrain::unreal::BuildTool ToBridge(ETerrainBuildTool Tool)
	{
		return static_cast<terrain::unreal::BuildTool>(static_cast<uint8>(Tool));
	}

	// An action of a button (a new UInputAction holds a boolean value).
	UInputAction* MakeAction(UObject& Outer, const TCHAR* Name)
	{
		return NewObject<UInputAction>(&Outer, Name, RF_Transient);
	}

	// The default keys, written in the log when they are mapped.
	constexpr TCHAR DefaultKeys[] = TEXT("break: LeftMouseButton, place: RightMouseButton, tools: "
		"One, Two, Three, next / previous tool: MouseScrollUp / MouseScrollDown");

	// The edit aimed at now, and the hit it comes from.
	struct FAim
	{
		terrain::unreal::BuildTarget Target;
		terrain::unreal::BuildHit Hit;
	};

	// The trace of the camera on the tiles of the world: the first tile hit within reach of the eye.
	std::optional<FAim> AimAt(APawn& Pawn, APlayerController& Controller, ATerrainWorld& World,
		const terrain::unreal::UnrealFrame& Frame, float ReachMeters, ETerrainBuildTool Tool,
		ETerrainBuildAction Action)
	{
		if (Controller.PlayerCameraManager == nullptr || Pawn.GetWorld() == nullptr)
		{
			return std::nullopt;
		}
		const FVector Camera = Controller.PlayerCameraManager->GetCameraLocation();
		const FVector Forward = Controller.PlayerCameraManager->GetCameraRotation().Vector();
		const FVector Eye = Pawn.GetPawnViewLocation();
		// From the camera (behind the pawn in third person) to the reach of the eye beyond it.
		const double Length = ReachMeters * 100.0 + FVector::Dist(Camera, Eye);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainBuild), true, &Pawn);
		FHitResult Hit;
		if (!Pawn.GetWorld()->LineTraceSingleByChannel(Hit, Camera, Camera + Forward * Length,
				ECC_Visibility, Params) ||
			Hit.GetActor() != &World)
		{
			return std::nullopt;
		}
		FAim Aim;
		Aim.Hit = terrain::unreal::hitFromUnreal(Frame, ToUnrealPoint(Hit.ImpactPoint),
			Hit.ImpactNormal.X, Hit.ImpactNormal.Y, Hit.ImpactNormal.Z,
			FVector::Dist(Hit.ImpactPoint, Eye));
		if (!terrain::unreal::withinReach(Aim.Hit, ReachMeters))
		{
			return std::nullopt;
		}
		Aim.Target = terrain::unreal::buildTarget(Aim.Hit, ToBridge(Tool),
			Action == ETerrainBuildAction::Break ? terrain::unreal::BuildAction::Break
												 : terrain::unreal::BuildAction::Place);
		return Aim;
	}

	// True if a placed volume would close on the capsule of the pawn.
	bool InCapsule(const APawn& Pawn, const terrain::unreal::UnrealFrame& Frame,
		const terrain::VolumeEdit& Edit)
	{
		const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Pawn.GetRootComponent());
		if (Capsule == nullptr)
		{
			return false;
		}
		const terrain::unreal::CoreCapsule Core = terrain::unreal::capsuleFromUnreal(Frame,
			ToUnrealPoint(Capsule->GetComponentLocation()), Capsule->GetScaledCapsuleHalfHeight(),
			Capsule->GetScaledCapsuleRadius());
		return terrain::unreal::touchesCapsule(Edit, Core);
	}

	FString Describe(ETerrainBuildAction Action, ETerrainBuildTool Tool, const terrain::VolumeEdit& Edit)
	{
		return FString::Printf(TEXT("%s %s at (%d, %d, %d)"),
			Action == ETerrainBuildAction::Break ? TEXT("break") : TEXT("place"),
			UTF8_TO_TCHAR(terrain::unreal::toolName(ToBridge(Tool))), Edit.anchor.x,
			Edit.anchor.y, Edit.anchor.z);
	}
} // namespace

UTerrainBuildComponent::UTerrainBuildComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after the camera moved this frame
}

void UTerrainBuildComponent::BeginPlay()
{
	Super::BeginPlay();
	Runtime = MakePimpl<FTerrainBuildRuntime>();
	Runtime->Rate = terrain::unreal::EditRate(1.0 / FMath::Max(EditsPerSecond, 0.5f));
	MakeDefaultInput();
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}
	Ghost = NewObject<UStaticMeshComponent>(Owner, TEXT("TerrainBuildGhost"), RF_Transient);
	Ghost->SetUsingAbsoluteLocation(true);
	Ghost->SetUsingAbsoluteRotation(true);
	Ghost->SetUsingAbsoluteScale(true);
	Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ghost->SetCastShadow(false);
	Ghost->SetVisibility(false);
	CubeShape = LoadObject<UStaticMesh>(nullptr, CubeMesh);
	SphereShape = LoadObject<UStaticMesh>(nullptr, SphereMesh);
	Ghost->SetStaticMesh(CubeShape.Get());
	UMaterialInterface* Material = GhostMaterial != nullptr
		? GhostMaterial.Get()
		: LoadObject<UMaterialInterface>(nullptr, WireframeMaterial);
	if (Material != nullptr)
	{
		Ghost->SetMaterial(0, Material);
	}
	Ghost->RegisterComponent();
}

void UTerrainBuildComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnmapInput();
	if (Ghost != nullptr)
	{
		Ghost->DestroyComponent();
		Ghost = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UTerrainBuildComponent::MakeDefaultInput()
{
	if (MappingContext != nullptr)
	{
		return; // the context and the actions of the project
	}
	BreakAction = MakeAction(*this, TEXT("TerrainBreak"));
	PlaceAction = MakeAction(*this, TEXT("TerrainPlace"));
	NextToolAction = MakeAction(*this, TEXT("TerrainNextTool"));
	PreviousToolAction = MakeAction(*this, TEXT("TerrainPreviousTool"));
	BlockToolAction = MakeAction(*this, TEXT("TerrainBlockTool"));
	SmallSphereToolAction = MakeAction(*this, TEXT("TerrainSmallSphereTool"));
	LargeSphereToolAction = MakeAction(*this, TEXT("TerrainLargeSphereTool"));
	UInputMappingContext* Context =
		NewObject<UInputMappingContext>(this, TEXT("TerrainBuildContext"), RF_Transient);
	Context->MapKey(BreakAction.Get(), EKeys::LeftMouseButton);
	Context->MapKey(PlaceAction.Get(), EKeys::RightMouseButton);
	Context->MapKey(NextToolAction.Get(), EKeys::MouseScrollUp);
	Context->MapKey(PreviousToolAction.Get(), EKeys::MouseScrollDown);
	Context->MapKey(BlockToolAction.Get(), EKeys::One);
	Context->MapKey(SmallSphereToolAction.Get(), EKeys::Two);
	Context->MapKey(LargeSphereToolAction.Get(), EKeys::Three);
	MappingContext = Context;
	Runtime->bDefaultInput = true;
}

// The actions bound on an input component of the build, pushed on the stack of the controller:
// the input of the character stays as it is.
void UTerrainBuildComponent::BindInput(APlayerController& Controller)
{
	BuildInput = NewObject<UEnhancedInputComponent>(this, TEXT("TerrainBuildInput"), RF_Transient);
	const auto Bind = [this](const TObjectPtr<UInputAction>& Action, ETriggerEvent Event,
						  void (UTerrainBuildComponent::*Handler)())
	{
		if (Action != nullptr)
		{
			BuildInput->BindAction(Action.Get(), Event, this, Handler);
		}
	};
	Bind(BreakAction, ETriggerEvent::Started, &UTerrainBuildComponent::OnBreakPressed);
	Bind(BreakAction, ETriggerEvent::Completed, &UTerrainBuildComponent::OnBreakReleased);
	Bind(PlaceAction, ETriggerEvent::Started, &UTerrainBuildComponent::OnPlacePressed);
	Bind(PlaceAction, ETriggerEvent::Completed, &UTerrainBuildComponent::OnPlaceReleased);
	Bind(NextToolAction, ETriggerEvent::Started, &UTerrainBuildComponent::OnNextTool);
	Bind(PreviousToolAction, ETriggerEvent::Started, &UTerrainBuildComponent::OnPreviousTool);
	Bind(BlockToolAction, ETriggerEvent::Started, &UTerrainBuildComponent::OnBlockTool);
	Bind(SmallSphereToolAction, ETriggerEvent::Started, &UTerrainBuildComponent::OnSmallSphereTool);
	Bind(LargeSphereToolAction, ETriggerEvent::Started, &UTerrainBuildComponent::OnLargeSphereTool);
	Controller.PushInputComponent(BuildInput.Get());
}

void UTerrainBuildComponent::MapInput()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (Runtime->MappedController.Get() == Controller)
	{
		return;
	}
	UnmapInput();
	if (Controller == nullptr || MappingContext == nullptr)
	{
		return;
	}
	UEnhancedInputLocalPlayerSubsystem* Input =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
	if (Input == nullptr)
	{
		return;
	}
	Input->AddMappingContext(MappingContext.Get(), MappingPriority);
	BindInput(*Controller);
	Runtime->MappedController = Controller;
	// Only the keys of the build: those of the character are read in game.
	const FString Keys = Runtime->bDefaultInput
		? FString(DefaultKeys)
		: FString::Printf(TEXT("the context %s of the project"), *MappingContext->GetName());
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain Build: input of %s at priority %d: %s"),
		*GetNameSafe(GetOwner()), MappingPriority, *Keys);
}

void UTerrainBuildComponent::UnmapInput()
{
	APlayerController* Controller = Runtime.IsValid() ? Runtime->MappedController.Get() : nullptr;
	if (Controller != nullptr && BuildInput != nullptr)
	{
		Controller->PopInputComponent(BuildInput.Get());
	}
	BuildInput = nullptr;
	if (Controller != nullptr && MappingContext != nullptr)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
					Controller->GetLocalPlayer()))
		{
			Input->RemoveMappingContext(MappingContext.Get());
		}
	}
	if (Runtime.IsValid())
	{
		Runtime->MappedController.Reset();
		Runtime->bBreakHeld = false;
		Runtime->bPlaceHeld = false;
	}
}

ATerrainWorld* UTerrainBuildComponent::FindTerrainWorld()
{
	if (!TerrainWorld.IsValid() && GetWorld() != nullptr)
	{
		for (TActorIterator<ATerrainWorld> It(GetWorld()); It; ++It)
		{
			TerrainWorld = *It;
			break;
		}
	}
	return TerrainWorld.Get();
}

FString UTerrainBuildComponent::GateRefusal() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller =
		Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	ATerrainWorld* World = TerrainWorld.Get();
	terrain::unreal::BuildGate Gate;
	Gate.playing = World != nullptr && FTerrainBuildHooks::State(*World) != nullptr;
	Gate.devMapOpen = World != nullptr && FTerrainBuildHooks::IsDevMapOpen(*World);
	Gate.loading = World == nullptr || FTerrainBuildHooks::IsLoadingView(*World);
	Gate.controlsOwner = Controller != nullptr && Controller->IsLocalController();
	// No Unreal call reads the focus here: without it the viewport sends no input, and the keys
	// held are released (Completed) when it is lost; to check in game (Alt-Tab, a button held).
	Gate.focused = true;
	const char* Refusal = terrain::unreal::gateRefusal(Gate);
	return Refusal == nullptr ? FString() : FString(UTF8_TO_TCHAR(Refusal));
}

void UTerrainBuildComponent::SetTool(ETerrainBuildTool NewTool)
{
	Tool = NewTool;
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 2.0f, FColor::White,
			FString::Printf(TEXT("Tool: %s"),
				UTF8_TO_TCHAR(terrain::unreal::toolName(ToBridge(Tool)))));
	}
}

bool UTerrainBuildComponent::Build(ETerrainBuildAction Action)
{
	FindTerrainWorld();
	LastRefusal = GateRefusal();
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	FTerrainWorldState* State =
		TerrainWorld.IsValid() ? FTerrainBuildHooks::State(*TerrainWorld.Get()) : nullptr;
	if (!LastRefusal.IsEmpty() || Controller == nullptr || State == nullptr)
	{
		return false;
	}
	if (!Runtime->Rate.allow(FPlatformTime::Seconds()))
	{
		LastRefusal = TEXT("too many edits a second");
		return false;
	}
	const std::optional<FAim> Aim = AimAt(*Pawn, *Controller, *TerrainWorld.Get(), State->Frame,
		ReachMeters, Tool, Action);
	if (!Aim)
	{
		LastRefusal = TEXT("no ground within reach");
		return false;
	}
	if (Action == ETerrainBuildAction::Place && InCapsule(*Pawn, State->Frame, Aim->Target.edit))
	{
		LastRefusal = TEXT("it would close on the pawn");
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Yellow,
				TEXT("Cannot place: too close to the character"));
		}
		return false;
	}
	FTerrainBuildHooks::QueueBuild(*State, Aim->Target.edit,
		Describe(Action, Tool, Aim->Target.edit));
	return true;
}

void UTerrainBuildComponent::OnBreakPressed()
{
	Runtime->bBreakHeld = true;
}

void UTerrainBuildComponent::OnBreakReleased()
{
	Runtime->bBreakHeld = false;
}

void UTerrainBuildComponent::OnPlacePressed()
{
	Runtime->bPlaceHeld = true;
}

void UTerrainBuildComponent::OnPlaceReleased()
{
	Runtime->bPlaceHeld = false;
}

void UTerrainBuildComponent::ChangeTool(ETerrainBuildTool NewTool)
{
	if (GateRefusal().IsEmpty()) // the dev map, a load, another pawn: the keys are not ours
	{
		SetTool(NewTool);
	}
}

void UTerrainBuildComponent::OnNextTool()
{
	ChangeTool(static_cast<ETerrainBuildTool>(
		static_cast<uint8>(terrain::unreal::nextTool(ToBridge(Tool), 1))));
}

void UTerrainBuildComponent::OnPreviousTool()
{
	ChangeTool(static_cast<ETerrainBuildTool>(
		static_cast<uint8>(terrain::unreal::nextTool(ToBridge(Tool), -1))));
}

void UTerrainBuildComponent::OnBlockTool()
{
	ChangeTool(ETerrainBuildTool::Block);
}

void UTerrainBuildComponent::OnSmallSphereTool()
{
	ChangeTool(ETerrainBuildTool::Sphere2);
}

void UTerrainBuildComponent::OnLargeSphereTool()
{
	ChangeTool(ETerrainBuildTool::Sphere4);
}

// Pressed or held: the rate of the edits (EditsPerSecond) spaces them, the first one at once.
void UTerrainBuildComponent::BuildHeld()
{
	if (Runtime->bBreakHeld)
	{
		Build(ETerrainBuildAction::Break);
	}
	else if (Runtime->bPlaceHeld)
	{
		Build(ETerrainBuildAction::Place);
	}
}

void UTerrainBuildComponent::UpdateGhost()
{
	if (Ghost == nullptr)
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	FTerrainWorldState* State =
		TerrainWorld.IsValid() ? FTerrainBuildHooks::State(*TerrainWorld.Get()) : nullptr;
	std::optional<FAim> Aim;
	if (bShowGhost && Controller != nullptr && State != nullptr && GateRefusal().IsEmpty())
	{
		// The ghost of a break with the left button held, else of a place: what a click would do.
		const ETerrainBuildAction Action = Runtime->bPlaceHeld ? ETerrainBuildAction::Place
															   : ETerrainBuildAction::Break;
		Aim = AimAt(*Pawn, *Controller, *TerrainWorld.Get(), State->Frame, ReachMeters, Tool, Action);
	}
	if (!Aim)
	{
		Ghost->SetVisibility(false);
		return;
	}
	const terrain::unreal::UnrealGhost Shape =
		terrain::unreal::toUnrealGhost(State->Frame, Aim->Target.ghost);
	UStaticMesh* Mesh = Shape.sphere ? SphereShape.Get() : CubeShape.Get();
	if (Mesh != nullptr && Ghost->GetStaticMesh() != Mesh)
	{
		Ghost->SetStaticMesh(Mesh);
	}
	// The meshes of the engine are 100 cm across.
	Ghost->SetWorldLocationAndRotation(FVector(Shape.center.x, Shape.center.y, Shape.center.z),
		FRotator::ZeroRotator);
	Ghost->SetWorldScale3D(FVector(Shape.half.x, Shape.half.y, Shape.half.z) / 50.0);
	Ghost->SetVisibility(true);
}

void UTerrainBuildComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Runtime.IsValid() || FindTerrainWorld() == nullptr)
	{
		return;
	}
	MapInput();
	BuildHeld();
	UpdateGhost();
}
