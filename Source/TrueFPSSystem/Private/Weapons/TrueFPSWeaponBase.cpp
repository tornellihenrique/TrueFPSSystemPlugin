// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapons/TrueFPSWeaponBase.h"

#include "TrueFPSSystem.h"
#include "Character/TrueFPSCharacterInterface.h"
#include "GameFramework/Character.h"
#include "VisualizationMacros.h"
#include "Character/TrueFPSPlayerController.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundCue.h"
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

	bLoopedMuzzleFX = false;
	bLoopedFireAnim = false;
	bPlayingFireAnim = false;
	bIsEquipped = false;
	bWantsToFire = false;
	bPendingReload = false;
	bPendingEquip = false;
	CurrentState = EWeaponState::Idle;

	CurrentAmmo = 0;
	CurrentAmmoInClip = 0;
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
}

void ATrueFPSWeaponBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (WeaponConfig.InitialClips > 0)
	{
		CurrentAmmoInClip = WeaponConfig.AmmoPerClip;
		CurrentAmmo = WeaponConfig.AmmoPerClip * WeaponConfig.InitialClips;
	}

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

	DOREPLIFETIME_CONDITION( ThisClass, CurrentAmmo, COND_OwnerOnly );
	DOREPLIFETIME_CONDITION( ThisClass, CurrentAmmoInClip, COND_OwnerOnly );

	DOREPLIFETIME_CONDITION( ThisClass, BurstCounter, COND_SkipOwner );
	DOREPLIFETIME_CONDITION( ThisClass, bPendingReload,	COND_SkipOwner );
}

void ATrueFPSWeaponBase::GiveAmmo(int AddAmount)
{
	const int32 MissingAmmo = FMath::Max(0, WeaponConfig.MaxAmmo - CurrentAmmo);
	AddAmount = FMath::Min(AddAmount, MissingAmmo);
	CurrentAmmo += AddAmount;
	
	// start reload if clip was empty
	if (GetCurrentAmmoInClip() <= 0 &&
		CanReload() &&
		MyPawn && MyPawn->Implements<UTrueFPSCharacterInterface>() && (ITrueFPSCharacterInterface::Execute_GetCurrentWeapon(MyPawn) == this))
	{
		ClientStartReload();
	}
}

void ATrueFPSWeaponBase::UseAmmo()
{
	if (!HasInfiniteAmmo())
	{
		CurrentAmmoInClip--;
	}

	if (!HasInfiniteAmmo() && !HasInfiniteClip())
	{
		CurrentAmmo--;
	}
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
	
	if (MyPawn)
	{
		// try to reload empty clip
		if (MyPawn->IsLocallyControlled() &&
			CurrentAmmoInClip <= 0 &&
			CanReload())
		{
			StartReload();
		}
	}
}

