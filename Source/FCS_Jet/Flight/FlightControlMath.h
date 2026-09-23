// FCS_Jet - the flight model's arithmetic, with no engine state behind it

#pragma once

#include "CoreMinimal.h"
#include "Flight/JetFlightParams.h"

/**
 * The whole control law as free functions over params and plain numbers.
 *
 * Nothing here touches a UObject, a component or the world, so each piece can be reasoned
 * about, and later tested, without standing up a pawn. What is left in the pawn is the
 * part that genuinely needs the engine: sweeps, component writes and input plumbing.
 */
namespace FlightMath
{
	/** Gravity strength at a world height: 1 at or below the ceiling, 0 past the band. */
	FCS_JET_API float GravityScaleAtZ(float WorldZ, const FJetFlightParams& P);

	/** Throttle setting after one frame of input, clamped to 0..1. */
	FCS_JET_API float UpdateThrottle(float Throttle, float ThrottleInput, float DeltaSeconds,
		const FJetFlightParams& P);

	/** Speed the thrust is asking for at this throttle. */
	FCS_JET_API float TargetSpeed(float Throttle, bool bAfterburner, const FJetFlightParams& P);

	/** Speed after one frame of thrust lag. */
	FCS_JET_API float UpdateSpeed(float CurrentSpeed, float Throttle, bool bAfterburner,
		float DeltaSeconds, const FJetFlightParams& P);

	/**
	 * Descent after one frame: gravity accumulates inside the well and bleeds off past the
	 * band. Which of the two applies is decided by GravityScale, not by altitude, so the
	 * caller stays the only place that knows where the ship is.
	 */
	FCS_JET_API float UpdateFallSpeed(float FallSpeed, float GravityScale, float DeltaSeconds,
		const FJetFlightParams& P);

	/**
	 * Commanded body rates in degrees per second: X pitch, Y yaw, Z roll.
	 *
	 * While levelling, pitch and roll are replaced by a correction toward the horizon and
	 * yaw is left with the player. The correction is clamped to the same rate limits the
	 * player gets, so levelling can never out-turn the ship's own handling.
	 */
	FCS_JET_API FVector TargetRates(float PitchInput, float YawInput, float RollInput,
		bool bAutoLevel, const FRotator& Attitude, const FJetFlightParams& P);

	/** Thrust along the nose plus the accumulated descent. */
	FCS_JET_API FVector Velocity(const FVector& Forward, float Speed, float FallSpeed);

	/**
	 * How hard a surface is being closed on: the speed along its normal rather than the
	 * total speed, so skimming a deck is not treated the same as flying into it.
	 */
	FCS_JET_API float ImpactNormalSpeed(const FVector& Velocity, const FVector& Normal);

	/** Free look offset after one frame, or eased back toward centre when not looking. */
	FCS_JET_API FVector2D UpdateFreeLook(const FVector2D& Offset, const FVector2D& MouseDelta,
		const FVector2D& StickInput, bool bLooking, float DeltaSeconds,
		const FJetFlightParams& P);

	/** True once the stick is deflected far enough to count as looking around. */
	FCS_JET_API bool IsStickLooking(const FVector2D& StickInput, const FJetFlightParams& P);

	/** Blast progress, 0 at the impact and 1 once it has finished. */
	FCS_JET_API float BlastAlpha(float CrashElapsed, const FJetFlightParams& P);

	/** Blast sphere radius in cm at this progress. */
	FCS_JET_API float BlastRadius(float Alpha, const FJetFlightParams& P);

	/** Emissive multiplier at this progress. */
	FCS_JET_API float BlastStrength(float Alpha, const FJetFlightParams& P);

	/** Blast light intensity at this progress. */
	FCS_JET_API float BlastLightIntensity(float Alpha, const FJetFlightParams& P);

	/** Decaying impact shake to add to the arm rotation. Zero once the blast is over. */
	FCS_JET_API FRotator ShakeOffset(float CrashElapsed, const FJetFlightParams& P);
}
