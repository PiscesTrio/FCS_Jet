// FCS_Jet - space flight pawn (no aerodynamics)

#include "FCS_JetPawn.h"

#include "Flight/JetTuning.h"

#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * Facts about the engine and about arithmetic, not tunables, so they stay here rather
	 * than going into FJetFlightParams. Anything a designer might reasonably want to
	 * change moved to the params struct.
	 */

	/** cm/s to km/h. */
	constexpr float CmsToKmh = 0.036f;

	/** /Engine/BasicShapes/Sphere is 100 units across, so scale 1 is a 50 unit radius. */
	constexpr float BasicSphereRadius = 50.f;
}

AFCS_JetPawn::AFCS_JetPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// The controller rotation plays no part; attitude is integrated entirely by this class.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	ShipRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShipRoot"));
	SetRootComponent(ShipRoot);

	ShipMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipMesh"));
	ShipMesh->SetupAttachment(ShipRoot);
	// It is a spacecraft. Stopping dead against the floor would suit neither the premise
	// nor the feel, and swept movement at these speeds is an easy way to get stuck.
	ShipMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShipMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshAsset(
		TEXT("/Game/Imported/TIE_Interceptor/TIE_Interceptor.TIE_Interceptor"));
	if (MeshAsset.Succeeded())
	{
		ShipMesh->SetStaticMesh(MeshAsset.Object);
	}

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(ShipRoot);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// The Tuning asset is not reachable from a constructor, so the components start on the
	// authored defaults. BeginPlay reapplies them from whatever asset is actually assigned.
	ApplyMeshParams(DefaultParams);
	ApplyCameraParams(DefaultParams);

	// ---------- Crash blast ----------

	BlastMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlastMesh"));
	BlastMesh->SetupAttachment(ShipRoot);
	// Absolute transform so the blast stays where the ship hit rather than following the
	// wreck and then being dragged across the map by the respawn teleport.
	BlastMesh->SetAbsolute(true, true, false);
	BlastMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlastMesh->SetGenerateOverlapEvents(false);
	BlastMesh->SetCastShadow(false);
	BlastMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		BlastMesh->SetStaticMesh(SphereAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlashAsset(
		TEXT("/Game/FCS_Jet/Effects/M_CrashBlast.M_CrashBlast"));
	if (FlashAsset.Succeeded())
	{
		BlastMesh->SetMaterial(0, FlashAsset.Object);
	}

	BlastLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BlastLight"));
	BlastLight->SetupAttachment(ShipRoot);
	BlastLight->SetAbsolute(true, true, false);
	BlastLight->SetCastShadows(false);
	BlastLight->SetAttenuationRadius(DefaultParams.BlastLightAttenuationRadius);
	BlastLight->SetIntensity(0.f);
	BlastLight->SetVisibility(false);

	// ---------- Input assets ----------

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> ImcAsset(
		TEXT("/Game/FCS_Jet/Input/IMC_FCS_Jet.IMC_FCS_Jet"));
	static ConstructorHelpers::FObjectFinder<UInputAction> PitchAsset(
		TEXT("/Game/FCS_Jet/Input/IA_Pitch.IA_Pitch"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RollAsset(
		TEXT("/Game/FCS_Jet/Input/IA_Roll.IA_Roll"));
	static ConstructorHelpers::FObjectFinder<UInputAction> YawAsset(
		TEXT("/Game/FCS_Jet/Input/IA_Yaw.IA_Yaw"));
	static ConstructorHelpers::FObjectFinder<UInputAction> ThrottleAsset(
		TEXT("/Game/FCS_Jet/Input/IA_Throttle.IA_Throttle"));
	static ConstructorHelpers::FObjectFinder<UInputAction> AfterburnerAsset(
		TEXT("/Game/FCS_Jet/Input/IA_Afterburner.IA_Afterburner"));
	static ConstructorHelpers::FObjectFinder<UInputAction> AutoLevelAsset(
		TEXT("/Game/FCS_Jet/Input/IA_AutoLevel.IA_AutoLevel"));
	static ConstructorHelpers::FObjectFinder<UInputAction> FreeLookAsset(
		TEXT("/Game/FCS_Jet/Input/IA_FreeLook.IA_FreeLook"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookAsset(
		TEXT("/Game/FCS_Jet/Input/IA_Look.IA_Look"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookStickAsset(
		TEXT("/Game/FCS_Jet/Input/IA_LookStick.IA_LookStick"));

	DefaultMappingContext = ImcAsset.Succeeded() ? ImcAsset.Object : nullptr;
	PitchAction = PitchAsset.Succeeded() ? PitchAsset.Object : nullptr;
	RollAction = RollAsset.Succeeded() ? RollAsset.Object : nullptr;
	YawAction = YawAsset.Succeeded() ? YawAsset.Object : nullptr;
	ThrottleAction = ThrottleAsset.Succeeded() ? ThrottleAsset.Object : nullptr;
	AfterburnerAction = AfterburnerAsset.Succeeded() ? AfterburnerAsset.Object : nullptr;
	AutoLevelAction = AutoLevelAsset.Succeeded() ? AutoLevelAsset.Object : nullptr;
	FreeLookAction = FreeLookAsset.Succeeded() ? FreeLookAsset.Object : nullptr;
	LookAction = LookAsset.Succeeded() ? LookAsset.Object : nullptr;
	LookStickAction = LookStickAsset.Succeeded() ? LookStickAsset.Object : nullptr;
}

const FJetFlightParams& AFCS_JetPawn::GetParams() const
{
	return Tuning ? Tuning->Params : DefaultParams;
}

bool AFCS_JetPawn::IsPredictedImpactFatal() const
{
	return bImpactPredicted && PredictedImpactSpeed >= GetParams().MinCrashImpactSpeed;
}

void AFCS_JetPawn::ApplyMeshParams(const FJetFlightParams& P)
{
	if (!ShipMesh)
	{
		return;
	}

	// The mesh nose is +Y and a pawn moves along +X, so the mesh is yawed into the control
	// frame. The offset then decides what the ship rotates about, and therefore what the
	// free look camera orbits, since the spring arm sits on the pawn origin.
	ShipMesh->SetRelativeRotation(FRotator(0.f, P.MeshYawOffsetDeg, 0.f));
	ShipMesh->SetRelativeLocation(P.MeshPivotOffset);
}

void AFCS_JetPawn::ApplyCameraParams(const FJetFlightParams& P)
{
	if (!SpringArm || !Camera)
	{
		return;
	}

	SpringArm->TargetArmLength = P.ArmLength;
	SpringArm->SocketOffset = P.CameraSocketOffset;
	SpringArm->bEnableCameraLag = P.bEnableCameraLag;
	SpringArm->CameraLagSpeed = P.CameraLagSpeed;
	SpringArm->bEnableCameraRotationLag = P.bEnableCameraRotationLag;
	SpringArm->CameraRotationLagSpeed = P.CameraRotationLagSpeed;
	SpringArm->bDoCollisionTest = P.bSpringArmCollisionTest;
}

void AFCS_JetPawn::ApplyMappingContext(const TCHAR* CallSite)
{
	if (!DefaultMappingContext)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[FCS_Jet] %s: DefaultMappingContext is null. /Game/FCS_Jet/Input/IMC_FCS_Jet "
				 "failed to load, so no control input will reach the ship."), CallSite);
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FCS_Jet] %s: no player controller yet."), CallSite);
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[FCS_Jet] %s: no local player subsystem yet."), CallSite);
		return;
	}

	Subsystem->AddMappingContext(DefaultMappingContext, 0);
	UE_LOG(LogTemp, Log, TEXT("[FCS_Jet] %s: mapping context applied, has_context=%s."),
		CallSite, Subsystem->HasMappingContext(DefaultMappingContext) ? TEXT("true") : TEXT("false"));
}

void AFCS_JetPawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	ApplyMappingContext(TEXT("NotifyControllerChanged"));
}

void AFCS_JetPawn::BeginPlay()
{
	Super::BeginPlay();

	// The spawn transform doubles as the respawn point, so wherever the PlayerStart puts
	// the ship is where it comes back.
	RespawnTransform = GetActorTransform();

	// The constructor could only reach DefaultParams. Now that the Tuning asset is
	// resolved, push the real values onto the components.
	const FJetFlightParams& P = GetParams();
	ApplyMeshParams(P);
	ApplyCameraParams(P);
	if (Camera)
	{
		// Start on the tuned value. TickCamera only ever interpolates toward it, so without
		// this the first second of flight would ease across from the engine default.
		Camera->SetFieldOfView(P.BaseFOV);
	}

	if (BlastMesh && BlastMesh->GetMaterial(0))
	{
		BlastMaterial = BlastMesh->CreateDynamicMaterialInstance(0);
	}
	if (!BlastMaterial)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FCS_Jet] No blast material. /Game/FCS_Jet/Effects/M_CrashBlast failed to load, "
				 "so the crash will still happen but without the fireball."));
	}
}

