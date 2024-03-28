#include "GMCAbilitySystemEditor.h"
#include "Properties/GameplayElementMappingDetails.h"

#define LOCTEXT_NAMESPACE "FGMCAbilitySystemEditorModule"

void FGMCAbilitySystemEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout( "GMCGameplayElementTagPropertyMapping", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FGMCGameplayElementTagPropertyMappingPropertyDetails::MakeInstance ) );
    
}

void FGMCAbilitySystemEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GMCGameplayElementTagPropertyMapping");
	}
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FGMCAbilitySystemEditorModule, GMCAbilitySystemEditor)