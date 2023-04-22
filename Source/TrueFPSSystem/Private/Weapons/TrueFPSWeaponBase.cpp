// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapons/TrueFPSWeaponBase.h"

#include "AIController.h"
#include "TrueFPSSystem.h"
#include "Character/TrueFPSCharacterInterface.h"
#include "GameFramework/Character.h"
#include "VisualizationMacros.h"
#include "Bots/TrueFPSAIController.h"
#include "Character/TrueFPSPlayerController.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/Attachments/TrueFPSSightsAttachment.h"
#include "Weapons/Attachments/TrueFPSWeaponAttachmentBase.h"
#include "Weapons/Attachments/TrueFPSWeaponAttachmentPoint.h"

ATrueFPSWeaponBase::ATrueFPSWeaponBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Mesh1P = ObjectInitializer.CreateDefaultSubobject<USkeletalMeshComponent>(this, TEXT("WeaponMesh1P"));
	Mesh1P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh1P->bReceivesDecals = false;
	Mesh1P->CastShadow = true;
	Mesh1P->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh1P->SetCollisionResponseToAllChannels(ECR_Ignore);
	RootComponent = Mesh1P;

	Mesh3P = ObjectInitializer.CreateDefaultSubobject<USkeletalMeshComponent>(this, TEXT("WeaponMesh3P"));
	Mesh3P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh3P->bReceivesDecals = false;
	Mesh3P->CastShadow = true;
	Mesh3P->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh3P->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh3P->SetCollisionResponseToChannel(COLLISION_WEAPON, ECR_Block);
	Mesh3P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh3P->SetCollisionResponseToChannel(COLLISION_PROJECTILE, ECR_Block);
	Mesh3P->SetupAttachment(Mesh1P);

	bLoopedFireAnim = false;
	bPlayingFireAnim = false;
	bIsEquipped = false;
	bWantsToFire = false;
	bPendingEquip = false;
	CurrentState = EWeaponState::Idle;
	BurstCounter = 0;
	LastFireTime = 0.0f;
	
	RecoilResetSpeed = 5.f;
	RecoilSpeed = 10.f;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRemoteRoleForBackwardsCompat(ROLE_SimulatedProxy);
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
}

void ATrueFPSWeaponBase::BeginPlay()
{
#if WITH_EDITORONLY_DATA
	if (OriginWidgetComp)
	{
		OriginWidgetComp->DestroyComponent();
		OriginWidgetComp = nullptr;
	}
	
	if (AimOffsetComp)
	{
		AimOffsetComp->DestroyComponent();
		AimOffsetComp = nullptr;
	}
#endif// WITH_EDITORONLY_DATA
	
	Super::BeginPlay();

	// By default, the sights relative transform should equal whatever
	// the GetDefaultSightsRelativeTransform implementation returns.
	TargetSightsRelativeTransform = GetDefaultSightsRelativeTransform();
}

void ATrueFPSWeaponBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HandleRecoil(DeltaSeconds);

	// Smooth sights toggling
	SightsRelativeTransform = UKismetMathLibrary::TInterpTo(SightsRelativeTransform, TargetSightsRelativeTransform, DeltaSeconds, 8.f);

	// Smooth wall offset
	WallOffsetTransformAlpha = UKismetMathLibrary::FInterpTo(WallOffsetTransformAlpha, CalculateWallOffsetTransformAlpha(), DeltaSeconds, 8.f);
}

void ATrueFPSWeaponBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	DetachMeshFromPawn();
}

void ATrueFPSWeaponBase::Destroyed()
{
	Super::Destroyed();

	StopSimulatingWeaponFire();
}

void ATrueFPSWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME( ThisClass, MyPawn );
	DOREPLIFETIME( ThisClass, TargetSightsRelativeTransform );

	DOREPLIFETIME_CONDITION( ThisClass, BurstCounter, COND_SkipOwner );
}

