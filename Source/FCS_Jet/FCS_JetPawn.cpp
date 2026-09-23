// FCS_Jet - space flight pawn (no aerodynamics)

#include "FCS_JetPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
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

	TickThrottle(DeltaSeconds);
	TickAttitude(DeltaSeconds);
	TickTranslation(DeltaSeconds);
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

void AFCS_JetPawn::TickTranslation(float DeltaSeconds)
{
	// Velocity always points along the nose: no sideslip and no carried momentum. That is
	// the arcade feel being asked for, not real orbital mechanics.
	AddActorWorldOffset(GetActorForwardVector() * CurrentSpeed * DeltaSeconds, /*bSweep=*/false);
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

	SpringArm->SetRelativeRotation(FRotator(FreeLookOffset.Y, FreeLookOffset.X, 0.f));

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
}
