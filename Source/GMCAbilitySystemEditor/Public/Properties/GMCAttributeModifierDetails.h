#pragma once

#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
struct FGMCAttributeModifier;

// Details row for FGMCAttributeModifier: op-first summary header + inline warning box when the
// row is configured to read its own target attribute (stale read + feedback loop on re-application).
class FGMCAttributeModifierDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:

	// Null on multi-select or unresolved data.
	static const FGMCAttributeModifier* ResolveModifier(const TSharedRef<IPropertyHandle>& StructPropertyHandle);

	// True when a value source or an op operand reads the modifier's own target attribute.
	static bool IsSelfReadingConfig(const FGMCAttributeModifier& Modifier);
};