void ATrueFPSWeaponBase::OnEquip(const ATrueFPSWeaponBase* LastWeapon)
{
	AttachMeshToPawn();

	bPendingEquip = true;
	DetermineWeaponState();

	// Only play animation if last weapon is valid
	if (LastWeapon)
	{
		float Duration = PlayWeaponAnimation(EquipAnim);
		if (Duration <= 0.0f)
		{
			// failsafe
			Duration = 0.5f;
		}
		EquipStartedTime = GetWorld()->GetTimeSeconds();
		EquipDuration = Duration;

		GetWorldTimerManager().SetTimer(TimerHandle_OnEquipFinished, this, &ThisClass::OnEquipFinished, Duration, false);
	}
	else
	{
		OnEquipFinished();
	}

	if (MyPawn && MyPawn->IsLocallyControlled())
	{
		PlayWeaponSound(EquipSound);
	}
}

void ATrueFPSWeaponBase::OnEquipFinished()
{
	AttachMeshToPawn();

	bIsEquipped = true;
	bPendingEquip = false;

	// Determine the state so that the can reload checks will work
	DetermineWeaponState(); 
}

void ATrueFPSWeaponBase::OnUnEquip()
{
	DetachMeshFromPawn();
	bIsEquipped = false;
	StopFire();

	if (bPendingEquip)
	{
		StopWeaponAnimation(EquipAnim);
		bPendingEquip = false;

		GetWorldTimerManager().ClearTimer(TimerHandle_OnEquipFinished);
	}

	DetermineWeaponState();
}

void ATrueFPSWeaponBase::OnEnterInventory(ACharacter* NewOwner)
{
	SetOwningPawn(NewOwner);
}

void ATrueFPSWeaponBase::OnLeaveInventory()
{
	if (IsAttachedToPawn())
	{
		OnUnEquip();
	}

	if (GetLocalRole() == ROLE_Authority)
	{
		SetOwningPawn(nullptr);
	}
}

bool ATrueFPSWeaponBase::IsEquipped() const
{
	return bIsEquipped;
}

bool ATrueFPSWeaponBase::IsAttachedToPawn() const
{
	return bIsEquipped || bPendingEquip;
}

void ATrueFPSWeaponBase::StartFire()
{
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStartFire();
	}

	if (!bWantsToFire)
	{
		bWantsToFire = true;
		DetermineWeaponState();
	}
}

void ATrueFPSWeaponBase::StopFire()
{
	if ((GetLocalRole() < ROLE_Authority) && MyPawn && MyPawn->IsLocallyControlled())
	{
		ServerStopFire();
	}

	if (bWantsToFire)
	{
		bWantsToFire = false;
		DetermineWeaponState();
	}
}

void ATrueFPSWeaponBase::ToggleSights()
{
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerToggleSights();
	}
	else
	{
		TArray<ATrueFPSSightsAttachment*> AllSights;
		GetAttachments(AllSights);

		if (CurrentSights)
		{
			const int32 CurrentIndex = AllSights.Find(CurrentSights);
			CurrentSights = CurrentIndex != INDEX_NONE && AllSights.IsValidIndex(CurrentIndex + 1) ? AllSights[CurrentIndex + 1] : nullptr;
		}
		else if (AllSights.IsValidIndex(0))
		{
			CurrentSights = AllSights[0];
		}
		
		TargetSightsRelativeTransform = CurrentSights ? CurrentSights->GetSightsWorldTransform().GetRelativeTransform(GetActorTransform()) : GetDefaultSightsRelativeTransform();
	}
}

bool ATrueFPSWeaponBase::CanFire() const
{
	bool bCanFire = MyPawn && MyPawn->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_CanFire(MyPawn);
	bool bStateOKToFire = ( ( CurrentState ==  EWeaponState::Idle ) || ( CurrentState == EWeaponState::Firing) );	
	return (( bCanFire == true ) && ( bStateOKToFire == true ));
}

bool ATrueFPSWeaponBase::CanAim() const
{
	bool bIsCloseToWall = IsCloseToWall();
	
	return (bIsCloseToWall == false);
}

EWeaponState::Type ATrueFPSWeaponBase::GetCurrentState() const
{
	return CurrentState;
}

