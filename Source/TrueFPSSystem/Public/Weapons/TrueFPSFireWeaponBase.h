// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TrueFPSWeaponBase.h"
#include "TrueFPSFireWeaponBase.generated.h"

USTRUCT(DisplayName = "Fire Weapon")
struct FFireWeaponData
{
	GENERATED_USTRUCT_BODY()

	/** inifite ammo for reloads */
	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	bool bInfiniteAmmo;

	/** infinite ammo in clip, no reload required */
	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	bool bInfiniteClip;

	/** max ammo */
	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 MaxAmmo;

	/** clip size */
	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 AmmoPerClip;

	/** initial clips */
	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 InitialClips;

	/** failsafe reload duration if weapon doesn't have any animation for it */
	UPROPERTY(EditDefaultsOnly, Category=WeaponStat)
	float NoAnimReloadDuration;

	/** shots made for each fire. Useful for shotguns */
	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 ShotsByFiring = 1;

	/** defaults */
	FFireWeaponData()
	{
		bInfiniteAmmo = false;
		bInfiniteClip = false;
		MaxAmmo = 100;
		AmmoPerClip = 20;
		InitialClips = 4;
		NoAnimReloadDuration = 1.0f;
	}
};

UCLASS(Abstract, Blueprintable)
class TRUEFPSSYSTEM_API ATrueFPSFireWeaponBase : public ATrueFPSWeaponBase
{
	GENERATED_BODY()

public:

	ATrueFPSFireWeaponBase(const FObjectInitializer& ObjectInitializer);
	
	/** perform initial setup */
	virtual void PostInitializeComponents() override;

	//////////////////////////////////////////////////////////////////////////
	// Reading Data
	
	/** get current ammo amount (total) */
	int32 GetCurrentAmmo() const;

	/** get current ammo amount (clip) */
	int32 GetCurrentAmmoInClip() const;

	/** get clip size */
	int32 GetAmmoPerClip() const;

	/** get max ammo amount */
	int32 GetMaxAmmo() const;
	
	/** check if weapon has infinite ammo (include owner's cheats) */
	bool HasInfiniteAmmo() const;

	/** check if weapon has infinite clip (include owner's cheats) */
	bool HasInfiniteClip() const;

	//////////////////////////////////////////////////////////////////////////
	// Ammo
	
	/** [server] add ammo */
	void GiveAmmo(int AddAmount);

	/** consume a bullet */
	void UseAmmo();

	//////////////////////////////////////////////////////////////////////////
	// Inventory

	virtual void OnUnEquip() override;
	
	virtual void OnEquipFinished() override;

	//////////////////////////////////////////////////////////////////////////
	// Input
	
	/** [all] start weapon reload */
	virtual void StartReload(bool bFromReplication = false);

	/** [local + server] interrupt weapon reload */
	virtual void StopReload();

	/** [server] performs actual reload */
	virtual void ReloadWeapon();

	/** trigger reload from server */
	UFUNCTION(reliable, client)
	void ClientStartReload();

	//////////////////////////////////////////////////////////////////////////
	// Control

	virtual bool CanFire() const override;
	
	/** check if weapon can be reloaded */
	virtual bool CanReload() const;

protected:

	/** weapon config */
	UPROPERTY(EditDefaultsOnly, Category="Config")
	FFireWeaponData FireWeaponConfig;

	/** name of bone/socket for muzzle in weapon mesh */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Muzzle")
	FName MuzzleAttachPoint;

	/** FX for muzzle flash */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Muzzle")
	UParticleSystem* MuzzleFX;

	/** spawned component for muzzle FX */
	UPROPERTY(Transient)
	UParticleSystemComponent* MuzzlePSC;

	/** spawned component for second muzzle FX (Needed for split screen) */
	UPROPERTY(Transient)
	UParticleSystemComponent* MuzzlePSCSecondary;

	/** niagara FX for muzzle flash */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Muzzle")
	TSoftObjectPtr<UNiagaraSystem> NiagaraMuzzleFX;

	/** niagara param name for muzzle flash trigger */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Muzzle")
	FName NiagaraMuzzleTriggerParam;

