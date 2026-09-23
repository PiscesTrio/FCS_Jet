// FCS_Jet - space flight pawn (no aerodynamics)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FCS_JetPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FHitResult;
struct FInputActionValue;

/**
 * Space fighter with War Thunder style keyboard controls.
 *
 * No aerodynamics is simulated: there is no lift, drag, stall or gravity. The ship
 * simply translates along its nose direction at the current speed. The handling feel
 * comes from first order lag on the angular rates (current rate interpolates toward
 * the commanded rate) rather than from aerodynamic moments.
 *
 * Bindings: W nose down / S nose up / A D roll / Q E yaw
 *           Shift throttle up / Ctrl throttle down / Space afterburner
 *           Hold right mouse button for free look
 */
UCLASS()
class FCS_JET_API AFCS_JetPawn : public APawn
{
	GENERATED_BODY()

public:
	AFCS_JetPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	/** Current throttle setting, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetThrottle() const { return Throttle; }

	/** Current speed in cm/s. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetCurrentSpeed() const { return CurrentSpeed; }

	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsAfterburnerOn() const { return bAfterburnerHeld; }

	/** Downward speed accumulated by gravity, cm/s. Positive means falling. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetFallSpeed() const { return FallSpeed; }

	/** How much gravity is acting right now, 1 deep in the well and 0 out in space. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetGravityScale() const;

	/** True between hitting the ground and respawning. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsCrashed() const { return bCrashed; }

	/** Seconds elapsed since the crash, or 0 while flying. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetCrashElapsed() const { return bCrashed ? CrashElapsed : 0.f; }

protected:
	// ---------- Components ----------

	/**
	 * Clean root node.
	 * The mesh needs a -90 degree yaw correction (see MeshYawCorrection in the cpp).
	 * Parenting the spring arm under the mesh would force that yaw to be cancelled out
	 * at every site that touches the arm, so the two are kept as siblings instead.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ShipRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ShipMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * Blast sphere for the ground impact. Hidden while flying.
	 * Kept in absolute location so it stays at the impact point while the wreck and the
	 * camera carry on moving, and so the respawn teleport does not drag it along.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crash", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BlastMesh;

	/** Ground illumination for the blast. Also absolute, for the same reason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crash", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPointLightComponent> BlastLight;

	// ---------- Gravity ----------

	/**
	 * Downward acceleration inside the gravity well, cm/s^2. 980 is Earth.
	 *
	 * There is still no aerodynamics here: gravity is a separate vertical velocity that
	 * accumulates, and the only way to climb is to point the nose up and let the thrust
	 * vector out-run it. Nothing generates lift.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Gravity", meta = (ClampMin = "0.0"))
	float GravityAcceleration = 980.f;

	/** World Z at or below which gravity acts at full strength. 50000 cm is 500 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Gravity")
	float GravityCeilingZ = 50000.f;

	/**
	 * Band above the ceiling across which gravity fades out.
	 * A hard on/off boundary makes the ship lurch as it crosses the line, so the
	 * transition is spread over this distance instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Gravity", meta = (ClampMin = "0.0"))
	float GravityFalloffRange = 20000.f;

	/**
	 * Terminal fall speed, cm/s. Without a cap the fall would grow without bound, since
	 * nothing here produces drag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Gravity", meta = (ClampMin = "0.0"))
	float MaxFallSpeed = 3500.f;

	/**
	 * How fast an inherited descent bleeds away once the ship is clear of the falloff
	 * band, per second. 0 keeps the descent forever.
	 *
	 * Strictly, momentum in a vacuum should be conserved. This model is not a momentum
	 * model though: cutting the throttle drops the forward speed immediately with nothing
	 * carried over, so holding the vertical component to a stricter standard than the
	 * horizontal one is inconsistent. It also contradicts the ZERO-G readout, since a
	 * ship that is visibly sinking is not what anyone reads "no gravity" to mean.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Gravity", meta = (ClampMin = "0.0"))
	float ZeroGFallDamping = 3.f;

	// ---------- Crash ----------

	/** Turn ground impacts off entirely and fly through everything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash")
	bool bCrashEnabled = true;

	/**
	 * Impact speed along the surface normal needed to destroy the ship, cm/s.
	 *
	 * The test is the normal component rather than the total speed on purpose: skimming
	 * the deck at full throttle but level should scrape, not detonate, while any real
	 * dive puts almost all of the speed into the normal and blows up as expected.
	 * Below the threshold the ship touches down and slides along the surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.0"))
	float MinCrashImpactSpeed = 2500.f;

	/**
	 * Radius of the sphere swept along the flight path each frame.
	 * The hull half extents are roughly 387 x 413 x 299 cm, so this is the solid core
	 * rather than the wingtips: clipping a wingtip on the deck should not be fatal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "1.0"))
	float HullRadius = 300.f;

	/** Seconds from impact to respawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.1"))
	float RespawnDelay = 2.5f;

	/** Seconds the blast takes to expand and fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.05"))
	float BlastDuration = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "1.0"))
	float BlastMaxRadius = 1100.f;

	/**
	 * Emissive multiplier at the instant of impact, decaying to zero over BlastDuration.
	 * This is an additive unlit material, so anything much above about 8 drives every
	 * channel past white and the fireball reads as a flat white disc with no colour left.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.0"))
	float BlastPeakStrength = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash")
	FLinearColor BlastColor = FLinearColor(1.0f, 0.42f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.0"))
	float BlastLightPeakIntensity = 250000.f;

	/** Peak camera shake in degrees, decaying over BlastDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.0"))
	float ShakeAmplitude = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.1"))
	float ShakeFrequency = 26.f;

	// ---------- Speed ----------

	/** Cruise speed at 100% throttle, cm/s. 12000 = 120 m/s = 432 km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Speed", meta = (ClampMin = "0.0"))
	float CruiseSpeed = 12000.f;

	/** Speed at 0% throttle. A spacecraft can genuinely come to rest, hence 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Speed", meta = (ClampMin = "0.0"))
	float IdleSpeed = 0.f;

	/** Afterburner multiplier applied on top of the cruise speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Speed", meta = (ClampMin = "1.0"))
	float AfterburnerMultiplier = 2.5f;

	/** Throttle change per second. 0.5 means a full sweep takes two seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Speed", meta = (ClampMin = "0.01"))
	float ThrottleChangeRate = 0.5f;

	/** How quickly actual speed catches up to the commanded speed (thrust lag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Speed", meta = (ClampMin = "0.01"))
	float SpeedInterpSpeed = 1.2f;

	/** Speed response while the afterburner is lit. Deliberately punchier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Speed", meta = (ClampMin = "0.01"))
	float AfterburnerInterpSpeed = 3.0f;

	// ---------- Handling ----------

	/** Maximum pitch rate, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Handling", meta = (ClampMin = "1.0"))
	float PitchRate = 65.f;

	/** Maximum roll rate. Fighters roll far faster than they pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Handling", meta = (ClampMin = "1.0"))
	float RollRate = 140.f;

	/** Maximum yaw rate. The slowest axis, as on a real fighter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Handling", meta = (ClampMin = "1.0"))
	float YawRate = 40.f;

	/**
	 * How fast the angular rate chases the commanded rate. This is the single knob
	 * that decides how heavy the ship feels.
	 * Higher = crisper and more twitchy, lower = heavier with more inertia.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Handling", meta = (ClampMin = "0.1"))
	float ControlInterpSpeed = 5.f;

	// ---------- Camera ----------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.01"))
	float FreeLookSensitivity = 2.2f;

	/** How fast the view recenters after the right mouse button is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1"))
	float FreeLookReturnSpeed = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float FreeLookYawLimit = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float FreeLookPitchLimit = 80.f;

	/** Invert the free look vertical axis. Purely a matter of personal preference. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bInvertFreeLookPitch = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "30.0", ClampMax = "170.0"))
	float BaseFOV = 90.f;

	/** Extra field of view while the afterburner is lit, to sell the speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float AfterburnerFOVBonus = 14.f;

	/** Draw speed / throttle / rate readout in the corner of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowFlightReadout = true;

	// ---------- Input assets ----------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PitchAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RollAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> YawAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ThrottleAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AfterburnerAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> FreeLookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

private:
	/**
	 * Push the mapping context into the local player's Enhanced Input subsystem.
	 *
	 * Called from both NotifyControllerChanged and SetupPlayerInputComponent because
	 * the first of those can run before the controller has a local player attached,
	 * in which case the subsystem does not exist yet and the call is a no-op.
	 * AddMappingContext is idempotent, so running it twice is harmless.
	 */
	void ApplyMappingContext(const TCHAR* CallSite);