USkeletalMeshComponent* ATrueFPSWeaponBase::GetWeaponMesh() const
{
	// return (MyPawn != nullptr && IsFirstPerson()) ? Mesh1P : Mesh3P;
	return Mesh1P;
}

bool ATrueFPSWeaponBase::IsFirstPerson() const
{
	if (!MyPawn) return false;
	if (!MyPawn->Implements<UTrueFPSCharacterInterface>()) return false;
	return ITrueFPSCharacterInterface::Execute_IsFirstPerson(MyPawn);
}

void ATrueFPSWeaponBase::SetOwningPawn(ACharacter* NewOwner)
{
	if (MyPawn != NewOwner)
	{
		SetInstigator(NewOwner);
		MyPawn = NewOwner;
		// net owner for RPC calls
		SetOwner(NewOwner);
	}
}

float ATrueFPSWeaponBase::GetEquipStartedTime() const
{
	return EquipStartedTime;
}

float ATrueFPSWeaponBase::GetEquipDuration() const
{
	return EquipDuration;
}

void ATrueFPSWeaponBase::GetAttachments(TArray<ATrueFPSWeaponAttachmentBase*>& OutAttachments) const
{
	TArray<UTrueFPSWeaponAttachmentPoint*> AttachmentPoints;
	GetAttachmentPoints(AttachmentPoints);

	for (const UTrueFPSWeaponAttachmentPoint* AttachmentPoint : AttachmentPoints)
		if (ATrueFPSWeaponAttachmentBase* Attachment = AttachmentPoint->GetAttachment())
			OutAttachments.Add(Attachment);
}

template <typename T>
void ATrueFPSWeaponBase::GetAttachments(TArray<T*>& OutAttachments) const
{
	static_assert(TIsClass<T>::Value, TEXT("ATrueFPSWeaponBase::GetAttachments template parameter is not a class type"));

	TArray<UTrueFPSWeaponAttachmentPoint*> AttachmentPoints;
	GetAttachmentPoints(AttachmentPoints);

	for (const UTrueFPSWeaponAttachmentPoint* AttachmentPoint : AttachmentPoints)
		if (T* Attachment = Cast<T>(AttachmentPoint->GetAttachment()))
			OutAttachments.Add(Attachment);
}

void ATrueFPSWeaponBase::GetAttachmentsOfClass(TArray<ATrueFPSWeaponAttachmentBase*>& OutAttachments, const TSubclassOf<ATrueFPSWeaponAttachmentBase>& Class) const
{
	if(!Class) return;
	
	TArray<UTrueFPSWeaponAttachmentPoint*> AttachmentPoints;
	GetAttachmentPoints(AttachmentPoints);

	for (const UTrueFPSWeaponAttachmentPoint* AttachmentPoint : AttachmentPoints)
		if (IsValid(AttachmentPoint->GetAttachment()) && AttachmentPoint->GetAttachment()->IsA(Class))
			OutAttachments.Add(AttachmentPoint->GetAttachment());
}

void ATrueFPSWeaponBase::GetAttachmentPoints(TArray<UTrueFPSWeaponAttachmentPoint*>& OutAttachmentPoints) const
{
	GetComponents<UTrueFPSWeaponAttachmentPoint>(OutAttachmentPoints);
}

void ATrueFPSWeaponBase::ServerStartFire_Implementation()
{
	StartFire();
}

void ATrueFPSWeaponBase::ServerStopFire_Implementation()
{
	StopFire();
}

void ATrueFPSWeaponBase::ServerToggleSights_Implementation()
{
	ToggleSights();
}

void ATrueFPSWeaponBase::OnRep_MyPawn()
{
	if (MyPawn)
	{
		OnEnterInventory(MyPawn);
	}
	else
	{
		OnLeaveInventory();
	}
}

void ATrueFPSWeaponBase::OnRep_BurstCounter()
{
	if (BurstCounter > 0)
	{
		SimulateWeaponFire();
	}
	else
	{
		StopSimulatingWeaponFire();
	}
}

