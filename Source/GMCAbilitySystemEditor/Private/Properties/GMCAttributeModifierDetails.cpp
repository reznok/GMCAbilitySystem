#include "Properties/GMCAttributeModifierDetails.h"

#include "Attributes/GMCAttributeModifier.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "SWarningOrErrorBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GMCAttributeModifierDetails"

TSharedRef<IPropertyTypeCustomization> FGMCAttributeModifierDetails::MakeInstance()
{
	return MakeShareable(new FGMCAttributeModifierDetails());
}

const FGMCAttributeModifier* FGMCAttributeModifierDetails::ResolveModifier(const TSharedRef<IPropertyHandle>& StructPropertyHandle)
{
	void* Data = nullptr;
	if (StructPropertyHandle->GetValueData(Data) != FPropertyAccess::Success || !Data)
	{
		return nullptr;
	}
	return static_cast<const FGMCAttributeModifier*>(Data);
}

bool FGMCAttributeModifierDetails::IsSelfReadingConfig(const FGMCAttributeModifier& Modifier)
{
	if (!Modifier.AttributeTag.IsValid())
	{
		return false;
	}

	const bool bValueSourceSelfRead = Modifier.ValueType == EGMCAttributeModifierType::AMT_Attribute
		&& Modifier.ValueAsAttribute == Modifier.AttributeTag;

	const bool bPercentSelfRead = Modifier.Op == EModifierType::AddPercentageAttribute
		&& Modifier.ValueAsAttribute == Modifier.AttributeTag;

	const bool bSumSelfRead = Modifier.Op == EModifierType::AddPercentageAttributeSum
		&& Modifier.Attributes.HasTagExact(Modifier.AttributeTag);

	const bool bBoundsSelfRead = (Modifier.Op == EModifierType::AddScaledBetween || Modifier.Op == EModifierType::AddClampedBetween)
		&& ((Modifier.XAsAttribute && Modifier.XAttribute == Modifier.AttributeTag)
			|| (Modifier.YAsAttribute && Modifier.YAttribute == Modifier.AttributeTag));

	return bValueSourceSelfRead || bPercentSelfRead || bSumSelfRead || bBoundsSelfRead;
}

void FGMCAttributeModifierDetails::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const FSlateFontInfo SummaryFont = CustomizationUtils.GetRegularFont();

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(300.f)
	[
		SNew(STextBlock)
		.Font(SummaryFont)
		.Text_Lambda([StructPropertyHandle]()
		{
			const FGMCAttributeModifier* Modifier = ResolveModifier(StructPropertyHandle);
			if (!Modifier)
			{
				return FText::GetEmpty();
			}
			return FText::Format(LOCTEXT("ModifierSummary", "{0}  →  {1}"),
				StaticEnum<EModifierType>()->GetDisplayNameTextByValue(static_cast<int64>(Modifier->Op)),
				FText::FromName(Modifier->AttributeTag.GetTagName()));
		})
	];
}

void FGMCAttributeModifierDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ChildBuilder.AddCustomRow(LOCTEXT("SelfReadWarningFilter", "Warning"))
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([StructPropertyHandle]()
	{
		const FGMCAttributeModifier* Modifier = ResolveModifier(StructPropertyHandle);
		return (Modifier && IsSelfReadingConfig(*Modifier)) ? EVisibility::Visible : EVisibility::Collapsed;
	})))
	.WholeRowContent()
	[
		SNew(SWarningOrErrorBox)
		.MessageStyle(EMessageStyle::Warning)
		.Message(LOCTEXT("SelfReadWarning",
			"This modifier reads its own target attribute at application time. The read is one tick stale and feeds back on each re-application (the value inflates). Use '% [Add Percentage Of Base]' instead."))
	];

	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		ChildBuilder.AddProperty(StructPropertyHandle->GetChildHandle(Index).ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE
