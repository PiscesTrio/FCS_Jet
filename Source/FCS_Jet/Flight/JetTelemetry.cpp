// FCS_Jet - flight readout data and how it is drawn

#include "Flight/JetTelemetry.h"

#include "Engine/Engine.h"

namespace
{
	/** cm/s to km/h. */
	constexpr float CmsToKmh = 0.036f;

	/**
	 * Debug message keys. Fixed so each line overwrites itself every frame instead of
	 * stacking, and distinct so a line can be removed on its own when it does not apply.
	 */
	constexpr int32 KeySpeed = 101;
	constexpr int32 KeyThrottle = 102;
	constexpr int32 KeyRates = 103;
	constexpr int32 KeyFreeLook = 104;
	constexpr int32 KeyCrash = 105;
	constexpr int32 KeyAltitude = 106;
	constexpr int32 KeyImpact = 107;
	constexpr int32 KeyAutoLevel = 108;
}

namespace JetReadout
{

void Draw(const FJetTelemetry& T, const FJetFlightParams& P)
{
	if (!P.bShowFlightReadout || !GEngine)
	{
		return;
	}

	// The on screen debug font has no CJK glyphs, so this readout stays ASCII only.
	const int32 Filled = FMath::RoundToInt(T.Throttle * 10.f);
	FString Bar;
	for (int32 i = 0; i < 10; ++i)
	{
		Bar += (i < Filled) ? TEXT("|") : TEXT(".");
	}

	GEngine->AddOnScreenDebugMessage(KeySpeed, 0.f, FColor::Cyan,
		FString::Printf(TEXT("SPD  %6.0f km/h   (%5.0f m/s)"),
			T.SpeedCms * CmsToKmh, T.SpeedCms * 0.01f));

	GEngine->AddOnScreenDebugMessage(KeyThrottle, 0.f,
		T.bAfterburner ? FColor::Orange : FColor::Green,
		FString::Printf(TEXT("THR  [%s] %3.0f%%%s"), *Bar, T.Throttle * 100.f,
			T.bAfterburner ? TEXT("   AFTERBURNER") : TEXT("")));

	GEngine->AddOnScreenDebugMessage(KeyRates, 0.f, FColor::White,
		FString::Printf(TEXT("RATE P %6.1f   Y %6.1f   R %6.1f  deg/s"),
			T.Rates.X, T.Rates.Y, T.Rates.Z));

	GEngine->AddOnScreenDebugMessage(KeyAltitude, 0.f,
		T.GravityScale > 0.f ? FColor::Yellow : FColor::Cyan,
		FString::Printf(TEXT("ALT %7.0f m   GRAV %3.0f%%%s   VS %+6.0f m/s"),
			T.AltitudeCm * 0.01f,
			T.GravityScale * 100.f,
			T.GravityScale > 0.f ? TEXT("") : TEXT("  ZERO-G"),
			-T.FallSpeed * 0.01f));

	if (T.bFreeLook)
	{
		GEngine->AddOnScreenDebugMessage(KeyFreeLook, 0.f, FColor::Yellow,
			FString::Printf(TEXT("FREE LOOK  yaw %5.0f  pitch %5.0f"),
				T.FreeLookOffset.X, T.FreeLookOffset.Y));
	}
	else
	{
		GEngine->RemoveOnScreenDebugMessage(KeyFreeLook);
	}

	if (T.bAutoLevel)
	{
		GEngine->AddOnScreenDebugMessage(KeyAutoLevel, 0.f, FColor::Orange,
			FString::Printf(TEXT("AUTO-LEVEL   pitch %+5.1f   roll %+6.1f"),
				FRotator::NormalizeAxis(T.Attitude.Pitch),
				FRotator::NormalizeAxis(T.Attitude.Roll)));
	}
	else
	{
		GEngine->RemoveOnScreenDebugMessage(KeyAutoLevel);
	}

	if (T.bCrashed)
	{
		GEngine->AddOnScreenDebugMessage(KeyCrash, 0.f, FColor::Red,
			FString::Printf(TEXT("GROUND IMPACT  %.0f km/h  -  respawn in %.1f s"),
				T.LastImpactSpeed * CmsToKmh,
				FMath::Max(0.f, P.RespawnDelay - T.CrashElapsed)));
		GEngine->RemoveOnScreenDebugMessage(KeyImpact);
		return;
	}

	GEngine->RemoveOnScreenDebugMessage(KeyCrash);

	if (!T.bCrashArmed)
	{
		// Said plainly rather than left blank. A quiet impact line would otherwise look
		// like a clear path right up until the ship flies through the deck.
		GEngine->AddOnScreenDebugMessage(KeyImpact, 0.f, FColor::Magenta,
			TEXT("IMPACT   DISARMED  -  ground impacts off"));
		return;
	}

	// The number that decides the outcome, live, next to the threshold it is judged
	// against. Reporting it only after the fact would be useless for staying alive.
	if (T.bImpactPredicted)
	{
		const bool bFatal = T.PredictedImpactSpeed >= P.MinCrashImpactSpeed;
		GEngine->AddOnScreenDebugMessage(KeyImpact, 0.f,
			bFatal ? FColor::Red : FColor::Green,
			FString::Printf(TEXT("IMPACT %5.0f km/h in %4.1f s   limit %.0f   %s"),
				T.PredictedImpactSpeed * CmsToKmh,
				T.PredictedImpactTime,
				P.MinCrashImpactSpeed * CmsToKmh,
				bFatal ? TEXT("FATAL") : TEXT("survivable")));
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(KeyImpact, 0.f, FColor::Silver,
			FString::Printf(TEXT("IMPACT     -              limit %.0f   clear"),
				P.MinCrashImpactSpeed * CmsToKmh));
	}
}

}  // namespace JetReadout
