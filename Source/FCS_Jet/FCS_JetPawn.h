// FCS_Jet - space flight pawn (no aerodynamics)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FCS_JetPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
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
	void TickTranslation(float DeltaSeconds);
	void TickCamera(float DeltaSeconds);
	void DrawReadout() const;

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
};