void ATrueFPSWeaponBase::OnUnEquip()
{
	DetachMeshFromPawn();
	bIsEquipped = false;
	StopFire();

	if (bPendingReload)
	{
		StopWeaponAnimation(ReloadAnim);
		bPendingReload = false;

		GetWorldTimerManager().ClearTimer(TimerHandle_StopReload);
		GetWorldTimerManager().ClearTimer(TimerHandle_ReloadWeapon);
	}

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

void ATrueFPSWeaponBase::StartReload(bool bFromReplication)
{
	if (!bFromReplication && GetLocalRole() < ROLE_Authority)
	{
		ServerStartReload();
	}

	if (bFromReplication || CanReload())
	{
		bPendingReload = true;
		DetermineWeaponState();

		float AnimDuration = PlayWeaponAnimation(ReloadAnim);		
		if (AnimDuration <= 0.0f)
		{
			AnimDuration = WeaponConfig.NoAnimReloadDuration;
		}

		GetWorldTimerManager().SetTimer(TimerHandle_StopReload, this, &ThisClass::StopReload, AnimDuration, false);
		if (GetLocalRole() == ROLE_Authority)
		{
			GetWorldTimerManager().SetTimer(TimerHandle_ReloadWeapon, this, &ThisClass::ReloadWeapon, FMath::Max(0.1f, AnimDuration - 0.1f), false);
		}
		
		if (MyPawn && MyPawn->IsLocallyControlled())
		{
			PlayWeaponSound(ReloadSound);
		}
	}
}

void ATrueFPSWeaponBase::StopReload()
{
	if (CurrentState == EWeaponState::Reloading)
	{
		bPendingReload = false;
		DetermineWeaponState();
		StopWeaponAnimation(ReloadAnim);
	}
}

void ATrueFPSWeaponBase::ReloadWeapon()
{
	int32 ClipDelta = FMath::Min(WeaponConfig.AmmoPerClip - CurrentAmmoInClip, CurrentAmmo - CurrentAmmoInClip);

	if (HasInfiniteClip())
	{
		ClipDelta = WeaponConfig.AmmoPerClip - CurrentAmmoInClip;
	}

	if (ClipDelta > 0)
	{
		CurrentAmmoInClip += ClipDelta;
	}

	if (HasInfiniteClip())
	{
		CurrentAmmo = FMath::Max(CurrentAmmoInClip, CurrentAmmo);
	}
}

void ATrueFPSWeaponBase::ClientStartReload_Implementation()
{
	StartReload();
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
	return (( bCanFire == true ) && ( bStateOKToFire == true ) && ( bPendingReload == false ));
}

bool ATrueFPSWeaponBase::CanReload() const
{
	bool bCanReload = (!MyPawn || (MyPawn && MyPawn->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_CanReload(MyPawn)));
	bool bGotAmmo = (CurrentAmmoInClip < WeaponConfig.AmmoPerClip) && (CurrentAmmo - CurrentAmmoInClip > 0 || HasInfiniteClip());
	bool bStateOKToReload = ((CurrentState == EWeaponState::Idle) || (CurrentState == EWeaponState::Firing));
	return ((bCanReload == true) && (bGotAmmo == true) && (bStateOKToReload == true));	
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

int32 ATrueFPSWeaponBase::GetCurrentAmmo() const
{
	return CurrentAmmo;
}

int32 ATrueFPSWeaponBase::GetCurrentAmmoInClip() const
{
	return CurrentAmmoInClip;
}

int32 ATrueFPSWeaponBase::GetAmmoPerClip() const
{
	return WeaponConfig.AmmoPerClip;
}

int32 ATrueFPSWeaponBase::GetMaxAmmo() const
{
	return WeaponConfig.MaxAmmo;
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

bool ATrueFPSWeaponBase::HasInfiniteAmmo() const
{
	return WeaponConfig.bInfiniteAmmo;
}

bool ATrueFPSWeaponBase::HasInfiniteClip() const
{
	return WeaponConfig.bInfiniteClip;
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

void ATrueFPSWeaponBase::ServerStartReload_Implementation()
{
	StartReload();
}

void ATrueFPSWeaponBase::ServerStopReload_Implementation()
{
	StopReload();
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

void ATrueFPSWeaponBase::OnRep_Reload()
{
	if (bPendingReload)
	{
		StartReload(true);
	}
	else
	{
		StopReload();
	}
}

void ATrueFPSWeaponBase::SimulateWeaponFire()
{
	if (GetLocalRole() == ROLE_Authority && CurrentState != EWeaponState::Firing)
	{
		return;
	}

	if (MuzzleFX)
	{
		USkeletalMeshComponent* UseWeaponMesh = GetWeaponMesh();
		if (!bLoopedMuzzleFX || MuzzlePSC == nullptr)
		{
			// Split screen requires we create 2 effects. One that we see and one that the other player sees.
			if( (MyPawn != nullptr ) && ( MyPawn->IsLocallyControlled() == true ) )
			{
				AController* PlayerCon = MyPawn->GetController();				
				if( PlayerCon != nullptr )
				{
					// Mesh1P->GetSocketLocation(MuzzleAttachPoint);
					MuzzlePSC = UGameplayStatics::SpawnEmitterAttached(MuzzleFX, Mesh1P, MuzzleAttachPoint);
					MuzzlePSC->bOwnerNoSee = false;
					MuzzlePSC->bOnlyOwnerSee = true;

					// Mesh3P->GetSocketLocation(MuzzleAttachPoint);
					MuzzlePSCSecondary = UGameplayStatics::SpawnEmitterAttached(MuzzleFX, Mesh3P, MuzzleAttachPoint);
					MuzzlePSCSecondary->bOwnerNoSee = true;
					MuzzlePSCSecondary->bOnlyOwnerSee = false;				
				}				
			}
			else
			{
				MuzzlePSC = UGameplayStatics::SpawnEmitterAttached(MuzzleFX, UseWeaponMesh, MuzzleAttachPoint);
			}
		}
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
	if (bLoopedMuzzleFX )
	{
		if( MuzzlePSC != nullptr)
		{
			MuzzlePSC->DeactivateSystem();
			MuzzlePSC = nullptr;
		}
		if( MuzzlePSCSecondary != nullptr)
		{
			MuzzlePSCSecondary->DeactivateSystem();
			MuzzlePSCSecondary = nullptr;
		}
	}

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
	const bool bShouldUpdateAmmo = (CurrentAmmoInClip > 0 && CanFire());

	HandleFiring();

	if (bShouldUpdateAmmo)
	{
		// update ammo
		UseAmmo();

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
	if ((CurrentAmmoInClip > 0 || HasInfiniteClip() || HasInfiniteAmmo()) && CanFire())
	{
		if (GetNetMode() != NM_DedicatedServer)
		{
			SimulateWeaponFire();
		}

		if (MyPawn && MyPawn->IsLocallyControlled())
		{
			if (WeaponConfig.ShotsByFiring > 1)
			{
				for (int32 i = 0; i < WeaponConfig.ShotsByFiring; i++)
				{
					FireWeapon();
				}
			}
			else
			{
				FireWeapon();
			}

			UseAmmo();
			
			// update firing FX on remote clients if function was called on server
			BurstCounter++;
		}
	}
	else if (CanReload())
	{
		StartReload();
	}
	else if (MyPawn && MyPawn->IsLocallyControlled())
	{
		if (GetCurrentAmmo() == 0 && !bRefiring)
		{
			PlayWeaponSound(OutOfAmmoSound);
		}
		
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

		// reload after firing last round
		if (CurrentAmmoInClip <= 0 && CanReload())
		{
			StartReload();
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
		if( bPendingReload  )
		{
			if( CanReload() == false )
			{
				NewState = CurrentState;
			}
			else
			{
				NewState = EWeaponState::Reloading;
			}
		}		
		else if ( (bPendingReload == false ) && ( bWantsToFire == true ) && ( CanFire() == true ))
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
	// for (int32 i = 0; i < RecoilInstances.Num(); i++)
	// {
	// 	FRecoilInstance& RecoilInstance = RecoilInstances[i];
	// 	
	// 	const float LastTime = RecoilInstance.CurrentTime;
	// 	const float CurrentTime = RecoilInstance.CurrentTime += DeltaSeconds * RecoilInstance.PlayRate;
	//
	// 	// LastTime's Transform
	// 	const FVector LastCurveRotationVector = RecoilInstance.RotationCurve->GetVectorValue(LastTime);
	// 	FRotator LastCurveRotation(LastCurveRotationVector.Y, LastCurveRotationVector.Z, LastCurveRotationVector.X);
	// 	FVector LastCurveLocation(RecoilInstance.LocationCurve->GetVectorValue(LastTime));
	// 	FTransform LastValue(LastCurveRotation, LastCurveLocation);
	//
	// 	// CurrentTime's Transform
	// 	const FVector CurveRotationVector = RecoilInstance.RotationCurve->GetVectorValue(CurrentTime);
	// 	FRotator CurveRotation(CurveRotationVector.Y, CurveRotationVector.Z, CurveRotationVector.X);
	// 	FVector CurveLocation(RecoilInstance.LocationCurve->GetVectorValue(CurrentTime));
	// 	FTransform CurrentValue(CurveRotation, CurveLocation);
	// 	
	// 	// Accumulate the difference of this time's transform to last time's
	// 	const FTransform& Difference = CurrentValue.GetRelativeTransform(LastValue);
	// 	RecoilOffset *= Difference;
	// 	
	// 	if (CurrentTime >= RecoilInstance.Lifetime)
	// 	{ // If we have surpassed the duration of the recoil curve...
	// 		RecoilInstances.RemoveAt(i--);
	//
	// 		if (RecoilInstances.IsEmpty())
	// 		{
	// 			RecoilOffset = FTransform::Identity;
	// 		}
	// 	}
	// }

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
			Mesh3P->SetHiddenInGame(false);
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

UAudioComponent* ATrueFPSWeaponBase::PlayWeaponSound(USoundCue* Sound)
{
	UAudioComponent* AC = nullptr;
	if (Sound && MyPawn)
	{
		AC = UGameplayStatics::SpawnSoundAttached(Sound, MyPawn->GetRootComponent(), MuzzleAttachPoint);
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
	// If it's close to wall, use muzzle's rotation
	if (IsCloseToWall()) return GetMuzzleDirection();
	
	APlayerController* const PlayerController = GetInstigatorController<APlayerController>();
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
		FinalAim = GetInstigator()->GetBaseAimRotation().Vector();
	}

	return FinalAim;
}

FVector ATrueFPSWeaponBase::GetCameraAim() const
{
	APlayerController* const PlayerController = GetInstigatorController<APlayerController>();
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

FVector ATrueFPSWeaponBase::GetMuzzleLocation() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketLocation(MuzzleAttachPoint);
}

FVector ATrueFPSWeaponBase::GetMuzzleDirection() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketRotation(MuzzleAttachPoint).Vector();
}

FHitResult ATrueFPSWeaponBase::WeaponTrace(const FVector& StartTrace, const FVector& EndTrace) const
{
	// Perform trace to retrieve hit info
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WeaponTrace), true, GetInstigator());
	TraceParams.bReturnPhysicalMaterial = true;

	FHitResult Hit(ForceInit);
	GetWorld()->LineTraceSingleByChannel(Hit, StartTrace, EndTrace, COLLISION_WEAPON, TraceParams);

	return Hit;
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

float ATrueFPSWeaponBase::GetWallOffsetTransformAlpha() const
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
