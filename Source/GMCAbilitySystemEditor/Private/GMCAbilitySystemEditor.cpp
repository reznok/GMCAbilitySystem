#include "GMCAbilitySystemEditor.h"
#include "Properties/GameplayElementMappingDetails.h"
#include "Properties/GMCAttributeModifierDetails.h"

#define LOCTEXT_NAMESPACE "FGMCAbilitySystemEditorModule"

void FGMCAbilitySystemEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout( "GMCGameplayElementTagPropertyMapping", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FGMCGameplayElementTagPropertyMappingPropertyDetails::MakeInstance ) );
	PropertyModule.RegisterCustomPropertyTypeLayout( "GMCAttributeModifier", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FGMCAttributeModifierDetails::MakeInstance ) );

}

void FGMCAbilitySystemEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GMCGameplayElementTagPropertyMapping");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GMCAttributeModifier");
	}
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FGMCAbilitySystemEditorModule, GMCAbilitySystemEditor)