#include "Properties/GameplayElementMappingDetails.h"
#include "Utility/GameplayElementMapping.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Engine/Blueprint.h"
#include "Widgets/Input/SComboBox.h"

TSharedRef<IPropertyTypeCustomization> FGMCGameplayElementTagPropertyMappingPropertyDetails::MakeInstance()
{
	return MakeShareable(new FGMCGameplayElementTagPropertyMappingPropertyDetails());
}

void FGMCGameplayElementTagPropertyMappingPropertyDetails::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	NamePropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGMCGameplayElementTagPropertyMapping, PropertyName));
	GuidPropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGMCGameplayElementTagPropertyMapping, PropertyGuid));

	FString SelectedPropertyName;
	NamePropertyHandle->GetValue(SelectedPropertyName);

	TArray<void*> RawData;
	GuidPropertyHandle->AccessRawData(RawData);

	FGuid SelectedPropertyGuid;
	if (RawData.Num() > 0)
	{
		SelectedPropertyGuid = *static_cast<FGuid*>(RawData[0]);
	}

	FProperty* FoundProperty = nullptr;

	TArray<UObject*> OuterObjects;
	NamePropertyHandle->GetOuterObjects(OuterObjects);

	for (UObject* ParentObject : OuterObjects)
	{
		if (!ParentObject)
		{
			continue;
		}

		for (TFieldIterator<FProperty> PropertyIt(ParentObject->GetClass()); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (!Property)
			{
				continue;
			}

			// Only support booleans, floats, and integers.
			const bool bIsValidType = Property->IsA(FBoolProperty::StaticClass()) || Property->IsA(FIntProperty::StaticClass()) || Property->IsA(FFloatProperty::StaticClass());
			if (!bIsValidType)
			{
				continue;
			}

			// Only accept properties from a blueprint.
			if (!UBlueprint::GetBlueprintFromClass(Property->GetOwnerClass()))
			{
				continue;
			}

			// Ignore properties that don't have a GUID since we rely on it to handle property name changes.
			FGuid PropertyToTestGuid = GetPropertyGuid(Property);
			if (!PropertyToTestGuid.IsValid())
			{
				continue;
			}

			// Add the property to the combo box.
			PropertyOptions.AddUnique(Property);

			// Find our current selected property in the list.
			if (SelectedPropertyGuid == PropertyToTestGuid)
			{
				FoundProperty = Property;
			}
		}
	}

	// Sort the options list alphabetically.
	PropertyOptions.StableSort([](const TFieldPath<FProperty>& A, const TFieldPath<FProperty>& B) { return (A->GetName() < B->GetName()); });

	if ((FoundProperty == nullptr) || (FoundProperty != SelectedProperty) || (FoundProperty->GetName() != SelectedPropertyName))
	{
		// The selected property needs to be updated.
		OnChangeProperty(FoundProperty, ESelectInfo::Direct);
	}

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		];	
}	

void FGMCGameplayElementTagPropertyMappingPropertyDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> TagsToMapHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGMCGameplayElementTagPropertyMapping, TagsToMap));
	TSharedPtr<IPropertyHandle> PropertyToEditHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGMCGameplayElementTagPropertyMapping, PropertyToMap));

	// Add the FGameplay Tag first.
	ChildBuilder.AddProperty(TagsToMapHandle.ToSharedRef());

	// Add the combo box next.
	IDetailPropertyRow& PropertyRow = ChildBuilder.AddProperty(StructPropertyHandle);
	PropertyRow.CustomWidget()
		.NameContent()
		[
			PropertyToEditHandle->CreatePropertyNameWidget()
		]

		.ValueContent()
		.HAlign(HAlign_Left)
		.MinDesiredWidth(250.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SComboBox< TFieldPath<FProperty> >)
				.OptionsSource(&PropertyOptions)
				.OnGenerateWidget(this, &FGMCGameplayElementTagPropertyMappingPropertyDetails::GeneratePropertyWidget)
				.OnSelectionChanged(this, &FGMCGameplayElementTagPropertyMappingPropertyDetails::OnChangeProperty)
				.ContentPadding(FMargin(2.0f, 2.0f))
				.ToolTipText(this, &FGMCGameplayElementTagPropertyMappingPropertyDetails::GetSelectedValueText)
				.InitiallySelectedItem(SelectedProperty)
				[
					SNew(STextBlock)
					.Text(this, &FGMCGameplayElementTagPropertyMappingPropertyDetails::GetSelectedValueText)
				]
			]
		];	
}

void FGMCGameplayElementTagPropertyMappingPropertyDetails::OnChangeProperty(TFieldPath<FProperty> ItemSelected,
	ESelectInfo::Type SelectInfo)
{
	if (NamePropertyHandle.IsValid() && GuidPropertyHandle.IsValid())
	{
		SelectedProperty = ItemSelected;

		NamePropertyHandle->SetValue(GetPropertyName(ItemSelected));

		TArray<void*> RawData;
		GuidPropertyHandle->AccessRawData(RawData);

		if (RawData.Num() > 0)
		{
			FGuid* RawGuid = static_cast<FGuid*>(RawData[0]);
			*RawGuid = GetPropertyGuid(ItemSelected);
		}
	}
}

FGuid FGMCGameplayElementTagPropertyMappingPropertyDetails::GetPropertyGuid(TFieldPath<FProperty> Property) const
{
	FGuid Guid;

	if (Property != nullptr)
	{
		UBlueprint::GetGuidFromClassByFieldName<FProperty>(Property->GetOwnerClass(), Property->GetFName(), Guid);
	}

	return Guid;
}

FString FGMCGameplayElementTagPropertyMappingPropertyDetails::GetPropertyName(TFieldPath<FProperty> Property) const
{
	return (Property != nullptr ? Property->GetName() : TEXT("None"));
}

TSharedRef<SWidget> FGMCGameplayElementTagPropertyMappingPropertyDetails::GeneratePropertyWidget(
	TFieldPath<FProperty> Property)
{
	return SNew(STextBlock).Text(FText::FromString(GetPropertyName(Property.Get())));
}

FText FGMCGameplayElementTagPropertyMappingPropertyDetails::GetSelectedValueText() const
{
	return FText::FromString(GetPropertyName(SelectedProperty));
}

