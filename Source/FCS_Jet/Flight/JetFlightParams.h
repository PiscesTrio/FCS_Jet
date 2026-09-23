// FCS_Jet - every tunable value of the flight model

#pragma once

#include "CoreMinimal.h"
#include "JetFlightParams.generated.h"

/**
 * Every tunable value of the ship, in Unreal units (cm, s, deg).
 *
 * Kept as plain data in its own struct so it can live in a UJetTuning asset, which is
 * what makes these editable while flying instead of only at compile time. The pawn holds
 * no tunables of its own; it reads this every frame through GetParams.
 */
USTRUCT(BlueprintType)
struct FCS_JET_API FJetFlightParams
{
	GENERATED_BODY()

	// ---------- Frame ----------

	/**
	 * Yaw taking the control frame (+X forward) to the mesh frame, degrees.
	 *
	 * Measured, not assumed: the per material section vertex centroids put the TIE's
	 * engine at Y = -110, its wingtip cannons at Y = +437 and the cockpit viewport at
	 * Y = +46, so the mesh nose is +Y while a pawn moves along +X.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	float MeshYawOffsetDeg = -90.f;

	/**
	 * Mesh offset that decides what the ship rotates about, and therefore what the free
	 * look camera orbits, since the spring arm sits on the pawn origin.
	 *
	 * The default puts the cockpit ball centroid on the pawn origin. In mesh space that
	 * centroid is (0, 34.3, -12.2); after the yaw above it lands at (34.3, 0, -12.2),
	 * hence the negation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame")
	FVector MeshPivotOffset = FVector(-34.3f, 0.f, 12.2f);

	// ---------- Flight|Speed ----------

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

	// ---------- Flight|Handling ----------

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

	/**
	 * Proportional gain for the level-off, in commanded degrees per second per degree of
	 * error. 2.5 means a 60 degree bank asks for 150 deg/s, which the roll limit then
	 * clips to 140, so the correction runs at full rate until the last 56 degrees and
	 * eases in from there.
	 *
	 * The commanded rate is clamped to the same PitchRate and RollRate the player gets,
	 * so levelling can never out-turn the ship's own handling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Handling", meta = (ClampMin = "0.1"))
	float AutoLevelGain = 2.5f;

	// ---------- Flight|Gravity ----------

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
	float MinCrashImpactSpeed = 4000.f;

	/**
	 * Radius of the sphere swept along the flight path each frame.
	 * The hull half extents are roughly 387 x 413 x 299 cm, so this is the solid core
	 * rather than the wingtips: clipping a wingtip on the deck should not be fatal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "1.0"))
	float HullRadius = 300.f;

	/**
	 * How far ahead the impact readout looks, expressed in seconds of travel at the
	 * current speed rather than a fixed distance, so the warning arrives with roughly the
	 * same amount of reaction time whether the ship is crawling or on afterburner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.1"))
	float ImpactWarningSeconds = 5.f;

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

	/** Reach of the blast light, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "1.0"))
	float BlastLightAttenuationRadius = 6000.f;

	/** Below this the ship is effectively parked and the ground sweep is pointless, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crash", meta = (ClampMin = "0.0"))
	float MinSweepSpeed = 1.f;

	// ---------- Camera ----------

	/** Mouse free look gain. The mouse reports a delta, so this is not a rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.01"))
	float FreeLookSensitivity = 2.2f;

	/**
	 * Right stick free look rate at full deflection, degrees per second.
	 *
	 * The stick holds a position rather than reporting a delta, so it has to drive a rate
	 * or the pan speed would depend on the frame rate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1.0"))
	float FreeLookStickRate = 140.f;

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

	/** Distance from the pivot back to the camera, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float ArmLength = 1400.f;

	/** Camera offset in the arm's own space, so it rotates with the view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector CameraSocketOffset = FVector(0.f, 0.f, 250.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bEnableCameraLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1"))
	float CameraLagSpeed = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bEnableCameraRotationLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1"))
	float CameraRotationLagSpeed = 12.f;

	/**
	 * Spring arm collision probe. Off by default: with it on the arm is pushed around by
	 * the hull and the ground and the view jitters.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bSpringArmCollisionTest = false;

	/** How fast the field of view eases toward its target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1"))
	float FOVInterpSpeed = 4.f;

	/**
	 * Stick deflection that counts as "the player is looking around".
	 * The mapping already applies a radial deadzone, so anything arriving here is a
	 * deliberate push; this only guards against the last sliver of noise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StickLookEngageThreshold = 0.05f;

	// ---------- Debug ----------

	/** Draw speed / throttle / rate readout in the corner of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowFlightReadout = true;

};