	/** niagara spawned component for muzzle FX */
	UPROPERTY(Transient)
	class UNiagaraComponent* NiagaraMuzzleNC;

	/** niagara spawned component for second muzzle FX (Needed for split screen) */
	UPROPERTY(Transient)
	UNiagaraComponent* NiagaraMuzzleNCSecondary;

	/** niagara FX for shell eject */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Shell")
	TSoftObjectPtr<UNiagaraSystem> NiagaraShellFX;

	/** shell eject mesh */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Shell")
	TSoftObjectPtr<UStaticMesh> ShellMesh;

	/** shell eject socket name */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Shell")
	FName ShellEjectSocketName;

	/** niagara param name for shell eject trigger */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Shell")
	FName NiagaraShellTriggerParam;

	/** niagara param name for shell eject mesh */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Shell")
	FName NiagaraShellMeshParam;

	/** niagara spawned component for shell FX */
	UPROPERTY(Transient)
	class UNiagaraComponent* NiagaraShellNC;

	/** niagara spawned component for second shell FX (Needed for split screen) */
	UPROPERTY(Transient)
	UNiagaraComponent* NiagaraShellNCSecondary;

	/** reload sound */
	UPROPERTY(EditDefaultsOnly, Category="Config|Sound")
	USoundBase* ReloadSound;

	/** reload animations */
	UPROPERTY(EditDefaultsOnly, Category="Config|Animation")
	FWeaponAnim ReloadAnim;

	/** is reload animation playing? */
	UPROPERTY(Transient, ReplicatedUsing=OnRep_Reload)
	uint32 bPendingReload : 1;

	/** is muzzle FX looped? */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Muzzle")
	uint32 bLoopedMuzzleFX : 1;

	/** is muzzle FX looped? */
	UPROPERTY(EditDefaultsOnly, Category="Config|Effects|Shell")
	uint32 bLoopedShellFX : 1;

	/** Handle for efficient management of StopReload timer */
	FTimerHandle TimerHandle_StopReload;

	/** Handle for efficient management of ReloadWeapon timer */
	FTimerHandle TimerHandle_ReloadWeapon;

public:
	
	/** bullet icon used to draw current clip (left side) */
	UPROPERTY(EditDefaultsOnly, Category="Config|HUD")
	FCanvasIcon PrimaryClipIcon;

	/** bullet icon used to draw secondary clip (left side) */
	UPROPERTY(EditDefaultsOnly, Category="Config|HUD")
	FCanvasIcon SecondaryClipIcon;

	/** how many icons to draw per clip */
	UPROPERTY(EditDefaultsOnly, Category="Config|HUD")
	float AmmoIconsCount;

	/** defines spacing between primary ammo icons (left side) */
	UPROPERTY(EditDefaultsOnly, Category="Config|HUD")
	int32 PrimaryClipIconOffset;

	/** defines spacing between secondary ammo icons (left side) */
	UPROPERTY(EditDefaultsOnly, Category="Config|HUD")
	int32 SecondaryClipIconOffset;

protected:

	//////////////////////////////////////////////////////////////////////////
	// Ammo

	/** current total ammo */
	UPROPERTY(Transient, Replicated)
	int32 CurrentAmmo;

	/** current ammo - inside clip */
	UPROPERTY(Transient, Replicated)
	int32 CurrentAmmoInClip;

	//////////////////////////////////////////////////////////////////////////
	// Input - Server Side
	
	UFUNCTION(reliable, server)
	void ServerStartReload();

	UFUNCTION(reliable, server)
	void ServerStopReload();

	//////////////////////////////////////////////////////////////////////////
	// Replication & Effects

	virtual void SimulateWeaponFire() override;

	virtual void StopSimulatingWeaponFire() override;
	
	UFUNCTION()
	void OnRep_Reload();

	//////////////////////////////////////////////////////////////////////////
	// Weapon Usage

	virtual void ServerHandleFiring_Implementation() override;

	virtual void HandleFiring() override;

	virtual void DetermineWeaponState() override;

	virtual FVector GetAdjustedAim() const override;

	/** get the muzzle location of the weapon */
	FVector GetMuzzleLocation() const;

	/** get direction of weapon's muzzle */
	FVector GetMuzzleDirection() const;
	
};
