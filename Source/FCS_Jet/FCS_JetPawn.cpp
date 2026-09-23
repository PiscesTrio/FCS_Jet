// FCS_Jet - space flight pawn (no aerodynamics)

#include "FCS_JetPawn.h"

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
	 * The TIE mesh points its nose along +Y while an UE pawn moves along +X, so the
	 * mesh needs a -90 degree yaw correction.
	 *
	 * This is measured, not assumed: the per material section vertex centroids put the
	 * RedEngineGlow (engine) section at Y = -110, the Dark (wingtip cannons) section at
	 * Y = +437 and the DarkWindow (cockpit viewport) section at Y = +46. The nose is
	 * unambiguously +Y.
	 */
	constexpr float MeshYawCorrection = -90.f;

	/**
	 * Centroid of the cockpit ball (the PolishedMetalDark section) in mesh local space.
	 * Moving it onto the pawn origin makes the ship rotate about its cockpit rather than
	 * about the mesh origin, which is what makes the handling feel right.
	 *
	 * After the yaw correction above, (0, 34.3, -12.2) lands at (34.3, 0, -12.2), so the
	 * mesh offset is the negation of that.
	 */
	const FVector MeshPivotOffset(-34.3f, 0.f, 12.2f);

	/** cm/s to km/h. */
	constexpr float CmsToKmh = 0.036f;

	/** How fast the field of view eases toward its target. */
	constexpr float FOVInterpSpeed = 4.f;

	/** /Engine/BasicShapes/Sphere is 100 units across, so scale 1 is a 50 unit radius. */
	constexpr float BasicSphereRadius = 50.f;

	/** Below this the ship is effectively parked and the ground sweep is pointless. */
	constexpr float MinSweepSpeed = 1.f;
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
	ShipMesh->SetRelativeRotation(FRotator(0.f, MeshYawCorrection, 0.f));
	ShipMesh->SetRelativeLocation(MeshPivotOffset);
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
	SpringArm->TargetArmLength = 1400.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 250.f);
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 12.f;
	// Without this the arm collides with the hull and the ground and the view jitters.
	SpringArm->bDoCollisionTest = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(BaseFOV);

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
	BlastLight->SetAttenuationRadius(6000.f);
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
	static ConstructorHelpers::FObjectFinder<UInputAction> FreeLookAsset(
		TEXT("/Game/FCS_Jet/Input/IA_FreeLook.IA_FreeLook"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookAsset(
		TEXT("/Game/FCS_Jet/Input/IA_Look.IA_Look"));

	DefaultMappingContext = ImcAsset.Succeeded() ? ImcAsset.Object : nullptr;
	PitchAction = PitchAsset.Succeeded() ? PitchAsset.Object : nullptr;
	RollAction = RollAsset.Succeeded() ? RollAsset.Object : nullptr;
	YawAction = YawAsset.Succeeded() ? YawAsset.Object : nullptr;
	ThrottleAction = ThrottleAsset.Succeeded() ? ThrottleAsset.Object : nullptr;
	AfterburnerAction = AfterburnerAsset.Succeeded() ? AfterburnerAsset.Object : nullptr;
	FreeLookAction = FreeLookAsset.Succeeded() ? FreeLookAsset.Object : nullptr;
	LookAction = LookAsset.Succeeded() ? LookAsset.Object : nullptr;
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

	if (SpringArm)
	{
		bDefaultCameraLag = SpringArm->bEnableCameraLag;
		bDefaultCameraRotationLag = SpringArm->bEnableCameraRotationLag;
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
void AFCS_JetPawn::OnFreeLookStart() { bFreeLookHeld = true; }
void AFCS_JetPawn::OnFreeLookStop() { bFreeLookHeld = false; }

void AFCS_JetPawn::OnLook(const FInputActionValue& Value)
{
	// The mouse reports a delta, so accumulate here and let TickCamera consume it.
	LookInput += Value.Get<FVector2D>();
}

void AFCS_JetPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCrashed)
	{
		// No throttle, no attitude, no translation. The wreck just sits there.
		TickCrash(DeltaSeconds);
	}
	else
	{
		TickThrottle(DeltaSeconds);
		TickAttitude(DeltaSeconds);
		TickGravity(DeltaSeconds);
		TickTranslation(DeltaSeconds);
	}

	TickCamera(DeltaSeconds);
	DrawReadout();
}

void AFCS_JetPawn::TickThrottle(float DeltaSeconds)
{
	if (!FMath::IsNearlyZero(ThrottleInput))
	{
		Throttle = FMath::Clamp(
			Throttle + ThrottleInput * ThrottleChangeRate * DeltaSeconds, 0.f, 1.f);
	}

	const float Boost = bAfterburnerHeld ? AfterburnerMultiplier : 1.f;
	const float TargetSpeed = FMath::Lerp(IdleSpeed, CruiseSpeed, Throttle) * Boost;
	const float Responsiveness = bAfterburnerHeld ? AfterburnerInterpSpeed : SpeedInterpSpeed;

	CurrentSpeed = FMath::FInterpTo(CurrentSpeed, TargetSpeed, DeltaSeconds, Responsiveness);
}

void AFCS_JetPawn::TickAttitude(float DeltaSeconds)
{
	// There are no aerodynamic moments, so the entire handling character lives in this
	// one interpolation.
	const FVector TargetRates(PitchInput * PitchRate, YawInput * YawRate, RollInput * RollRate);
	CurrentRates = FMath::VInterpTo(CurrentRates, TargetRates, DeltaSeconds, ControlInterpSpeed);

	const FRotator DeltaRotation(
		CurrentRates.X * DeltaSeconds,   // Pitch
		CurrentRates.Y * DeltaSeconds,   // Yaw
		CurrentRates.Z * DeltaSeconds);  // Roll

	// Compose the local rotation as a quaternion. A spacecraft needs full 360 degree
	// freedom on every axis and accumulating Euler angles would eventually gimbal lock.
	AddActorLocalRotation(DeltaRotation.Quaternion());
}

float AFCS_JetPawn::GetGravityScaleAtZ(float WorldZ) const
{
	if (WorldZ <= GravityCeilingZ)
	{
		return 1.f;
	}
	if (GravityFalloffRange <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const float Above = WorldZ - GravityCeilingZ;
	return FMath::Clamp(1.f - Above / GravityFalloffRange, 0.f, 1.f);
}

float AFCS_JetPawn::GetGravityScale() const
{
	return GetGravityScaleAtZ(GetActorLocation().Z);
}

void AFCS_JetPawn::TickGravity(float DeltaSeconds)
{
	const float Scale = GetGravityScaleAtZ(GetActorLocation().Z);

	if (Scale > 0.f)
	{
		FallSpeed = FMath::Min(FallSpeed + GravityAcceleration * Scale * DeltaSeconds, MaxFallSpeed);
	}
	else if (ZeroGFallDamping > 0.f)
	{
		// Clear of the band, a descent carried up from below bleeds away instead of being
		// kept forever. See ZeroGFallDamping for why conserving it is the wrong call here.
		//
		// Damping is gated on the band being fully behind the ship rather than scaled
		// across it. Blending the two against each other inside the band would give
		// partial gravity its own much lower terminal velocity, which is not what a
		// thinning gravity well should feel like.
		FallSpeed = FMath::FInterpTo(FallSpeed, 0.f, DeltaSeconds, ZeroGFallDamping);
	}
}

void AFCS_JetPawn::TickTranslation(float DeltaSeconds)
{
	// Thrust always points along the nose, with no sideslip and no carried momentum, and
	// gravity adds a separate vertical term on top. Still no lift or drag anywhere.
	const FVector Velocity = GetActorForwardVector() * CurrentSpeed + FVector(0.f, 0.f, -FallSpeed);
	const FVector Delta = Velocity * DeltaSeconds;

	// Collision is detected by sweeping a sphere along the path rather than by enabling
	// collision on the hull and using swept movement. Swept movement would leave the ship
	// resting against, and frequently wedged into, whatever it touched; here the hit is
	// only ever a trigger and the response is entirely ours.
	if (bCrashEnabled && Velocity.Size() > MinSweepSpeed && GetWorld())
	{
		const FVector Start = GetActorLocation();
		const FVector End = Start + Delta;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(FCSJetGroundSweep), /*bTraceComplex=*/false);
		Params.AddIgnoredActor(this);

		FHitResult Hit;
		const bool bBlocked = GetWorld()->SweepSingleByChannel(
			Hit, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(HullRadius), Params);

		if (bBlocked)
		{
			// Close the gap up to the surface first, whatever happens next.
			AddActorWorldOffset(Hit.Location - Start, /*bSweep=*/false);

			// How hard the surface was actually hit, rather than how fast the ship was
			// travelling. See MinCrashImpactSpeed.
			const float ImpactSpeed = FMath::Abs(FVector::DotProduct(Velocity, Hit.ImpactNormal));

			if (ImpactSpeed >= MinCrashImpactSpeed)
			{
				BeginCrash(Hit, ImpactSpeed);
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

void AFCS_JetPawn::BeginCrash(const FHitResult& Hit, float ImpactSpeed)
{
	bCrashed = true;
	CrashElapsed = 0.f;

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
		BlastMaterial->SetVectorParameterValue(TEXT("FlashColor"), BlastColor);
		BlastMaterial->SetScalarParameterValue(TEXT("FlashStrength"), BlastPeakStrength);
	}
	if (BlastLight)
	{
		// Lift the light off the deck so it reads as a fireball rather than a floor decal.
		BlastLight->SetWorldLocation(ImpactPoint + FVector(0.f, 0.f, 300.f));
		BlastLight->SetLightColor(BlastColor);
		BlastLight->SetIntensity(BlastLightPeakIntensity);
		BlastLight->SetVisibility(true);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[FCS_Jet] Ground impact on %s at %s. Normal speed %.0f km/h, threshold %.0f km/h. "
			 "Respawning in %.1f s."),
		*GetNameSafe(Hit.GetActor()), *ImpactPoint.ToCompactString(),
		ImpactSpeed * CmsToKmh, MinCrashImpactSpeed * CmsToKmh, RespawnDelay);
}

void AFCS_JetPawn::TickCrash(float DeltaSeconds)
{
	CrashElapsed += DeltaSeconds;

	const float Alpha = FMath::Clamp(CrashElapsed / BlastDuration, 0.f, 1.f);
	const float Fade = FMath::Square(1.f - Alpha);

	if (BlastMesh && BlastMesh->IsVisible())
	{
		// Ease out so the fireball punches open and then slows, rather than growing linearly.
		const float Radius = BlastMaxRadius * (1.f - FMath::Square(1.f - Alpha));
		BlastMesh->SetWorldScale3D(FVector(Radius / BasicSphereRadius));

		if (BlastMaterial)
		{
			BlastMaterial->SetScalarParameterValue(TEXT("FlashStrength"), BlastPeakStrength * Fade);
		}
		if (Alpha >= 1.f)
		{
			BlastMesh->SetVisibility(false);
		}
	}

	if (BlastLight && BlastLight->IsVisible())
	{
		BlastLight->SetIntensity(BlastLightPeakIntensity * Fade * (1.f - Alpha));
		if (Alpha >= 1.f)
		{
			BlastLight->SetIntensity(0.f);
			BlastLight->SetVisibility(false);
		}
	}

	if (CrashElapsed >= RespawnDelay)
	{
		Respawn();
	}
}

void AFCS_JetPawn::Respawn()
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

void AFCS_JetPawn::TickCamera(float DeltaSeconds)
{
	if (bFreeLookHeld)
	{
		// Raw mouse Y is negative when the mouse is pushed forward, so the sign is
		// flipped to make "push the mouse up" mean "look up".
		const float PitchSign = bInvertFreeLookPitch ? 1.f : -1.f;

		FreeLookOffset.X = FMath::Clamp(
			FreeLookOffset.X + LookInput.X * FreeLookSensitivity,
			-FreeLookYawLimit, FreeLookYawLimit);
		FreeLookOffset.Y = FMath::Clamp(
			FreeLookOffset.Y + PitchSign * LookInput.Y * FreeLookSensitivity,
			-FreeLookPitchLimit, FreeLookPitchLimit);
	}
	else
	{
		FreeLookOffset = FMath::Vector2DInterpTo(
			FreeLookOffset, FVector2D::ZeroVector, DeltaSeconds, FreeLookReturnSpeed);
	}

	LookInput = FVector2D::ZeroVector;

	FRotator ArmRotation(FreeLookOffset.Y, FreeLookOffset.X, 0.f);

	// Impact shake. Two different frequencies so the pitch and yaw components do not move
	// in lockstep, which would read as a single clean wobble rather than a jolt.
	if (bCrashed && CrashElapsed < BlastDuration && ShakeAmplitude > 0.f)
	{
		const float Decay = FMath::Square(1.f - FMath::Clamp(CrashElapsed / BlastDuration, 0.f, 1.f));
		const float Amount = ShakeAmplitude * Decay;
		ArmRotation.Pitch += Amount * FMath::Sin(CrashElapsed * ShakeFrequency);
		ArmRotation.Yaw += Amount * FMath::Sin(CrashElapsed * ShakeFrequency * 1.37f + 1.1f);
		ArmRotation.Roll += Amount * 0.5f * FMath::Sin(CrashElapsed * ShakeFrequency * 0.83f + 2.2f);
	}

	SpringArm->SetRelativeRotation(ArmRotation);

	if (CameraSnapFramesLeft > 0)
	{
		SpringArm->bEnableCameraLag = false;
		SpringArm->bEnableCameraRotationLag = false;
		if (--CameraSnapFramesLeft == 0)
		{
			SpringArm->bEnableCameraLag = bDefaultCameraLag;
			SpringArm->bEnableCameraRotationLag = bDefaultCameraRotationLag;
		}
	}

	const float TargetFOV = BaseFOV + (bAfterburnerHeld ? AfterburnerFOVBonus : 0.f);
	Camera->SetFieldOfView(
		FMath::FInterpTo(Camera->FieldOfView, TargetFOV, DeltaSeconds, FOVInterpSpeed));
}

void AFCS_JetPawn::DrawReadout() const
{
	if (!bShowFlightReadout || !GEngine)
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

	if (bCrashed)
	{
		GEngine->AddOnScreenDebugMessage(105, 0.f, FColor::Red,
			FString::Printf(TEXT("GROUND IMPACT  -  respawn in %.1f s"),
				FMath::Max(0.f, RespawnDelay - CrashElapsed)));
	}
	else
	{
		GEngine->RemoveOnScreenDebugMessage(105);
	}
}
