#include "..\Public\GMCAttributes.h"

UGMCAttributeSet::UGMCAttributeSet()
{
}

FAttribute UGMCAttributeSet::GetAttributeValueByName(FName PropName)
{
	return *GetAttributeByName(PropName);
}

FAttribute* UGMCAttributeSet::GetAttributeByName(FName PropName)
{	
	FProperty* Property = GetClass()->FindPropertyByName(PropName);
	if (!Property) return nullptr;

	FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty) return nullptr;

	FAttribute* Attribute = StructProperty->ContainerPtrToValuePtr<FAttribute>(this);
	return Attribute;
}

void UGMCAttributeSet::SetAttributeByName(FName PropName, float NewValue)
{
	FProperty* Property = GetClass()->FindPropertyByName(PropName);
	if (!Property) return;

	FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty) return;

	FAttribute* Attribute = StructProperty->ContainerPtrToValuePtr<FAttribute>(this);
	Attribute->Value = NewValue;
}

TArray<FAttribute*> UGMCAttributeSet::GetAllAttributes()
{	
	TArray<FAttribute*> Attributes;
	for( TFieldIterator<FProperty> Attribute(GetClass()); Attribute; ++Attribute ) {
		Attributes.Push(Attribute->ContainerPtrToValuePtr<FAttribute>(this));
	}		
	return Attributes;
}