void AFCS_JetPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[FCS_Jet] InputComponent is not a UEnhancedInputComponent. Check "
				 "DefaultInputComponentClass in the project input settings."));
		return;
	}

	// Axis actions read their value on Triggered and reset it on Completed. Without the
	// reset the last non-zero value would persist after the key is released and the ship
	// would keep rotating on its own.
	if (PitchAction)
	{
		EIC->BindAction(PitchAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnPitch);
		EIC->BindAction(PitchAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnPitchReleased);
		EIC->BindAction(PitchAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnPitchReleased);
	}
	if (RollAction)
	{
		EIC->BindAction(RollAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnRoll);
		EIC->BindAction(RollAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnRollReleased);
		EIC->BindAction(RollAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnRollReleased);
	}
	if (YawAction)
	{
		EIC->BindAction(YawAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnYaw);
		EIC->BindAction(YawAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnYawReleased);
		EIC->BindAction(YawAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnYawReleased);
	}
	if (ThrottleAction)
	{
		EIC->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnThrottle);
		EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnThrottleReleased);
		EIC->BindAction(ThrottleAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnThrottleReleased);
	}
	if (AfterburnerAction)
	{
		EIC->BindAction(AfterburnerAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnAfterburnerStart);
		EIC->BindAction(AfterburnerAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnAfterburnerStop);
		EIC->BindAction(AfterburnerAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnAfterburnerStop);
	}
	if (AutoLevelAction)
	{
		EIC->BindAction(AutoLevelAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnAutoLevelStart);
		EIC->BindAction(AutoLevelAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnAutoLevelStop);
		EIC->BindAction(AutoLevelAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnAutoLevelStop);
	}
	if (FreeLookAction)
	{
		EIC->BindAction(FreeLookAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnFreeLookStart);
		EIC->BindAction(FreeLookAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnFreeLookStop);
		EIC->BindAction(FreeLookAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnFreeLookStop);
	}
	if (LookAction)
	{
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnLook);
	}
	if (LookStickAction)
	{
		EIC->BindAction(LookStickAction, ETriggerEvent::Triggered, this, &AFCS_JetPawn::OnLookStick);
		EIC->BindAction(LookStickAction, ETriggerEvent::Completed, this, &AFCS_JetPawn::OnLookStickReleased);
		EIC->BindAction(LookStickAction, ETriggerEvent::Canceled, this, &AFCS_JetPawn::OnLookStickReleased);
	}

	// By this point the controller definitely has a local player, which is not guaranteed
	// when NotifyControllerChanged runs.
	ApplyMappingContext(TEXT("SetupPlayerInputComponent"));
}

