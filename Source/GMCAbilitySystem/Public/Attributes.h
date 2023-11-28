#pragma once
#include "UObject/PropertyAccessUtil.h"

#include "Attributes.generated.h"

USTRUCT(BlueprintType)
struct FAttribute
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
};

USTRUCT(BlueprintType)
struct FGMCAttributeSet
{
	GENERATED_BODY()

	FGMCAttributeSet()
	{
		Health = 0;
		MaxHealth = 0;
		Stamina = 0;
		MaxStamina = 0;
	}

	// Attributes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	FAttribute Health;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	float MaxHealth = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	FAttribute Stamina;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	float MaxStamina = 100;
	
	FAttribute* GetAttributeByName(FName PropName)
	{
		FStructProperty* Prop = static_cast<FStructProperty*>(StaticStruct()->FindPropertyByName(PropName));
		if (!Prop) return nullptr;
		
		return Prop->ContainerPtrToValuePtr<FAttribute>(this);
	};
};


