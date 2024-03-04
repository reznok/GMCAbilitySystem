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
	UPROPERTY(EditDefaultsOnly, Category="Attribute")
	FGameplayTag Tag;

	// Whether this should be bound over GMC or not.
	// NOTE: If you don't bind it, you can't use it for any kind of prediction.
	UPROPERTY(EditDefaultsOnly)
	bool bIsGMCBound = true;
};

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAttributeSet : public UObject
{
	GENERATED_BODY()
public:
	UGMCAttributeSet();

	// Get a specific attribute by name. Used mainly for Effects.
	UFUNCTION(BlueprintCallable)
	FAttribute GetAttributeValueByName(FName PropName);

	// Get a specific attribute by tag
	FAttribute* GetAttributeByTag(FGameplayTag Tag);
	
	// Get a specific attribute by name. Used mainly for Effects.
	FAttribute* GetAttributeByName(FName PropName);

	// Set a specific attribute by name. Used mainly for Effects.
	UFUNCTION(BlueprintCallable)
	void SetAttributeByName(FName PropName, float NewValue);
	
	// Used to get the variables to bind for Replication automatically
	TArray<FAttribute*> GetAllAttributes();

};

