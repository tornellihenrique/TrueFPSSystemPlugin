// Fill out your copyright notice in the Description page of Project Settings.

#include "Weapons/TrueFPSFireWeaponBase.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Character/TrueFPSCharacterInterface.h"
#include "Character/TrueFPSPlayerController.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

ATrueFPSFireWeaponBase::ATrueFPSFireWeaponBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bLoopedMuzzleFX = false;
	bPendingReload = false;

	CurrentAmmo = 0;
	CurrentAmmoInClip = 0;
}

void ATrueFPSFireWeaponBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (FireWeaponConfig.InitialClips > 0)
	{
		CurrentAmmoInClip = FireWeaponConfig.AmmoPerClip;
		CurrentAmmo = FireWeaponConfig.AmmoPerClip * FireWeaponConfig.InitialClips;
	}
}

void ATrueFPSFireWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION( ThisClass, CurrentAmmo, COND_OwnerOnly );
	DOREPLIFETIME_CONDITION( ThisClass, CurrentAmmoInClip, COND_OwnerOnly );

	DOREPLIFETIME_CONDITION( ThisClass, bPendingReload,	COND_SkipOwner );
}

int32 ATrueFPSFireWeaponBase::GetCurrentAmmo() const
{
	return CurrentAmmo;
}

int32 ATrueFPSFireWeaponBase::GetCurrentAmmoInClip() const
{
	return CurrentAmmoInClip;
}

int32 ATrueFPSFireWeaponBase::GetAmmoPerClip() const
{
	return FireWeaponConfig.AmmoPerClip;
}

int32 ATrueFPSFireWeaponBase::GetMaxAmmo() const
{
	return FireWeaponConfig.MaxAmmo;
}

bool ATrueFPSFireWeaponBase::HasInfiniteAmmo() const
{
	const ATrueFPSPlayerController* MyPC = (MyPawn != NULL) ? Cast<const ATrueFPSPlayerController>(MyPawn->Controller) : NULL;
	return FireWeaponConfig.bInfiniteAmmo || (MyPC && MyPC->HasInfiniteAmmo());
}

bool ATrueFPSFireWeaponBase::HasInfiniteClip() const
{
	const ATrueFPSPlayerController* MyPC = (MyPawn != NULL) ? Cast<const ATrueFPSPlayerController>(MyPawn->Controller) : NULL;
	return FireWeaponConfig.bInfiniteClip || (MyPC && MyPC->HasInfiniteClip());
}

