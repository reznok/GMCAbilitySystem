// Copyright Iliad. Part of the GMAS plugin's Iliad-local fork.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GMASNiagaraParams.generated.h"

UENUM(BlueprintType)
enum class EGMASNiagaraUserParamType : uint8
{
	Float  UMETA(DisplayName = "Float"),
	Int    UMETA(DisplayName = "Int"),
	Bool   UMETA(DisplayName = "Bool"),
	Vector UMETA(DisplayName = "Vector"),
	Color  UMETA(DisplayName = "Color"),
};

/**
 * One user-parameter override to apply to a Niagara System after spawn.
 * Carried through GMAS multicast spawn helpers so all receivers (server,
 * owning client, sim proxies) apply identical user params to the local
 * NiagaraComponent they instantiate. Without this, only the caller's
 * NiagaraComponent gets the param set; remotes see defaults.
 *
 * Author specs in BP as an array, drive size/color/etc per spawn.
 * Param Name should be the Niagara user var leaf name (e.g. "SizeScale"
 * — NOT "User.SizeScale"; the spawn helper prefixes "User." for you).
 */
USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASNiagaraUserParam
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMAS|FX")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMAS|FX")
	EGMASNiagaraUserParamType Type = EGMASNiagaraUserParamType::Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMAS|FX")
	float FloatValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMAS|FX")
	int32 IntValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMAS|FX")
	bool BoolValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMAS|FX")
	FVector VectorValue = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GMAS|FX")
	FLinearColor ColorValue = FLinearColor::White;
};

/** One-node makers for FGMASNiagaraUserParam — lighter than a MakeStruct
 *  node in graphs and reachable from scripted graph authoring. */
UCLASS()
class GMCABILITYSYSTEM_API UGMASNiagaraParamLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "GMAS|FX")
	static FGMASNiagaraUserParam MakeNiagaraFloatParam(FName Name, float Value)
	{
		FGMASNiagaraUserParam P;
		P.Name = Name;
		P.Type = EGMASNiagaraUserParamType::Float;
		P.FloatValue = Value;
		return P;
	}

	UFUNCTION(BlueprintPure, Category = "GMAS|FX")
	static FGMASNiagaraUserParam MakeNiagaraVectorParam(FName Name, FVector Value)
	{
		FGMASNiagaraUserParam P;
		P.Name = Name;
		P.Type = EGMASNiagaraUserParamType::Vector;
		P.VectorValue = Value;
		return P;
	}
};