void AFCS_JetPawn::OnPitch(const FInputActionValue& Value) { PitchInput = Value.Get<float>(); }
void AFCS_JetPawn::OnRoll(const FInputActionValue& Value) { RollInput = Value.Get<float>(); }
void AFCS_JetPawn::OnYaw(const FInputActionValue& Value) { YawInput = Value.Get<float>(); }
void AFCS_JetPawn::OnThrottle(const FInputActionValue& Value) { ThrottleInput = Value.Get<float>(); }

void AFCS_JetPawn::OnPitchReleased() { PitchInput = 0.f; }
void AFCS_JetPawn::OnRollReleased() { RollInput = 0.f; }
void AFCS_JetPawn::OnYawReleased() { YawInput = 0.f; }
void AFCS_JetPawn::OnThrottleReleased() { ThrottleInput = 0.f; }

void AFCS_JetPawn::OnAfterburnerStart() { bAfterburnerHeld = true; }
void AFCS_JetPawn::OnAfterburnerStop() { bAfterburnerHeld = false; }
void AFCS_JetPawn::OnAutoLevelStart() { bAutoLevelHeld = true; }
void AFCS_JetPawn::OnAutoLevelStop() { bAutoLevelHeld = false; }
void AFCS_JetPawn::OnFreeLookStart() { bFreeLookHeld = true; }
void AFCS_JetPawn::OnFreeLookStop() { bFreeLookHeld = false; }