void ATrueFPSFireWeaponBase::GiveAmmo(int AddAmount)
{
	const int32 MissingAmmo = FMath::Max(0, FireWeaponConfig.MaxAmmo - CurrentAmmo);
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

void ATrueFPSFireWeaponBase::UseAmmo()
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

void ATrueFPSFireWeaponBase::OnUnEquip()
{
	if (bPendingReload)
	{
		StopWeaponAnimation(ReloadAnim);
		bPendingReload = false;

		GetWorldTimerManager().ClearTimer(TimerHandle_StopReload);
		GetWorldTimerManager().ClearTimer(TimerHandle_ReloadWeapon);
	}
	
	Super::OnUnEquip();
}

void ATrueFPSFireWeaponBase::OnEquipFinished()
{
	Super::OnEquipFinished();
	
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

void ATrueFPSFireWeaponBase::StartReload(bool bFromReplication)
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
			AnimDuration = FireWeaponConfig.NoAnimReloadDuration;
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

void ATrueFPSFireWeaponBase::StopReload()
{
	if (CurrentState == EWeaponState::Reloading)
	{
		bPendingReload = false;
		DetermineWeaponState();
		StopWeaponAnimation(ReloadAnim);
	}
}

void ATrueFPSFireWeaponBase::ReloadWeapon()
{
	int32 ClipDelta = FMath::Min(FireWeaponConfig.AmmoPerClip - CurrentAmmoInClip, CurrentAmmo - CurrentAmmoInClip);

	if (HasInfiniteClip())
	{
		ClipDelta = FireWeaponConfig.AmmoPerClip - CurrentAmmoInClip;
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
void ATrueFPSFireWeaponBase::ClientStartReload_Implementation()
{
	StartReload();
}

bool ATrueFPSFireWeaponBase::CanFire() const
{
	return ( Super::CanFire() ) && ( bPendingReload == false );
}

bool ATrueFPSFireWeaponBase::CanReload() const
{
	bool bCanReload = (!MyPawn || (MyPawn && MyPawn->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_CanReload(MyPawn)));
	bool bGotAmmo = (CurrentAmmoInClip < FireWeaponConfig.AmmoPerClip) && (CurrentAmmo - CurrentAmmoInClip > 0 || HasInfiniteClip());
	bool bStateOKToReload = ((CurrentState == EWeaponState::Idle) || (CurrentState == EWeaponState::Firing));
	return ((bCanReload == true) && (bGotAmmo == true) && (bStateOKToReload == true));	
}

void ATrueFPSFireWeaponBase::ServerStartReload_Implementation()
{
	StartReload();
}

void ATrueFPSFireWeaponBase::ServerStopReload_Implementation()
{
	StopReload();
}

void ATrueFPSFireWeaponBase::SimulateWeaponFire()
{
	Super::SimulateWeaponFire();
	
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

	if (NiagaraMuzzleFX.LoadSynchronous())
	{
		USkeletalMeshComponent* UseWeaponMesh = GetWeaponMesh();
		if (!bLoopedMuzzleFX || NiagaraMuzzleNC == nullptr)
		{
			// Split screen requires we create 2 effects. One that we see and one that the other player sees.
			if( (MyPawn != nullptr ) && ( MyPawn->IsLocallyControlled() == true ) )
			{
				AController* PlayerCon = MyPawn->GetController();				
				if( PlayerCon != nullptr )
				{
					NiagaraMuzzleNC = UNiagaraFunctionLibrary::SpawnSystemAttached(NiagaraMuzzleFX.Get(), Mesh1P, MuzzleAttachPoint, FVector(ForceInit), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
					NiagaraMuzzleNC->bOwnerNoSee = false;
					NiagaraMuzzleNC->bOnlyOwnerSee = true;
					NiagaraMuzzleNC->SetNiagaraVariableBool(NiagaraMuzzleTriggerParam.ToString(), true);

					NiagaraMuzzleNCSecondary = UNiagaraFunctionLibrary::SpawnSystemAttached(NiagaraMuzzleFX.Get(), Mesh3P, MuzzleAttachPoint, FVector(ForceInit), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
					NiagaraMuzzleNCSecondary->bOwnerNoSee = true;
					NiagaraMuzzleNCSecondary->bOnlyOwnerSee = false;
					NiagaraMuzzleNCSecondary->SetNiagaraVariableBool(NiagaraMuzzleTriggerParam.ToString(), true);
				}				
			}
			else
			{
				NiagaraMuzzleNC = UNiagaraFunctionLibrary::SpawnSystemAttached(NiagaraMuzzleFX.Get(), UseWeaponMesh, MuzzleAttachPoint, FVector(ForceInit), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
				NiagaraMuzzleNC->SetNiagaraVariableBool(NiagaraMuzzleTriggerParam.ToString(), true);
			}
		}
	}

	if (NiagaraShellFX.LoadSynchronous() && ShellMesh.LoadSynchronous())
	{
		USkeletalMeshComponent* UseWeaponMesh = GetWeaponMesh();
		if (!bLoopedShellFX || NiagaraShellNC == nullptr)
		{
			// Split screen requires we create 2 effects. One that we see and one that the other player sees.
			if( (MyPawn != nullptr ) && ( MyPawn->IsLocallyControlled() == true ) )
			{
				AController* PlayerCon = MyPawn->GetController();				
				if( PlayerCon != nullptr )
				{
					NiagaraShellNC = UNiagaraFunctionLibrary::SpawnSystemAttached(NiagaraShellFX.Get(), Mesh1P, NAME_None, Mesh1P->GetSocketTransform(ShellEjectSocketName, RTS_Actor).GetLocation(), FRotator(0.f, 90.f, 0.f), EAttachLocation::KeepRelativeOffset, true);
					NiagaraShellNC->bOwnerNoSee = false;
					NiagaraShellNC->bOnlyOwnerSee = true;
					NiagaraShellNC->SetNiagaraVariableBool(NiagaraShellTriggerParam.ToString(), true);
					NiagaraShellNC->SetVariableStaticMesh(NiagaraShellMeshParam, ShellMesh.Get());

					NiagaraShellNCSecondary = UNiagaraFunctionLibrary::SpawnSystemAttached(NiagaraShellFX.Get(), Mesh3P, NAME_None, Mesh3P->GetSocketTransform(ShellEjectSocketName, RTS_Actor).GetLocation(), FRotator(0.f, 90.f, 0.f), EAttachLocation::KeepRelativeOffset, true);
					NiagaraShellNCSecondary->bOwnerNoSee = true;
					NiagaraShellNCSecondary->bOnlyOwnerSee = false;
					NiagaraShellNCSecondary->SetNiagaraVariableBool(NiagaraShellTriggerParam.ToString(), true);
					NiagaraShellNCSecondary->SetVariableStaticMesh(NiagaraShellMeshParam, ShellMesh.Get());
				}				
			}
			else
			{
				NiagaraShellNC = UNiagaraFunctionLibrary::SpawnSystemAttached(NiagaraShellFX.Get(), UseWeaponMesh, NAME_None, UseWeaponMesh->GetSocketTransform(ShellEjectSocketName, RTS_Actor).GetLocation(), FRotator(0.f, 90.f, 0.f), EAttachLocation::KeepRelativeOffset, true);
				NiagaraShellNC->SetNiagaraVariableBool(NiagaraShellTriggerParam.ToString(), true);
				NiagaraShellNC->SetVariableStaticMesh(NiagaraShellMeshParam, ShellMesh.Get());
			}
		}
	}
}

void ATrueFPSFireWeaponBase::StopSimulatingWeaponFire()
{
	Super::StopSimulatingWeaponFire();
	
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
}

void ATrueFPSFireWeaponBase::OnRep_Reload()
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

void ATrueFPSFireWeaponBase::ServerHandleFiring_Implementation()
{
	Super::ServerHandleFiring_Implementation();
	
	const bool bShouldUpdateAmmo = (CurrentAmmoInClip > 0 && CanFire());

	if (bShouldUpdateAmmo)
	{
		// update ammo
		UseAmmo();
	}
}

void ATrueFPSFireWeaponBase::HandleFiring()
{
	if ((CurrentAmmoInClip > 0 || HasInfiniteClip() || HasInfiniteAmmo()) && CanFire())
	{
		if (GetNetMode() != NM_DedicatedServer)
		{
			SimulateWeaponFire();
		}

		if (MyPawn && MyPawn->IsLocallyControlled())
		{
			if (FireWeaponConfig.ShotsByFiring > 1)
			{
				for (int32 i = 0; i < FireWeaponConfig.ShotsByFiring; i++)
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

void ATrueFPSFireWeaponBase::DetermineWeaponState()
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

FVector ATrueFPSFireWeaponBase::GetAdjustedAim() const
{
	// If it's close to wall, use muzzle's rotation
	if (IsCloseToWall()) return GetMuzzleDirection();

	return Super::GetAdjustedAim();
}


FVector ATrueFPSFireWeaponBase::GetMuzzleLocation() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketLocation(MuzzleAttachPoint);
}

FVector ATrueFPSFireWeaponBase::GetMuzzleDirection() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketRotation(MuzzleAttachPoint).Vector();
}
