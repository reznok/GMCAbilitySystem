#include "Utility/GMASUtilities.h"

#if WITH_EDITOR
void UGMASUtilities::SetPropertyFlagsSafe(UClass* StaticClass, FName PropertyName, EPropertyFlags NewFlags)
{
	if (!StaticClass) return;

	if (FProperty *Property = StaticClass->FindPropertyByName(PropertyName))
	{
		Property->SetPropertyFlags(NewFlags);
	}	
}

void UGMASUtilities::ClearPropertyFlagsSafe(UClass* StaticClass, FName PropertyName, EPropertyFlags ClearFlags)
{
	if (!StaticClass) return;

	if (FProperty *Property = StaticClass->FindPropertyByName(PropertyName))
	{
		Property->ClearPropertyFlags(ClearFlags);
	}	
}
#endif