void AFCS_JetPawn::OnLook(const FInputActionValue& Value)
{
	// The mouse reports a delta, so accumulate here and let TickCamera consume it.
	LookInput += Value.Get<FVector2D>();
}

void AFCS_JetPawn::OnLookStick(const FInputActionValue& Value)
{
	// The stick reports a position, so it is stored rather than accumulated.
	StickLookInput = Value.Get<FVector2D>();
}

void AFCS_JetPawn::OnLookStickReleased()
{
	StickLookInput = FVector2D::ZeroVector;
}

void AFCS_JetPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Fetched once and handed down, so the whole frame runs on one consistent set of
	// values even if the tuning asset is edited from the details panel mid-frame.
	const FJetFlightParams& P = GetParams();

	if (bCrashed)
	{
		// No throttle, no attitude, no translation. The wreck just sits there.
		TickCrash(DeltaSeconds, P);
	}
	else
	{
		TickThrottle(DeltaSeconds, P);
		TickAttitude(DeltaSeconds, P);
		TickGravity(DeltaSeconds, P);
		// Predicted before translation so the readout describes the trajectory the ship is
		// about to fly, not the one it has already committed to this frame.
		TickImpactPrediction(P);
		TickTranslation(DeltaSeconds, P);
	}

	TickCamera(DeltaSeconds, P);
	DrawReadout(P);
}

void AFCS_JetPawn::TickThrottle(float DeltaSeconds, const FJetFlightParams& P)
{
	if (!FMath::IsNearlyZero(ThrottleInput))
	{
		Throttle = FMath::Clamp(
			Throttle + ThrottleInput * P.ThrottleChangeRate * DeltaSeconds, 0.f, 1.f);
	}

	const float Boost = bAfterburnerHeld ? P.AfterburnerMultiplier : 1.f;
	const float TargetSpeed = FMath::Lerp(P.IdleSpeed, P.CruiseSpeed, Throttle) * Boost;
	const float Responsiveness = bAfterburnerHeld ? P.AfterburnerInterpSpeed : P.SpeedInterpSpeed;

	CurrentSpeed = FMath::FInterpTo(CurrentSpeed, TargetSpeed, DeltaSeconds, Responsiveness);
}

void AFCS_JetPawn::TickAttitude(float DeltaSeconds, const FJetFlightParams& P)
{
	// There are no aerodynamic moments, so the entire handling character lives in this
	// one interpolation.
	FVector TargetRates(PitchInput * P.PitchRate, YawInput * P.YawRate, RollInput * P.RollRate);

	if (bAutoLevelHeld)
	{
		// Level means wings level and nose on the horizon. Yaw is deliberately left with
		// the player: which way the ship points is their intent, not something to take
		// away while they are only asking to stop tumbling.
		//
		// The correction is proportional on the pitch and roll angles rather than a
		// quaternion shortest arc. The two are coupled while banked, but roll is the far
		// faster axis here, so it wins the race and the pitch term becomes accurate as
		// the wings come level. Convergence from inverted and from near vertical is
		// measured rather than assumed.
		const FRotator Current = GetActorRotation();
		const float PitchError = FRotator::NormalizeAxis(-Current.Pitch);
		const float RollError = FRotator::NormalizeAxis(-Current.Roll);

		TargetRates.X = FMath::Clamp(PitchError * P.AutoLevelGain, -P.PitchRate, P.PitchRate);
		TargetRates.Z = FMath::Clamp(RollError * P.AutoLevelGain, -P.RollRate, P.RollRate);
	}

	CurrentRates = FMath::VInterpTo(CurrentRates, TargetRates, DeltaSeconds, P.ControlInterpSpeed);

	const FRotator DeltaRotation(
		CurrentRates.X * DeltaSeconds,   // Pitch
		CurrentRates.Y * DeltaSeconds,   // Yaw
		CurrentRates.Z * DeltaSeconds);  // Roll

	// Compose the local rotation as a quaternion. A spacecraft needs full 360 degree
	// freedom on every axis and accumulating Euler angles would eventually gimbal lock.
	AddActorLocalRotation(DeltaRotation.Quaternion());
}