void ATrueFPSWeaponBase::SimulateWeaponFire()
{
	if (GetLocalRole() == ROLE_Authority && CurrentState != EWeaponState::Firing)
	{
		return;
	}

	if (!bLoopedFireAnim || !bPlayingFireAnim)
	{
		PlayWeaponAnimation(FireAnim);
		bPlayingFireAnim = true;
	}

	if (bLoopedFireSound)
	{
		if (FireAC == nullptr)
		{
			FireAC = PlayWeaponSound(FireLoopSound);
		}
	}
	else
	{
		PlayWeaponSound(FireSound);
	}

	// Adding Recoil
	AddRecoilInstance(WeaponAnimProperties.RecoilParams);

	ATrueFPSPlayerController* PC = (MyPawn != nullptr) ? Cast<ATrueFPSPlayerController>(MyPawn->Controller) : nullptr;
	if (PC != nullptr && PC->IsLocalController())
	{
		if (RecoilCurve != nullptr && MyPawn && MyPawn->Implements<UTrueFPSCharacterInterface>())
		{
			const FVector2D RecoilAmount(RecoilCurve->GetVectorValue(FMath::RandRange(0.f, 1.f)).X, RecoilCurve->GetVectorValue(FMath::RandRange(0.f, 1.f)).Y);
			ITrueFPSCharacterInterface::Execute_ApplyRecoil(MyPawn, RecoilAmount, RecoilSpeed, RecoilResetSpeed);
		}
		
		if (FireCameraShake != nullptr)
		{
			PC->ClientStartCameraShake(FireCameraShake, 1);
		}

		if (FireForceFeedback != nullptr/* && PC->IsVibrationEnabled()*/) // TODO: Setup Vibration Configurable
		{
			FForceFeedbackParameters FFParams;
			FFParams.Tag = "Weapon";
			PC->ClientPlayForceFeedback(FireForceFeedback, FFParams);
		}
	}
}

void ATrueFPSWeaponBase::StopSimulatingWeaponFire()
{
	if (bLoopedFireAnim && bPlayingFireAnim)
	{
		StopWeaponAnimation(FireAnim);
		bPlayingFireAnim = false;
	}

	if (FireAC)
	{
		FireAC->FadeOut(0.1f, 0.0f);
		FireAC = nullptr;

		PlayWeaponSound(FireFinishSound);
	}
}

void ATrueFPSWeaponBase::ServerHandleFiring_Implementation()
{
	const bool bShouldIncrementBurstCounter = CanFire();

	HandleFiring();

	if (bShouldIncrementBurstCounter)
	{
		// update firing FX on remote clients
		BurstCounter++;
	}
}

void ATrueFPSWeaponBase::HandleReFiring()
{
	// Update TimerIntervalAdjustment
	UWorld* MyWorld = GetWorld();

	float SlackTimeThisFrame = FMath::Max(0.0f, (MyWorld->TimeSeconds - LastFireTime) - WeaponConfig.TimeBetweenShots);

	if (bAllowAutomaticWeaponCatchup)
	{
		TimerIntervalAdjustment -= SlackTimeThisFrame;
	}

	HandleFiring();
}

void ATrueFPSWeaponBase::HandleFiring()
{
	if (CanFire())
	{
		if (GetNetMode() != NM_DedicatedServer)
		{
			SimulateWeaponFire();
		}

		if (MyPawn && MyPawn->IsLocallyControlled())
		{
			FireWeapon();
			
			// update firing FX on remote clients if function was called on server
			BurstCounter++;
		}
	}
	else if (MyPawn && MyPawn->IsLocallyControlled())
	{
		// stop weapon fire FX, but stay in Firing state
		if (BurstCounter > 0)
		{
			OnBurstFinished();
		}
	}
	else
	{
		OnBurstFinished();
	}

	if (MyPawn && MyPawn->IsLocallyControlled())
	{
		// local client will notify server
		if (GetLocalRole() < ROLE_Authority)
		{
			ServerHandleFiring();
		}

		// setup refire timer
		bRefiring = (CurrentState == EWeaponState::Firing && WeaponConfig.TimeBetweenShots > 0.0f);
		if (bRefiring)
		{
			GetWorldTimerManager().SetTimer(TimerHandle_HandleFiring, this, &ThisClass::HandleReFiring, FMath::Max<float>(WeaponConfig.TimeBetweenShots + TimerIntervalAdjustment, SMALL_NUMBER), false);
			TimerIntervalAdjustment = 0.f;
		}
	}

	LastFireTime = GetWorld()->GetTimeSeconds();
}

