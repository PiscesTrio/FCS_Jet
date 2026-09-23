// FCS_Jet - the flight model's arithmetic, with no engine state behind it

#include "Flight/FlightControlMath.h"

namespace FlightMath
{

float GravityScaleAtZ(float WorldZ, const FJetFlightParams& P)
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

float UpdateThrottle(float Throttle, float ThrottleInput, float DeltaSeconds,
	const FJetFlightParams& P)
{
	if (FMath::IsNearlyZero(ThrottleInput))
	{
		return Throttle;
	}
	return FMath::Clamp(Throttle + ThrottleInput * P.ThrottleChangeRate * DeltaSeconds, 0.f, 1.f);
}

float TargetSpeed(float Throttle, bool bAfterburner, const FJetFlightParams& P)
{
	const float Boost = bAfterburner ? P.AfterburnerMultiplier : 1.f;
	return FMath::Lerp(P.IdleSpeed, P.CruiseSpeed, Throttle) * Boost;
}

float UpdateSpeed(float CurrentSpeed, float Throttle, bool bAfterburner, float DeltaSeconds,
	const FJetFlightParams& P)
{
	const float Responsiveness = bAfterburner ? P.AfterburnerInterpSpeed : P.SpeedInterpSpeed;
	return FMath::FInterpTo(CurrentSpeed, TargetSpeed(Throttle, bAfterburner, P),
		DeltaSeconds, Responsiveness);
}

float UpdateFallSpeed(float FallSpeed, float GravityScale, float DeltaSeconds,
	const FJetFlightParams& P)
{
	if (GravityScale > 0.f)
	{
		return FMath::Min(FallSpeed + P.GravityAcceleration * GravityScale * DeltaSeconds,
			P.MaxFallSpeed);
	}
	if (P.ZeroGFallDamping > 0.f)
	{
		// Clear of the band, a descent carried up from below bleeds away instead of being
		// kept forever. See ZeroGFallDamping for why conserving it is the wrong call here.
		//
		// Damping is gated on the band being fully behind the ship rather than scaled
		// across it. Blending the two against each other inside the band would give
		// partial gravity its own much lower terminal velocity, which is not what a
		// thinning gravity well should feel like.
		return FMath::FInterpTo(FallSpeed, 0.f, DeltaSeconds, P.ZeroGFallDamping);
	}
	return FallSpeed;
}

FVector TargetRates(float PitchInput, float YawInput, float RollInput, bool bAutoLevel,
	const FRotator& Attitude, const FJetFlightParams& P)
{
	FVector Rates(PitchInput * P.PitchRate, YawInput * P.YawRate, RollInput * P.RollRate);

	if (bAutoLevel)
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
		const float PitchError = FRotator::NormalizeAxis(-Attitude.Pitch);
		const float RollError = FRotator::NormalizeAxis(-Attitude.Roll);

		Rates.X = FMath::Clamp(PitchError * P.AutoLevelGain, -P.PitchRate, P.PitchRate);
		Rates.Z = FMath::Clamp(RollError * P.AutoLevelGain, -P.RollRate, P.RollRate);
	}

	return Rates;
}

FVector Velocity(const FVector& Forward, float Speed, float FallSpeed)
{
	// Thrust always points along the nose, with no sideslip and no carried momentum, and
	// gravity adds a separate vertical term on top. Still no lift or drag anywhere.
	return Forward * Speed + FVector(0.f, 0.f, -FallSpeed);
}

float ImpactNormalSpeed(const FVector& InVelocity, const FVector& Normal)
{
	return FMath::Abs(FVector::DotProduct(InVelocity, Normal));
}

bool IsStickLooking(const FVector2D& StickInput, const FJetFlightParams& P)
{
	return StickInput.SizeSquared() > FMath::Square(P.StickLookEngageThreshold);
}

FVector2D UpdateFreeLook(const FVector2D& Offset, const FVector2D& MouseDelta,
	const FVector2D& StickInput, bool bLooking, float DeltaSeconds, const FJetFlightParams& P)
{
	if (!bLooking)
	{
		return FMath::Vector2DInterpTo(Offset, FVector2D::ZeroVector, DeltaSeconds,
			P.FreeLookReturnSpeed);
	}

	// Raw mouse Y is negative when pushed forward while stick Y is positive, so both are
	// normalised to "positive means look up" before the invert preference applies.
	const float LookRight = MouseDelta.X * P.FreeLookSensitivity
		+ StickInput.X * P.FreeLookStickRate * DeltaSeconds;
	const float LookUp = -MouseDelta.Y * P.FreeLookSensitivity
		+ StickInput.Y * P.FreeLookStickRate * DeltaSeconds;

	const float PitchSign = P.bInvertFreeLookPitch ? -1.f : 1.f;

	return FVector2D(
		FMath::Clamp(Offset.X + LookRight, -P.FreeLookYawLimit, P.FreeLookYawLimit),
		FMath::Clamp(Offset.Y + PitchSign * LookUp, -P.FreeLookPitchLimit, P.FreeLookPitchLimit));
}

float BlastAlpha(float CrashElapsed, const FJetFlightParams& P)
{
	return FMath::Clamp(CrashElapsed / P.BlastDuration, 0.f, 1.f);
}

float BlastRadius(float Alpha, const FJetFlightParams& P)
{
	// Ease out so the fireball punches open and then slows, rather than growing linearly.
	return P.BlastMaxRadius * (1.f - FMath::Square(1.f - Alpha));
}

float BlastStrength(float Alpha, const FJetFlightParams& P)
{
	return P.BlastPeakStrength * FMath::Square(1.f - Alpha);
}

float BlastLightIntensity(float Alpha, const FJetFlightParams& P)
{
	return P.BlastLightPeakIntensity * FMath::Square(1.f - Alpha) * (1.f - Alpha);
}

FRotator ShakeOffset(float CrashElapsed, const FJetFlightParams& P)
{
	if (CrashElapsed >= P.BlastDuration || P.ShakeAmplitude <= 0.f)
	{
		return FRotator::ZeroRotator;
	}

	// Three different frequencies so the axes do not move in lockstep, which would read as
	// one clean wobble rather than a jolt.
	const float Decay = FMath::Square(1.f - BlastAlpha(CrashElapsed, P));
	const float Amount = P.ShakeAmplitude * Decay;

	return FRotator(
		Amount * FMath::Sin(CrashElapsed * P.ShakeFrequency),
		Amount * FMath::Sin(CrashElapsed * P.ShakeFrequency * 1.37f + 1.1f),
		Amount * 0.5f * FMath::Sin(CrashElapsed * P.ShakeFrequency * 0.83f + 2.2f));
}

}  // namespace FlightMath