float AFCS_JetPawn::GetGravityScaleAtZ(float WorldZ, const FJetFlightParams& P) const
{
	if (WorldZ <= P.GravityCeilingZ)
	{
		return 1.f;
	}
	if (P.GravityFalloffRange <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const float Above = WorldZ - P.GravityCeilingZ;
	return FMath::Clamp(1.f - Above / P.GravityFalloffRange, 0.f, 1.f);
}

float AFCS_JetPawn::GetGravityScale() const
{
	return GetGravityScaleAtZ(GetActorLocation().Z, GetParams());
}

void AFCS_JetPawn::TickGravity(float DeltaSeconds, const FJetFlightParams& P)
{
	const float Scale = GetGravityScaleAtZ(GetActorLocation().Z, P);

	if (Scale > 0.f)
	{
		FallSpeed = FMath::Min(FallSpeed + P.GravityAcceleration * Scale * DeltaSeconds, P.MaxFallSpeed);
	}
	else if (P.ZeroGFallDamping > 0.f)
	{
		// Clear of the band, a descent carried up from below bleeds away instead of being
		// kept forever. See ZeroGFallDamping for why conserving it is the wrong call here.
		//
		// Damping is gated on the band being fully behind the ship rather than scaled
		// across it. Blending the two against each other inside the band would give
		// partial gravity its own much lower terminal velocity, which is not what a
		// thinning gravity well should feel like.
		FallSpeed = FMath::FInterpTo(FallSpeed, 0.f, DeltaSeconds, P.ZeroGFallDamping);
	}
}

void AFCS_JetPawn::TickImpactPrediction(const FJetFlightParams& P)
{
	bImpactPredicted = false;
	PredictedImpactSpeed = 0.f;
	PredictedImpactTime = 0.f;

	if (!GetWorld())
	{
		return;
	}

	const FVector Velocity = GetActorForwardVector() * CurrentSpeed + FVector(0.f, 0.f, -FallSpeed);
	const float Speed = Velocity.Size();
	if (Speed <= P.MinSweepSpeed)
	{
		return;
	}

	const FVector Start = GetActorLocation();
	const FVector End = Start + Velocity * P.ImpactWarningSeconds;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FCSJetImpactPrediction), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(this);

	FHitResult Hit;
	if (!GetWorld()->SweepSingleByChannel(
			Hit, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(P.HullRadius), Params))
	{
		return;
	}

	bImpactPredicted = true;
	// Same measure the crash test uses, so the readout and the outcome cannot disagree.
	PredictedImpactSpeed = FMath::Abs(FVector::DotProduct(Velocity, Hit.ImpactNormal));
	PredictedImpactTime = (Hit.Distance > 0.f) ? Hit.Distance / Speed : 0.f;
}

void AFCS_JetPawn::TickTranslation(float DeltaSeconds, const FJetFlightParams& P)
{
	// Thrust always points along the nose, with no sideslip and no carried momentum, and
	// gravity adds a separate vertical term on top. Still no lift or drag anywhere.
	const FVector Velocity = GetActorForwardVector() * CurrentSpeed + FVector(0.f, 0.f, -FallSpeed);
	const FVector Delta = Velocity * DeltaSeconds;

	// Collision is detected by sweeping a sphere along the path rather than by enabling
	// collision on the hull and using swept movement. Swept movement would leave the ship
	// resting against, and frequently wedged into, whatever it touched; here the hit is
	// only ever a trigger and the response is entirely ours.
	if (P.bCrashEnabled && Velocity.Size() > P.MinSweepSpeed && GetWorld())
	{
		const FVector Start = GetActorLocation();
		const FVector End = Start + Delta;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(FCSJetGroundSweep), /*bTraceComplex=*/false);
		Params.AddIgnoredActor(this);

		FHitResult Hit;
		const bool bBlocked = GetWorld()->SweepSingleByChannel(
			Hit, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(P.HullRadius), Params);

		if (bBlocked)
		{
			// Close the gap up to the surface first, whatever happens next.
			AddActorWorldOffset(Hit.Location - Start, /*bSweep=*/false);

			// How hard the surface was actually hit, rather than how fast the ship was
			// travelling. See MinCrashImpactSpeed.
			const float ImpactSpeed = FMath::Abs(FVector::DotProduct(Velocity, Hit.ImpactNormal));

			if (ImpactSpeed >= P.MinCrashImpactSpeed)
			{
				BeginCrash(Hit, ImpactSpeed, P);
			}
			else
			{
				TouchDown(Hit, Delta * (1.f - Hit.Time));
			}
			return;
		}
	}

	AddActorWorldOffset(Delta, /*bSweep=*/false);
}