void ATrueFPSWeaponBase::OnBurstStarted()
{
	// start firing, can be delayed to satisfy TimeBetweenShots
	const float GameTime = GetWorld()->GetTimeSeconds();
	if (LastFireTime > 0 && WeaponConfig.TimeBetweenShots > 0.0f &&
		LastFireTime + WeaponConfig.TimeBetweenShots > GameTime)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_HandleFiring, this, &ThisClass::HandleFiring, LastFireTime + WeaponConfig.TimeBetweenShots - GameTime, false);
	}
	else
	{
		HandleFiring();
	}
}

void ATrueFPSWeaponBase::OnBurstFinished()
{
	// stop firing FX on remote clients
	BurstCounter = 0;

	// stop firing FX locally, unless it's a dedicated server
	//if (GetNetMode() != NM_DedicatedServer)
	//{
	StopSimulatingWeaponFire();
	//}
	
	GetWorldTimerManager().ClearTimer(TimerHandle_HandleFiring);
	bRefiring = false;

	// reset firing interval adjustment
	TimerIntervalAdjustment = 0.0f;
}

void ATrueFPSWeaponBase::SetWeaponState(EWeaponState::Type NewState)
{
	const EWeaponState::Type PrevState = CurrentState;

	if (PrevState == EWeaponState::Firing && NewState != EWeaponState::Firing)
	{
		OnBurstFinished();
	}

	CurrentState = NewState;

	if (PrevState != EWeaponState::Firing && NewState == EWeaponState::Firing)
	{
		OnBurstStarted();
	}
}

void ATrueFPSWeaponBase::DetermineWeaponState()
{
	EWeaponState::Type NewState = EWeaponState::Idle;

	if (bIsEquipped)
	{
		if ( ( bWantsToFire == true ) && ( CanFire() == true ))
		{
			NewState = EWeaponState::Firing;
		}
	}
	else if (bPendingEquip)
	{
		NewState = EWeaponState::Equipping;
	}

	SetWeaponState(NewState);
}

void ATrueFPSWeaponBase::HandleRecoil(float DeltaSeconds)
{
	for (int32 i = 0; i < RecoilInstances.Num(); i++)
	{
		FRecoilInstance& RecoilInstance = RecoilInstances[i];
		
		const float CurrentTime = RecoilInstance.CurrentTime += DeltaSeconds * RecoilInstance.PlayRate;

		const FVector CurveRotationVector = RecoilInstance.RotationCurve->GetVectorValue(CurrentTime);
		FRotator CurveRotation(CurveRotationVector.Y, CurveRotationVector.Z, CurveRotationVector.X);
		FVector CurveLocation(RecoilInstance.LocationCurve->GetVectorValue(CurrentTime));
		const FTransform CurrentValue(CurveRotation, CurveLocation);

		RecoilOffset = CurrentValue;

		if (CurrentTime >= RecoilInstance.Lifetime)
		{ // If we have surpassed the duration of the recoil curve...
			RecoilInstances.RemoveAt(i--);
	
			if (RecoilInstances.IsEmpty())
			{
				RecoilOffset = FTransform::Identity;
			}
		}
	}
}

