// FCS_Jet - flight readout data and how it is drawn

#pragma once

#include "CoreMinimal.h"
#include "Flight/JetFlightParams.h"
#include "JetTelemetry.generated.h"

/**
 * One frame of the ship's state, gathered for display.
 *
 * Separated from the drawing so the numbers can be read by something other than the debug
 * overlay later, and so gathering them stays independent of how they are presented.
 */
USTRUCT(BlueprintType)
struct FCS_JET_API FJetTelemetry
{
	GENERATED_BODY()

	/** Speed along the nose, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float SpeedCms = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float Throttle = 0.f;

	/** Commanded body rates, deg/s: X pitch, Y yaw, Z roll. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FVector Rates = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float AltitudeCm = 0.f;

	/** 1 deep in the gravity well, 0 out in space. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float GravityScale = 0.f;

	/** Positive means falling, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float FallSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FRotator Attitude = FRotator::ZeroRotator;

	/** Free look offset in degrees: X yaw, Y pitch. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FVector2D FreeLookOffset = FVector2D::ZeroVector;

	/** Closing speed on whatever is ahead, cm/s, or 0 when nothing is. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float PredictedImpactSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float PredictedImpactTime = 0.f;

	/** Normal speed of the impact that destroyed the ship, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float LastImpactSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float CrashElapsed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bAfterburner = false;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bFreeLook = false;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bAutoLevel = false;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bCrashed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bImpactPredicted = false;

	/** Whether ground impacts are live. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bCrashArmed = true;
};

namespace JetReadout
{
	/**
	 * Draw the readout with the engine's on screen debug messages.
	 *
	 * Deliberately not a HUD or a UMG widget: this is a development instrument, and the
	 * debug message channel costs nothing to maintain and disappears in Shipping builds.
	 */
	FCS_JET_API void Draw(const FJetTelemetry& T, const FJetFlightParams& P);
}