void AFCS_JetPawn::TouchDown(const FHitResult& Hit, const FVector& RemainingDelta)
{
	FallSpeed = 0.f;

	// Spend what is left of the frame's motion along the surface instead of into it, so a
	// shallow arrival skims the deck rather than sticking to the first contact point.
	const FVector Slide = FVector::VectorPlaneProject(RemainingDelta, Hit.ImpactNormal);

	// Only push out when the sweep actually started inside something. Applying a standing
	// normal offset every frame would make a parked ship levitate, because the push is far
	// larger than the distance one frame of gravity pulls it back down.
	FVector Depenetrate = FVector::ZeroVector;
	if (Hit.bStartPenetrating)
	{
		Depenetrate = Hit.ImpactNormal * (Hit.PenetrationDepth + 0.1f);
	}

	AddActorWorldOffset(Slide + Depenetrate, /*bSweep=*/false);
}

void AFCS_JetPawn::BeginCrash(const FHitResult& Hit, float ImpactSpeed, const FJetFlightParams& P)
{
	bCrashed = true;
	CrashElapsed = 0.f;
	LastImpactSpeed = ImpactSpeed;

	bImpactPredicted = false;
	PredictedImpactSpeed = 0.f;
	PredictedImpactTime = 0.f;

	CurrentSpeed = 0.f;
	CurrentRates = FVector::ZeroVector;
	FallSpeed = 0.f;
	Throttle = 0.f;
	PitchInput = RollInput = YawInput = ThrottleInput = 0.f;
	bAfterburnerHeld = false;

	if (ShipMesh)
	{
		ShipMesh->SetVisibility(false);
	}

	const FVector ImpactPoint = Hit.ImpactPoint;

	if (BlastMesh)
	{
		BlastMesh->SetWorldLocation(ImpactPoint);
		BlastMesh->SetWorldScale3D(FVector::ZeroVector);
		BlastMesh->SetVisibility(true);
	}
	if (BlastMaterial)
	{
		BlastMaterial->SetVectorParameterValue(TEXT("FlashColor"), P.BlastColor);
		BlastMaterial->SetScalarParameterValue(TEXT("FlashStrength"), P.BlastPeakStrength);
	}
	if (BlastLight)
	{
		// Lift the light off the deck so it reads as a fireball rather than a floor decal.
		BlastLight->SetWorldLocation(ImpactPoint + FVector(0.f, 0.f, 300.f));
		BlastLight->SetLightColor(P.BlastColor);
		BlastLight->SetIntensity(P.BlastLightPeakIntensity);
		BlastLight->SetVisibility(true);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[FCS_Jet] Ground impact on %s at %s. Normal speed %.0f km/h, threshold %.0f km/h. "
			 "Respawning in %.1f s."),
		*GetNameSafe(Hit.GetActor()), *ImpactPoint.ToCompactString(),
		ImpactSpeed * CmsToKmh, P.MinCrashImpactSpeed * CmsToKmh, P.RespawnDelay);
}

void AFCS_JetPawn::TickCrash(float DeltaSeconds, const FJetFlightParams& P)
{
	CrashElapsed += DeltaSeconds;

	const float Alpha = FMath::Clamp(CrashElapsed / P.BlastDuration, 0.f, 1.f);
	const float Fade = FMath::Square(1.f - Alpha);

	if (BlastMesh && BlastMesh->IsVisible())
	{
		// Ease out so the fireball punches open and then slows, rather than growing linearly.
		const float Radius = P.BlastMaxRadius * (1.f - FMath::Square(1.f - Alpha));
		BlastMesh->SetWorldScale3D(FVector(Radius / BasicSphereRadius));

		if (BlastMaterial)
		{
			BlastMaterial->SetScalarParameterValue(TEXT("FlashStrength"), P.BlastPeakStrength * Fade);
		}
		if (Alpha >= 1.f)
		{
			BlastMesh->SetVisibility(false);
		}
	}

	if (BlastLight && BlastLight->IsVisible())
	{
		BlastLight->SetIntensity(P.BlastLightPeakIntensity * Fade * (1.f - Alpha));
		if (Alpha >= 1.f)
		{
			BlastLight->SetIntensity(0.f);
			BlastLight->SetVisibility(false);
		}
	}

	if (CrashElapsed >= P.RespawnDelay)
	{
		Respawn(P);
	}
}

