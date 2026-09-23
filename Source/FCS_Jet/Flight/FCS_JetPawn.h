// FCS_Jet - space flight pawn (no aerodynamics)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Flight/JetFlightParams.h"
#include "Flight/JetTelemetry.h"
#include "FCS_JetPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UJetTuning;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FHitResult;
struct FInputActionValue;

/**
 * Space fighter with War Thunder style controls.
 *
 * No aerodynamics is simulated: no lift, no drag, no stall. Thrust points along the nose
 * and gravity adds a separate vertical term below a set altitude, so the only way to
 * climb is to point the nose up and out-run the descent. The handling feel comes from
 * first order lag on the angular rates rather than from aerodynamic moments.
 *
 * Every tunable lives in FJetFlightParams, reached through GetParams. The pawn keeps no
 * tunables of its own so they can be edited while flying, from a UJetTuning asset.
 *
 * Bindings: W nose down / S nose up / A D roll / Q E yaw / Shift throttle up
 *           Ctrl throttle down / Space afterburner / R level off
 *           Hold right mouse button for free look
 *           Left stick pitch and roll / LB RB yaw / RT LT throttle / Y level off
 *           Right stick free look / A afterburner
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

	/** True when the current trajectory runs into something within the warning window. */
	UFUNCTION(BlueprintPure, Category = "Flight|Impact")
	bool HasPredictedImpact() const { return bImpactPredicted; }

	/**
	 * How hard the ship would hit if it held this trajectory, cm/s, measured the same way
	 * the crash test measures it: the closing speed along the surface normal.
	 * Zero when nothing is ahead.
	 */
	UFUNCTION(BlueprintPure, Category = "Flight|Impact")
	float GetPredictedImpactSpeed() const { return PredictedImpactSpeed; }

	/** Seconds until that impact, or 0 when nothing is ahead. */
	UFUNCTION(BlueprintPure, Category = "Flight|Impact")
	float GetPredictedImpactTime() const { return PredictedImpactTime; }

	/** True when the predicted impact is hard enough to destroy the ship. */
	UFUNCTION(BlueprintPure, Category = "Flight|Impact")
	bool IsPredictedImpactFatal() const;

	/**
	 * The tunables in force right now: the Tuning asset's, or the code defaults when no
	 * asset is assigned.
	 *
	 * Read fresh every frame rather than cached. Caching would be marginally cheaper and
	 * would also defeat the entire point, since edits to the asset mid-flight would stop
	 * taking effect.
	 */
	const FJetFlightParams& GetParams() const;

	/** Normal speed of the impact that destroyed the ship, cm/s. Kept until the next one. */
	UFUNCTION(BlueprintPure, Category = "Flight|Impact")
	float GetLastImpactSpeed() const { return LastImpactSpeed; }

	/** True while the level-off button is held. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsAutoLevelling() const { return bAutoLevelHeld; }

	/**
	 * Commanded body angular rates in degrees per second: X pitch, Y yaw, Z roll.
	 *
	 * Exposed because the Euler angle derivative is not a sound proxy for it. The two
	 * axes are coupled while banked, so differentiating the attitude readout can report a
	 * rate well above what was actually commanded.
	 */
	UFUNCTION(BlueprintPure, Category = "Flight")
	FVector GetAngularRates() const { return CurrentRates; }

	/** One frame of state, gathered for display. */
	UFUNCTION(BlueprintPure, Category = "Flight")
	FJetTelemetry GetTelemetry() const { return BuildTelemetry(GetParams()); }

protected:
	// ---------- Components ----------

	/**
	 * Clean root node.
	 * The mesh carries a yaw correction into the control frame (MeshYawOffsetDeg).
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

	// ---------- Tuning ----------

	/**
	 * Where the tunables come from. Assigned on the Blueprint child so a designer can
	 * point the ship at a different tuning without touching code.
	 *
	 * Null is a supported state: GetParams falls back to the code defaults, so a missing
	 * or detached asset degrades the ship to its authored values rather than breaking it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight")
	TObjectPtr<UJetTuning> Tuning;

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
	TObjectPtr<UInputAction> AutoLevelAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> FreeLookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** Right stick free look. Separate from LookAction because a stick is not a delta. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookStickAction;

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
	void OnLookStick(const FInputActionValue& Value);
	void OnLookStickReleased();

	void OnPitchReleased();
	void OnRollReleased();
	void OnYawReleased();
	void OnThrottleReleased();

	void OnAfterburnerStart();
	void OnAfterburnerStop();
	void OnAutoLevelStart();
	void OnAutoLevelStop();
	void OnFreeLookStart();
	void OnFreeLookStop();

	// ---------- Per frame steps ----------
	//
	// Every step takes the params by reference rather than reaching for them itself, so
	// one frame always runs on one consistent set of values even if the asset is edited
	// mid-frame from the details panel.

	void TickThrottle(float DeltaSeconds, const FJetFlightParams& P);
	void TickAttitude(float DeltaSeconds, const FJetFlightParams& P);
	void TickGravity(float DeltaSeconds, const FJetFlightParams& P);
	void TickImpactPrediction(const FJetFlightParams& P);
	void TickTranslation(float DeltaSeconds, const FJetFlightParams& P);
	void TickCrash(float DeltaSeconds, const FJetFlightParams& P);
	void TickCamera(float DeltaSeconds, const FJetFlightParams& P);

	/** Gather this frame's state for the readout. */
	FJetTelemetry BuildTelemetry(const FJetFlightParams& P) const;

	/** Push the camera and spring arm tunables onto the components. */
	void ApplyCameraParams(const FJetFlightParams& P);

	/** Push the mesh frame tunables onto the mesh component. */
	void ApplyMeshParams(const FJetFlightParams& P);

	/** Touched down too slowly to break: kill the descent and slide along the surface. */
	void TouchDown(const FHitResult& Hit, const FVector& RemainingDelta);

	void BeginCrash(const FHitResult& Hit, float ImpactSpeed, const FJetFlightParams& P);
	void Respawn(const FJetFlightParams& P);

	// ---------- Runtime state ----------

	/**
	 * Authored defaults, used whenever no Tuning asset is assigned.
	 *
	 * A plain member rather than a static, so the values come from FJetFlightParams's own
	 * initialisers and there is exactly one place where a default is written down.
	 */
	FJetFlightParams DefaultParams;

	/** Commanded input for this frame, -1..1. */
	float PitchInput = 0.f;
	float RollInput = 0.f;
	float YawInput = 0.f;
	float ThrottleInput = 0.f;

	/** Accumulated mouse delta, consumed and cleared every frame. */
	FVector2D LookInput = FVector2D::ZeroVector;

	/**
	 * Right stick deflection, -1..1 per axis. Held rather than cleared, since the stick
	 * reports a sustained position and its own Completed event zeroes it on release.
	 */
	FVector2D StickLookInput = FVector2D::ZeroVector;

	bool bAfterburnerHeld = false;
	bool bFreeLookHeld = false;
	bool bAutoLevelHeld = false;

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

	/** Impact readout, refreshed every frame by TickImpactPrediction. */
	bool bImpactPredicted = false;
	float PredictedImpactSpeed = 0.f;
	float PredictedImpactTime = 0.f;

	/** Normal speed of the last crash, held so the wreck readout can report it. */
	float LastImpactSpeed = 0.f;

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
	 *
	 * ApplyCameraParams runs at the top of every TickCamera, so the flags come back from
	 * the params on their own once this reaches zero.
	 */
	int32 CameraSnapFramesLeft = 0;

};