	// ---------- Input handlers ----------

	void OnPitch(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnYaw(const FInputActionValue& Value);
	void OnThrottle(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);

	void OnPitchReleased();
	void OnRollReleased();
	void OnYawReleased();
	void OnThrottleReleased();

	void OnAfterburnerStart();
	void OnAfterburnerStop();
	void OnFreeLookStart();
	void OnFreeLookStop();

	// ---------- Per frame steps ----------

	void TickThrottle(float DeltaSeconds);
	void TickAttitude(float DeltaSeconds);
	void TickGravity(float DeltaSeconds);
	void TickTranslation(float DeltaSeconds);
	void TickCrash(float DeltaSeconds);
	void TickCamera(float DeltaSeconds);
	void DrawReadout() const;

	/** Gravity strength at a given world height: 1 below the ceiling, 0 well above it. */
	float GetGravityScaleAtZ(float WorldZ) const;

	/** Touched down too slowly to break: kill the descent and slide along the surface. */
	void TouchDown(const FHitResult& Hit, const FVector& RemainingDelta);

	void BeginCrash(const FHitResult& Hit, float ImpactSpeed);
	void Respawn();

	// ---------- Runtime state ----------

	/** Commanded input for this frame, -1..1. */
	float PitchInput = 0.f;
	float RollInput = 0.f;
	float YawInput = 0.f;
	float ThrottleInput = 0.f;

	/** Accumulated mouse delta, consumed and cleared every frame. */
	FVector2D LookInput = FVector2D::ZeroVector;

	bool bAfterburnerHeld = false;
	bool bFreeLookHeld = false;

	/** Throttle setting, 0..1. Holds its value when the key is released. */
	float Throttle = 0.35f;

	/** Current speed, cm/s. */
	float CurrentSpeed = 0.f;

	/** Current angular rates in degrees per second: X = pitch, Y = yaw, Z = roll. */
	FVector CurrentRates = FVector::ZeroVector;

	/** Free look offset in degrees: X = yaw, Y = pitch. */
	FVector2D FreeLookOffset = FVector2D::ZeroVector;

	/** Downward speed from gravity, cm/s. Positive means falling. */
	float FallSpeed = 0.f;

	/** Set between impact and respawn. All flight integration is skipped while true. */
	bool bCrashed = false;

	/** Seconds since the impact. */
	float CrashElapsed = 0.f;

	/** Where to put the ship back. Captured once at BeginPlay from the spawn transform. */
	FTransform RespawnTransform;

	/** Drives FlashStrength on the blast sphere. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BlastMaterial;

	/**
	 * Frames of camera lag suppression still owed after a respawn teleport. Without this
	 * the lagged camera sweeps across the whole map to catch up with the ship.
	 */
	int32 CameraSnapFramesLeft = 0;

	/** Lag flags as authored, captured at BeginPlay so the snap can restore them. */
	bool bDefaultCameraLag = true;
	bool bDefaultCameraRotationLag = true;
};