void AFCS_JetPawn::Respawn(const FJetFlightParams& P)
{
	bCrashed = false;
	CrashElapsed = 0.f;

	SetActorTransform(RespawnTransform, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	CurrentSpeed = 0.f;
	CurrentRates = FVector::ZeroVector;
	FallSpeed = 0.f;
	Throttle = 0.35f;
	PitchInput = RollInput = YawInput = ThrottleInput = 0.f;
	bAfterburnerHeld = false;
	FreeLookOffset = FVector2D::ZeroVector;

	if (ShipMesh)
	{
		ShipMesh->SetVisibility(true);
	}
	if (BlastMesh)
	{
		BlastMesh->SetVisibility(false);
	}
	if (BlastLight)
	{
		BlastLight->SetIntensity(0.f);
		BlastLight->SetVisibility(false);
	}

	// Two frames: one for the arm to land on the teleported ship, one to settle before
	// lag is handed back.
	CameraSnapFramesLeft = 2;

	UE_LOG(LogTemp, Log, TEXT("[FCS_Jet] Respawned at %s."),
		*RespawnTransform.GetLocation().ToCompactString());
}

void AFCS_JetPawn::TickCamera(float DeltaSeconds, const FJetFlightParams& P)
{
	// The right stick engages free look by being deflected at all. It is the only free
	// look control on a pad, so requiring a separate hold button would waste one.
	const bool bStickLooking = StickLookInput.SizeSquared() > FMath::Square(P.StickLookEngageThreshold);

	if (bFreeLookHeld || bStickLooking)
	{
		// Raw mouse Y is negative when pushed forward while stick Y is positive, so both
		// are normalised to "positive means look up" before the invert preference applies.
		const float LookRight = LookInput.X * P.FreeLookSensitivity
			+ StickLookInput.X * P.FreeLookStickRate * DeltaSeconds;
		const float LookUp = -LookInput.Y * P.FreeLookSensitivity
			+ StickLookInput.Y * P.FreeLookStickRate * DeltaSeconds;

		const float PitchSign = P.bInvertFreeLookPitch ? -1.f : 1.f;

		FreeLookOffset.X = FMath::Clamp(FreeLookOffset.X + LookRight,
			-P.FreeLookYawLimit, P.FreeLookYawLimit);
		FreeLookOffset.Y = FMath::Clamp(FreeLookOffset.Y + PitchSign * LookUp,
			-P.FreeLookPitchLimit, P.FreeLookPitchLimit);
	}
	else
	{
		FreeLookOffset = FMath::Vector2DInterpTo(
			FreeLookOffset, FVector2D::ZeroVector, DeltaSeconds, P.FreeLookReturnSpeed);
	}

	LookInput = FVector2D::ZeroVector;

	// Reapplied every frame so edits to the tuning asset take effect while flying, and so
	// the lag flags a respawn snap turned off come back on their own.
	ApplyCameraParams(P);

	FRotator ArmRotation(FreeLookOffset.Y, FreeLookOffset.X, 0.f);

	// Impact shake. Two different frequencies so the pitch and yaw components do not move
	// in lockstep, which would read as a single clean wobble rather than a jolt.
	if (bCrashed && CrashElapsed < P.BlastDuration && P.ShakeAmplitude > 0.f)
	{
		const float Decay = FMath::Square(1.f - FMath::Clamp(CrashElapsed / P.BlastDuration, 0.f, 1.f));
		const float Amount = P.ShakeAmplitude * Decay;
		ArmRotation.Pitch += Amount * FMath::Sin(CrashElapsed * P.ShakeFrequency);
		ArmRotation.Yaw += Amount * FMath::Sin(CrashElapsed * P.ShakeFrequency * 1.37f + 1.1f);
		ArmRotation.Roll += Amount * 0.5f * FMath::Sin(CrashElapsed * P.ShakeFrequency * 0.83f + 2.2f);
	}

	SpringArm->SetRelativeRotation(ArmRotation);

	if (CameraSnapFramesLeft > 0)
	{
		SpringArm->bEnableCameraLag = false;
		SpringArm->bEnableCameraRotationLag = false;
		--CameraSnapFramesLeft;
	}

	const float TargetFOV = P.BaseFOV + (bAfterburnerHeld ? P.AfterburnerFOVBonus : 0.f);
	Camera->SetFieldOfView(
		FMath::FInterpTo(Camera->FieldOfView, TargetFOV, DeltaSeconds, P.FOVInterpSpeed));
}

void AFCS_JetPawn::DrawReadout(const FJetFlightParams& P) const
{
	if (!P.bShowFlightReadout || !GEngine)
	{
		return;
	}

	// The on screen debug font has no CJK glyphs, so this readout stays ASCII only.
	const int32 Filled = FMath::RoundToInt(Throttle * 10.f);
	FString Bar;
	for (int32 i = 0; i < 10; ++i)
	{
		Bar += (i < Filled) ? TEXT("|") : TEXT(".");
	}

	GEngine->AddOnScreenDebugMessage(101, 0.f, FColor::Cyan,
		FString::Printf(TEXT("SPD  %6.0f km/h   (%5.0f m/s)"),
			CurrentSpeed * CmsToKmh, CurrentSpeed * 0.01f));

	GEngine->AddOnScreenDebugMessage(102, 0.f, bAfterburnerHeld ? FColor::Orange : FColor::Green,
		FString::Printf(TEXT("THR  [%s] %3.0f%%%s"), *Bar, Throttle * 100.f,
			bAfterburnerHeld ? TEXT("   AFTERBURNER") : TEXT("")));

	GEngine->AddOnScreenDebugMessage(103, 0.f, FColor::White,
		FString::Printf(TEXT("RATE P %6.1f   Y %6.1f   R %6.1f  deg/s"),
			CurrentRates.X, CurrentRates.Y, CurrentRates.Z));

	const float GravityScale = GetGravityScale();
	GEngine->AddOnScreenDebugMessage(106, 0.f,
		GravityScale > 0.f ? FColor::Yellow : FColor::Cyan,
		FString::Printf(TEXT("ALT %7.0f m   GRAV %3.0f%%%s   VS %+6.0f m/s"),
			GetActorLocation().Z * 0.01f,
			GravityScale * 100.f,
			GravityScale > 0.f ? TEXT("") : TEXT("  ZERO-G"),
			-FallSpeed * 0.01f));

	if (bFreeLookHeld)
	{
		GEngine->AddOnScreenDebugMessage(104, 0.f, FColor::Yellow,
			FString::Printf(TEXT("FREE LOOK  yaw %5.0f  pitch %5.0f"),
				FreeLookOffset.X, FreeLookOffset.Y));
	}
	else
	{
		GEngine->RemoveOnScreenDebugMessage(104);
	}

	if (bAutoLevelHeld)
	{
		const FRotator Now = GetActorRotation();
		GEngine->AddOnScreenDebugMessage(108, 0.f, FColor::Orange,
			FString::Printf(TEXT("AUTO-LEVEL   pitch %+5.1f   roll %+6.1f"),
				FRotator::NormalizeAxis(Now.Pitch), FRotator::NormalizeAxis(Now.Roll)));
	}
	else
	{
		GEngine->RemoveOnScreenDebugMessage(108);
	}

	if (bCrashed)
	{
		GEngine->AddOnScreenDebugMessage(105, 0.f, FColor::Red,
			FString::Printf(TEXT("GROUND IMPACT  %.0f km/h  -  respawn in %.1f s"),
				LastImpactSpeed * CmsToKmh,
				FMath::Max(0.f, P.RespawnDelay - CrashElapsed)));
		GEngine->RemoveOnScreenDebugMessage(107);
	}
	else
	{
		GEngine->RemoveOnScreenDebugMessage(105);

		// The number that decides the outcome, live, next to the threshold it is judged
		// against. Reporting it only after the fact would be useless for staying alive.
		if (bImpactPredicted)
		{
			const bool bFatal = PredictedImpactSpeed >= P.MinCrashImpactSpeed;
			GEngine->AddOnScreenDebugMessage(107, 0.f,
				bFatal ? FColor::Red : FColor::Green,
				FString::Printf(TEXT("IMPACT %5.0f km/h in %4.1f s   limit %.0f   %s"),
					PredictedImpactSpeed * CmsToKmh,
					PredictedImpactTime,
					P.MinCrashImpactSpeed * CmsToKmh,
					bFatal ? TEXT("FATAL") : TEXT("survivable")));
		}
		else
		{
			GEngine->AddOnScreenDebugMessage(107, 0.f, FColor::Silver,
				FString::Printf(TEXT("IMPACT     -              limit %.0f   clear"),
					P.MinCrashImpactSpeed * CmsToKmh));
		}
	}
}