void ATrueFPSWeaponBase::AttachMeshToPawn()
{
	if (MyPawn)
	{
		// Remove and hide both first and third person meshes
		DetachMeshFromPawn();

		FName AttachPoint = MyPawn->Implements<UTrueFPSCharacterInterface>() ? ITrueFPSCharacterInterface::Execute_GetWeaponAttachPoint(MyPawn) : FName("hand_r");
		if(MyPawn->IsLocallyControlled() == true)
		{
			USkeletalMeshComponent* PawnMesh1p = MyPawn->Implements<UTrueFPSCharacterInterface>() ? ITrueFPSCharacterInterface::Execute_GetSpecificPawnMesh(MyPawn, true) : MyPawn->GetMesh();
			USkeletalMeshComponent* PawnMesh3p = MyPawn->Implements<UTrueFPSCharacterInterface>() ? ITrueFPSCharacterInterface::Execute_GetSpecificPawnMesh(MyPawn, false) : MyPawn->GetMesh();
			Mesh1P->SetHiddenInGame(false);
			Mesh3P->SetHiddenInGame(true);
			Mesh1P->AttachToComponent(PawnMesh1p, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
			Mesh3P->AttachToComponent(PawnMesh3p, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
			Mesh1P->SetRelativeTransform(WeaponAnimProperties.PlacementTransform);
			Mesh3P->SetRelativeTransform(WeaponAnimProperties.PlacementTransform);
		}
		else
		{
			USkeletalMeshComponent* UseWeaponMesh = GetWeaponMesh();
			USkeletalMeshComponent* UsePawnMesh = MyPawn->GetMesh();
			UseWeaponMesh->AttachToComponent(UsePawnMesh, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
			UseWeaponMesh->SetRelativeTransform(WeaponAnimProperties.PlacementTransform);
			UseWeaponMesh->SetHiddenInGame( false );
		}

		TArray<ATrueFPSWeaponAttachmentBase*> CurrentAttachments;
		GetAttachments(CurrentAttachments);
		for (const auto& CurrentAttachment : CurrentAttachments)
			if (CurrentAttachment)
				CurrentAttachment->SetVisibility(true);
	}
}

void ATrueFPSWeaponBase::DetachMeshFromPawn()
{
	Mesh1P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	Mesh1P->SetHiddenInGame(true);

	Mesh3P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	Mesh3P->SetHiddenInGame(true);

	TArray<ATrueFPSWeaponAttachmentBase*> CurrentAttachments;
	GetAttachments(CurrentAttachments);
	for (const auto& CurrentAttachment : CurrentAttachments)
		if (CurrentAttachment)
			CurrentAttachment->SetVisibility(false);
}

UAudioComponent* ATrueFPSWeaponBase::PlayWeaponSound(USoundBase* Sound)
{
	UAudioComponent* AC = nullptr;
	if (Sound && MyPawn)
	{
		AC = UGameplayStatics::SpawnSoundAttached(Sound, MyPawn->GetRootComponent(), SoundsAttachPoint);
	}

	return AC;
}

float ATrueFPSWeaponBase::PlayWeaponAnimation(const FWeaponAnim& Animation)
{
	float Duration = 0.0f;
	if (MyPawn)
	{
		const bool bIsFirstPerson = IsFirstPerson();
		
		UAnimMontage* PawnAnim = bIsFirstPerson ? Animation.Pawn1P : Animation.Pawn3P;
		if (PawnAnim)
		{
			Duration = MyPawn->PlayAnimMontage(PawnAnim);
		}

		UAnimMontage* WeaponAnim = bIsFirstPerson ? Animation.Weapon1P : Animation.Weapon3P;
		if (WeaponAnim)
		{
			GetWeaponMesh()->PlayAnimation(WeaponAnim, false);
		}
	}

	return Duration;
}

void ATrueFPSWeaponBase::StopWeaponAnimation(const FWeaponAnim& Animation)
{
	if (MyPawn)
	{
		const bool bIsFirstPerson = IsFirstPerson();
		
		UAnimMontage* UseAnim = bIsFirstPerson ? Animation.Pawn1P : Animation.Pawn3P;
		if (UseAnim)
		{
			MyPawn->StopAnimMontage(UseAnim);
		}
	}
}

FVector ATrueFPSWeaponBase::GetAdjustedAim() const
{
	ATrueFPSPlayerController* const PlayerController = GetInstigatorController<ATrueFPSPlayerController>();
	FVector FinalAim = FVector::ZeroVector;
	// If we have a player controller use it for the aim
	if (PlayerController)
	{
		FVector CamLoc;
		FRotator CamRot;
		PlayerController->GetPlayerViewPoint(CamLoc, CamRot);
		FinalAim = CamRot.Vector();
	}
	else if (GetInstigator())
	{
		// Now see if we have an AI controller - we will want to get the aim from there if we do
		ATrueFPSAIController* AIController = MyPawn ? Cast<ATrueFPSAIController>(MyPawn->Controller) : NULL;
		if(AIController != NULL )
		{
			FinalAim = AIController->GetControlRotation().Vector();
		}
		else
		{			
			FinalAim = GetInstigator()->GetBaseAimRotation().Vector();
		}
	}

	return FinalAim;
}

FVector ATrueFPSWeaponBase::GetCameraAim() const
{
	ATrueFPSPlayerController* const PlayerController = GetInstigatorController<ATrueFPSPlayerController>();
	FVector FinalAim = FVector::ZeroVector;

	if (PlayerController)
	{
		FVector CamLoc;
		FRotator CamRot;
		PlayerController->GetPlayerViewPoint(CamLoc, CamRot);
		FinalAim = CamRot.Vector();
	}
	else if (GetInstigator())
	{
		FinalAim = GetInstigator()->GetBaseAimRotation().Vector();		
	}

	return FinalAim;
}

FVector ATrueFPSWeaponBase::GetCameraDamageStartLocation(const FVector& AimDir) const
{
	APlayerController* PC = MyPawn ? Cast<APlayerController>(MyPawn->Controller) : nullptr;
	FVector OutStartTrace = FVector::ZeroVector;

	if (PC)
	{
		// use player's camera
		FRotator UnusedRot;
		PC->GetPlayerViewPoint(OutStartTrace, UnusedRot);

		// Adjust trace so there is nothing blocking the ray between the camera and the pawn, and calculate distance from adjusted start
		OutStartTrace = OutStartTrace + AimDir * ((GetInstigator()->GetActorLocation() - OutStartTrace) | AimDir);
	}

	return OutStartTrace;
}

FTransform ATrueFPSWeaponBase::GetOffsetTransform() const
{
	const FTransform DefaultOffsetTransform = RecoilOffset * OffsetTransform;
	
	return UKismetMathLibrary::TLerp(DefaultOffsetTransform, DefaultOffsetTransform * WeaponAnimProperties.WallOffsetTransform, GetWallOffsetTransformAlpha());
}

FTransform ATrueFPSWeaponBase::GetDefaultSightsRelativeTransform() const
{
	return FTransform(FVector(-WeaponAnimProperties.AimOffset, 0.f, 0.f)) * GetWeaponMesh()->GetSocketTransform(WeaponAnimProperties.SightSocketName, RTS_Actor);
}

FTransform ATrueFPSWeaponBase::GetSightsWorldTransform() const
{
	return SightsRelativeTransform * GetActorTransform();
}

FTransform ATrueFPSWeaponBase::GetOriginRelativeTransform() const
{
	const FQuat Rot(WeaponAnimProperties.OriginRelativeTransform.GetRotation());
	return FTransform(Rot, Rot.RotateVector(WeaponAnimProperties.OriginRelativeTransform.GetLocation()), WeaponAnimProperties.OriginRelativeTransform.GetScale3D());
}

FTransform ATrueFPSWeaponBase::GetOriginWorldTransform() const
{
	return GetOriginRelativeTransform() * GetActorTransform();
}

FTransform ATrueFPSWeaponBase::GetOrientationRelativeTransform(const float AimingValue) const
{
	if (AimingValue <= 0.f) return GetOriginRelativeTransform();

	// Get the OriginRelativeTransform value with it's origin-space
	// Y location being equal to the sights origin-space Y location.
	FTransform OriginCenteredTransform = GetOriginRelativeTransform();
	OriginCenteredTransform.SetLocation(OriginCenteredTransform.GetLocation() +
		OriginCenteredTransform.GetRotation().RotateVector(FVector(0.f, WeaponAnimProperties.OriginRelativeTransform.GetRotation().UnrotateVector(SightsRelativeTransform.GetLocation()).Y, 0.f)));

	// Get the centered origin position rotated about the sights transform
	const FTransform SightsOrigin(SightsRelativeTransform.GetRotation(),
		(OriginCenteredTransform.GetRelativeTransform(SightsRelativeTransform) * SightsRelativeTransform.GetRotation() * WeaponAnimProperties.OriginRelativeTransform.GetRotation().Inverse() * SightsRelativeTransform).GetLocation());

	// Blend between the standard origin transform and the
	// sights origin by the aiming value.
	FTransform BlendedTransform;
	BlendedTransform.Blend(GetOriginRelativeTransform(), SightsOrigin, AimingValue);
	return BlendedTransform;
}

FTransform ATrueFPSWeaponBase::GetOrientationWorldTransform(const float AimingValue) const
{
	return GetOrientationRelativeTransform(AimingValue) * GetActorTransform();
}

float ATrueFPSWeaponBase::CalculateWallOffsetTransformAlpha() const
{
	if (!GetPawnOwner()) return 0.f;
	if (!GetPawnOwner()->Implements<UTrueFPSCharacterInterface>()) return 0.f;
	
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(GetWallOffsetTransformAlpha), false, this);
	TraceParams.AddIgnoredActor(GetInstigator());
	
	const FVector StartTrace = ITrueFPSCharacterInterface::Execute_GetFirstPersonCameraTarget(GetPawnOwner());
	const FVector EndTrace = StartTrace + GetPawnOwner()->GetBaseAimRotation().Vector() * WeaponAnimProperties.WallOffsetTransformMaxDistance;

	FHitResult Hit(ForceInit);
	if (GetWorld()->LineTraceSingleByChannel(Hit, StartTrace, EndTrace, ECC_Visibility, TraceParams))
	{
		const float WallMinDist = WeaponAnimProperties.WallOffsetTransformMinDistance;
		const float WallMaxDist = WeaponAnimProperties.WallOffsetTransformMaxDistance;
	
		return 1.f - FMath::Clamp((Hit.Distance - WallMinDist) / (WallMaxDist - WallMinDist), 0.f, 1.f);
	}

	return 0.f;
}

#if WITH_EDITORONLY_DATA
void ATrueFPSWeaponBase::RegisterAllComponents()
{
	Super::RegisterAllComponents();
	
	if(!OriginWidgetComp)
	{
		if(bShowOriginWidget)
		{
			OriginWidgetComp = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transactional | RF_TextExportTransient);
			if(OriginWidgetComp)
			{
				UStaticMesh* const WidgetMesh(OriginVisualizationMesh ? OriginVisualizationMesh : LoadObject<UStaticMesh>(nullptr, TEXT("/Niagara/DefaultAssets/S_Gnomon.S_Gnomon")));
				
				OriginWidgetComp->SetupAttachment(RootComponent);
				OriginWidgetComp->SetStaticMesh(WidgetMesh);
				OriginWidgetComp->SetUsingAbsoluteScale(true);
				OriginWidgetComp->SetIsVisualizationComponent(true);
				OriginWidgetComp->SetCastShadow(false);
				OriginWidgetComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
				OriginWidgetComp->CreationMethod = EComponentCreationMethod::Instance;
				OriginWidgetComp->SetComponentTickEnabled(false);
				OriginWidgetComp->SetHiddenInGame(true);
				OriginWidgetComp->SetRelativeTransform(FTransform(
					WeaponAnimProperties.OriginRelativeTransform.GetRotation(),
					WeaponAnimProperties.OriginRelativeTransform.GetRotation().RotateVector(
						WeaponAnimProperties.OriginRelativeTransform.GetLocation()),
					WeaponAnimProperties.OriginRelativeTransform.GetScale3D()));
				OriginWidgetComp->RegisterComponentWithWorld(GetWorld());
			}
		}
	}
	else if(!bShowOriginWidget)
	{
		OriginWidgetComp->DestroyComponent();
		OriginWidgetComp = nullptr;
	}

	STATIC_MESH_VISUALIZER_ACTOR_IMPLEMENTATION(AimOffset,
		AimOffsetComp->SetRelativeTransform(GetDefaultSightsRelativeTransform());
	);
}
#endif// WITH_EDITORONLY_DATA
