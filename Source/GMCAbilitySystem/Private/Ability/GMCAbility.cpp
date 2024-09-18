#include "Ability/GMCAbility.h"
#include "GMCAbilitySystem.h"
#include "GMCPawn.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "Components/GMCAbilityComponent.h"

UWorld* UGMCAbility::GetWorld() const
{
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		// If we're a CDO, we *must* return nullptr to avoid causing issues with
		// UObject::ImplementsGetWorld(), which just blithely and blindly calls GetWorld().
		return nullptr;
	}
	
#if WITH_EDITOR
	if (GIsEditor)
	{
		return GWorld;
	}
#endif // WITH_EDITOR

	// Sanity check rather than blindly accessing the world context array.
	auto Contexts = GEngine->GetWorldContexts();
	if (Contexts.Num() == 0)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("%s: instanciated class with no valid world!"), *GetClass()->GetName())
		return nullptr;
	}
	
	return Contexts[0].World();
}

void UGMCAbility::Tick(float DeltaTime)
{
	if (!OwnerAbilityComponent->HasAuthority())
	{
		if (!bServerConfirmed && ClientStartTime + ServerConfirmTimeout < OwnerAbilityComponent->ActionTimer)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Ability Not Confirmed By Server: %d, Removing..."), AbilityID);
			EndAbility();
			return;
		}
	}

	if (bEndPending) {
		EndAbility();
		return;
	}
	
	TickTasks(DeltaTime);
	TickEvent(DeltaTime);
}

void UGMCAbility::AncillaryTick(float DeltaTime){
	AncillaryTickTasks(DeltaTime);
	AncillaryTickEvent(DeltaTime);
}

void UGMCAbility::TickTasks(float DeltaTime)
{
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		if (Task.Value == nullptr) {continue;}
		Task.Value->Tick(DeltaTime);
	}
}

void UGMCAbility::AncillaryTickTasks(float DeltaTime){
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		if (Task.Value == nullptr) {continue;}
		Task.Value->AncillaryTick(DeltaTime);
	}
}

void UGMCAbility::Execute(UGMC_AbilitySystemComponent* InAbilityComponent, int InAbilityID, const UInputAction* InputAction)
{
	this->AbilityInputAction = InputAction;
	this->AbilityID = InAbilityID;
	this->OwnerAbilityComponent = InAbilityComponent;
	this->ClientStartTime = InAbilityComponent->ActionTimer;
	PreBeginAbility();
}

bool UGMCAbility::CanAffordAbilityCost() const
{
	if (AbilityCost == nullptr || OwnerAbilityComponent == nullptr) return true;
	
	UGMCAbilityEffect* AbilityEffect = AbilityCost->GetDefaultObject<UGMCAbilityEffect>();
	for (FGMCAttributeModifier AttributeModifier : AbilityEffect->EffectData.Modifiers)
	{
		for (const FAttribute* Attribute : OwnerAbilityComponent->GetAllAttributes())
		{
			if (Attribute->Tag.MatchesTagExact(AttributeModifier.AttributeTag))
			{
				if (Attribute->Value + AttributeModifier.Value < 0) return false;
			}
		}
	}

	return true;
}

void UGMCAbility::CommitAbilityCostAndCooldown()
{
	CommitAbilityCost();
	CommitAbilityCooldown();
}

void UGMCAbility::CommitAbilityCooldown()
{
	if (CooldownTime <= 0.f || OwnerAbilityComponent == nullptr) return;
	OwnerAbilityComponent->SetCooldownForAbility(AbilityTag, CooldownTime);
}

void UGMCAbility::CommitAbilityCost()
{
	if (AbilityCost == nullptr || OwnerAbilityComponent == nullptr) return;
	
	const UGMCAbilityEffect* EffectCDO = DuplicateObject(AbilityCost->GetDefaultObject<UGMCAbilityEffect>(), this);
	FGMCAbilityEffectData EffectData = EffectCDO->EffectData;
	EffectData.OwnerAbilityComponent = OwnerAbilityComponent;
	AbilityCostInstance = OwnerAbilityComponent->ApplyAbilityEffect(DuplicateObject(EffectCDO, this), EffectData);
}

void UGMCAbility::RemoveAbilityCost(){
	if(AbilityCostInstance){
		OwnerAbilityComponent->RemoveActiveAbilityEffect(AbilityCostInstance);
	}
}


void UGMCAbility::ModifyBlockOtherAbility(FGameplayTagContainer TagToAdd, FGameplayTagContainer TagToRemove) {
	for (auto Tag : TagToAdd) {
		BlockOtherAbility.AddTag(Tag);
	}

	for (auto Tag : TagToRemove) {
		BlockOtherAbility.RemoveTag(Tag);
	}
}


void UGMCAbility::ResetBlockOtherAbility() {
	BlockOtherAbility = GetClass()->GetDefaultObject<UGMCAbility>()->BlockOtherAbility;
}


