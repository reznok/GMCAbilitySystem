#pragma once
#include "GameplayTagContainer.h"
#include "GMCAttributes.generated.h"

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAttribute
{
	GENERATED_BODY()
	
	FAttribute()
	{
		Value = 0;
	}

	FAttribute(float Value)
	{
		this->Value = Value;
	}
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	mutable float Value;

	// Attribute.* 
	UPROPERTY(EditDefaultsOnly, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag Tag;

	// Whether this should be bound over GMC or not.
	// NOTE: If you don't bind it, you can't use it for any kind of prediction.
	UPROPERTY(EditDefaultsOnly)
	bool bIsGMCBound = false;

	FString ToString() const{
		return FString::Printf(TEXT("%s : %f (Bound: %d)"), *Tag.ToString(), Value, bIsGMCBound);
	}
};

USTRUCT()
struct GMCABILITYSYSTEM_API FGMCAttributeSet{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAttribute> Attributes;

	void AddAttribute(FAttribute NewAttribute) {Attributes.Add(NewAttribute);}

	TArray<FAttribute> GetAttributes() const{return Attributes;}
};

