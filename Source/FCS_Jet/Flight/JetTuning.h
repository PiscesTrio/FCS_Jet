// FCS_Jet - designer facing tuning asset

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Flight/JetFlightParams.h"
#include "JetTuning.generated.h"

/**
 * Tuning asset for the ship. Values can be edited while flying in PIE and they stick,
 * which is the whole point of pulling them off the pawn: a native class has no defaults
 * panel, so anything living on it could only be changed by recompiling.
 *
 * The pawn falls back to its code defaults when no asset is assigned, so losing or
 * detaching this asset degrades the ship to its authored values rather than breaking it.
 */
UCLASS(BlueprintType)
class FCS_JET_API UJetTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flight", meta = (ShowOnlyInnerProperties))
	FJetFlightParams Params;
};