void UGMCAbility::HandleTaskData(int TaskID, FInstancedStruct TaskData)
{
	const FGMCAbilityTaskData TaskDataFromInstance = TaskData.Get<FGMCAbilityTaskData>();
	if (RunningTasks.Contains(TaskID) && RunningTasks[TaskID] != nullptr)
	{
		if (TaskDataFromInstance.TaskType == EGMCAbilityTaskDataType::Progress)
		{
			RunningTasks[TaskID]->ProgressTask(TaskData);
		}
	}
}

void UGMCAbility::HandleTaskHeartbeat(int TaskID)
{
	if (RunningTasks.Contains(TaskID) && RunningTasks[TaskID] != nullptr) // Do we ever remove orphans tasks ?
	{
		RunningTasks[TaskID]->Heartbeat();
	}
}

void UGMCAbility::ServerConfirm()
{
	bServerConfirmed = true;
}


void UGMCAbility::SetPendingEnd() {
	bEndPending = true;
}


UGameplayTasksComponent* UGMCAbility::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent; }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskOwner(const UGameplayTask* Task) const
{
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent->GetOwner(); }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	// Wtf is avatar?
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent->GetOwner(); }
	return nullptr;
}

void UGMCAbility::OnGameplayTaskInitialized(UGameplayTask& Task)
{
	UGMCAbilityTaskBase* AbilityTask = Cast<UGMCAbilityTaskBase>(&Task);
	if (!AbilityTask)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbility::OnGameplayTaskInitialized called with non-UGMCAbilityTaskBase task"));
		return;
	}
	AbilityTask->SetAbilitySystemComponent(OwnerAbilityComponent);
	AbilityTask->Ability = this;
	
}

void UGMCAbility::OnGameplayTaskActivated(UGameplayTask& Task)
{
	ActiveTasks.Add(&Task);
}

void UGMCAbility::OnGameplayTaskDeactivated(UGameplayTask& Task)
{
	ActiveTasks.Remove(&Task);
}


void UGMCAbility::FinishEndAbility() {
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		if (Task.Value == nullptr) continue;
		Task.Value->EndTaskGMAS();
	}
	
	AbilityState = EAbilityState::Ended;
}


bool UGMCAbility::IsOnCooldown() const
{
	return OwnerAbilityComponent->GetCooldownForAbility(AbilityTag) > 0;
}


bool UGMCAbility::PreExecuteCheckEvent_Implementation() {
	return true;
}


bool UGMCAbility::PreBeginAbility() {
	if (IsOnCooldown())
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Cooldown"), *AbilityTag.ToString());
		CancelAbility();
		return false;
	}

	// PreCheck
	if (!PreExecuteCheckEvent())
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Failing PreExecution check"), *AbilityTag.ToString());
		CancelAbility();
		return false;
	}

	BeginAbility();

	return true;
}


void UGMCAbility::BeginAbility()
{
	
	if (OwnerAbilityComponent->IsAbilityTagBlocked(AbilityTag)) {
		CancelAbility();
		return;
	}

	
	if (bApplyCooldownAtAbilityBegin)
	{
		CommitAbilityCooldown();
	}
	
	// Initialize Ability
	AbilityState = EAbilityState::Initialized;

	// Cancel Abilities in CancelAbilitiesWithTag container
	for (const auto& AbilityToCancelTag : CancelAbilitiesWithTag) {
		if (AbilityTag == AbilityToCancelTag) {
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability (tag) %s is trying to cancel itself, if you attempt to reset the ability, please use //TODO instead"), *AbilityTag.ToString());
			continue;
		}
		
		if (OwnerAbilityComponent->EndAbilitiesByTag(AbilityToCancelTag)) {
			UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability (tag) %s has been cancelled by (tag) %s"), *AbilityTag.ToString(), *AbilityToCancelTag.ToString());	
		}
	}

	// Execute BP Event
	BeginAbilityEvent();
}

void UGMCAbility::EndAbility()
{
	if (AbilityState != EAbilityState::Ended) {
		FinishEndAbility();
		EndAbilityEvent();
	}
}


void UGMCAbility::CancelAbility() {
	if (AbilityState != EAbilityState::Ended) {
		FinishEndAbility();
	}
}


AActor* UGMCAbility::GetOwnerActor() const
{
	return OwnerAbilityComponent->GetOwner();
}

AGMC_Pawn* UGMCAbility::GetOwnerPawn() const{
	if (AGMC_Pawn* OwningPawn = Cast<AGMC_Pawn>(GetOwnerActor())){
		return OwningPawn;
	}
	return nullptr;
}

AGMC_PlayerController* UGMCAbility::GetOwningPlayerController() const{
	if (const AGMC_Pawn* OwningPawn = GetOwnerPawn()){
		if(AGMC_PlayerController* OwningPC = Cast<AGMC_PlayerController>(OwningPawn->GetController())){
			return OwningPC;
		}
	}
	return nullptr;
}

float UGMCAbility::GetOwnerAttributeValueByTag(FGameplayTag AttributeTag) const
{
	return OwnerAbilityComponent->GetAttributeValueByTag(AttributeTag);
}


void UGMCAbility::SetOwnerJustTeleported(bool bValue)
{
	OwnerAbilityComponent->bJustTeleported = bValue;
}
