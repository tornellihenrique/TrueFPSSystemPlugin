// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Canvas.h" // for FCanvasIcon
#include "TrueFPSTypes.h"
#include "TrueFPSWeaponBase.generated.h"

class UAnimMontage;
class ACharacter;
class UAudioComponent;
class UParticleSystemComponent;
class UForceFeedbackEffect;
class USoundCue;
class UCameraShakeBase;

namespace EWeaponState
{
	enum Type
	{
		Idle,
		Firing,
		Reloading,
		Equipping,
	};
}

USTRUCT()
struct FWeaponData
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

	/** time between two consecutive shots */
	UPROPERTY(EditDefaultsOnly, Category=WeaponStat)
	float TimeBetweenShots;

	/** failsafe reload duration if weapon doesn't have any animation for it */
	UPROPERTY(EditDefaultsOnly, Category=WeaponStat)
	float NoAnimReloadDuration;

	/** shots made for each fire. Useful for shotguns */
	UPROPERTY(EditDefaultsOnly, Category=Ammo)
	int32 ShotsByFiring = 1;

	/** defaults */
	FWeaponData()
	{
		bInfiniteAmmo = false;
		bInfiniteClip = false;
		MaxAmmo = 100;
		AmmoPerClip = 20;
		InitialClips = 4;
		TimeBetweenShots = 0.2f;
		NoAnimReloadDuration = 1.0f;
	}
};

USTRUCT()
struct FWeaponAnim
{
	GENERATED_USTRUCT_BODY()

	/** animation played on pawn (1st person view) */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	UAnimMontage* Pawn1P = nullptr;

	/** animation played on pawn (3rd person view) */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	UAnimMontage* Pawn3P = nullptr;

	/** animation played on weapon (1st person view) */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	UAnimMontage* Weapon1P = nullptr;

	/** animation played on weapon (3rd person view) */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	UAnimMontage* Weapon3P = nullptr;

	FWeaponAnim() {}
};

USTRUCT()
struct FWeaponAnimProperties
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	float WeightScale = 1.f;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FTransform CustomOffsetTransform;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FTransform OriginRelativeTransform;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FTransform PlacementTransform;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FTransform WallOffsetTransform = FTransform(FRotator(100.f, 0.f, 0.f), FVector(-10.f, 0.f, 0.f));

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	float WallOffsetTransformMaxDistance = 80.f;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	float WallOffsetTransformMinDistance = 30.f;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	float AimOffset;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FName SightSocketName = FName("sights");

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	class UCurveFloat* AimingCurve;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FRecoilParams RecoilParams;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	EAnimState AnimState = EAnimState::Default;

	UPROPERTY(EditDefaultsOnly, Category=Animation)
	class UAnimSequence* AnimPose = nullptr;
};

UCLASS(Abstract, Blueprintable)
class TRUEFPSSYSTEM_API ATrueFPSWeaponBase : public AActor
{
	GENERATED_BODY()
	
public:

	ATrueFPSWeaponBase(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;
	
	/** perform initial setup */
	virtual void PostInitializeComponents() override;

	virtual void Destroyed() override;

	//////////////////////////////////////////////////////////////////////////
	// Ammo
	
	enum class EAmmoType
	{
		EBullet,
		ERocket,
		EMax,
	};

	/** [server] add ammo */
	void GiveAmmo(int AddAmount);

	/** consume a bullet */
	void UseAmmo();

	/** query ammo type */
	virtual EAmmoType GetAmmoType() const
	{
		return EAmmoType::EBullet;
	}

	//////////////////////////////////////////////////////////////////////////
	// Inventory

	/** weapon is being equipped by owner pawn */
	virtual void OnEquip(const ATrueFPSWeaponBase* LastWeapon);

	/** weapon is now equipped by owner pawn */
	virtual void OnEquipFinished();

	/** weapon is holstered by owner pawn */
	virtual void OnUnEquip();

	/** [server] weapon was added to pawn's inventory */
	virtual void OnEnterInventory(ACharacter* NewOwner);

	/** [server] weapon was removed from pawn's inventory */
	virtual void OnLeaveInventory();

	/** check if it's currently equipped */
	bool IsEquipped() const;

	/** check if mesh is already attached */
	bool IsAttachedToPawn() const;


	//////////////////////////////////////////////////////////////////////////
	// Input

	/** [local + server] start weapon fire */
	virtual void StartFire();

	/** [local + server] stop weapon fire */
	virtual void StopFire();

	/** [all] start weapon reload */
	virtual void StartReload(bool bFromReplication = false);

	/** [local + server] interrupt weapon reload */
	virtual void StopReload();

	/** [server] performs actual reload */
	virtual void ReloadWeapon();

	/** trigger reload from server */
	UFUNCTION(reliable, client)
	void ClientStartReload();

	/** [server] toggle between sights */
	virtual void ToggleSights();


	//////////////////////////////////////////////////////////////////////////
	// Control

	/** check if weapon can fire */
	bool CanFire() const;

	/** check if weapon can be reloaded */
	bool CanReload() const;

	/** check if weapon can be reloaded */
	bool CanAim() const;


	//////////////////////////////////////////////////////////////////////////
	// Reading data

	/** get current weapon state */
	EWeaponState::Type GetCurrentState() const;

	/** get current ammo amount (total) */
	int32 GetCurrentAmmo() const;

	/** get current ammo amount (clip) */
	int32 GetCurrentAmmoInClip() const;

	/** get clip size */
	int32 GetAmmoPerClip() const;

	/** get max ammo amount */
	int32 GetMaxAmmo() const;

	/** get weapon mesh (needs pawn owner to determine variant) */
	USkeletalMeshComponent* GetWeaponMesh() const;

	/** get pawn owner */
	UFUNCTION(BlueprintCallable, Category="Game|Weapon")
	class ACharacter* GetPawnOwner() const
	{
		return MyPawn;
	};

	/** get casted pawn owner */
	template<typename T>
	T* GetPawnOwner() const
	{
		return Cast<T>(MyPawn);
	}

	/** get if is first person using interface */
	bool IsFirstPerson() const;

	/** icon displayed on the HUD when weapon is equipped as primary */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon PrimaryIcon;

	/** icon displayed on the HUD when weapon is secondary */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon SecondaryIcon;

	/** bullet icon used to draw current clip (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon PrimaryClipIcon;

	/** bullet icon used to draw secondary clip (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon SecondaryClipIcon;

	/** how many icons to draw per clip */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	float AmmoIconsCount;

	/** defines spacing between primary ammo icons (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	int32 PrimaryClipIconOffset;

	/** defines spacing between secondary ammo icons (left side) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	int32 SecondaryClipIconOffset;

	/** crosshair parts icons (left, top, right, bottom and center) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon Crosshair[5];

	/** crosshair parts icons when targeting (left, top, right, bottom and center) */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	FCanvasIcon AimingCrosshair[5];

	/** only use red colored center part of aiming crosshair */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	bool UseLaserDot;

	/** false = default crosshair */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	bool UseCustomCrosshair;

	/** false = use custom one if set, otherwise default crosshair */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	bool UseCustomAimingCrosshair;

	/** true - crosshair will not be shown unless aiming with the weapon */
	UPROPERTY(EditDefaultsOnly, Category=HUD)
	bool bHideCrosshairWhileNotAiming;

	/** Adjustment to handle frame rate affecting actual timer interval. */
	UPROPERTY(Transient)
	float TimerIntervalAdjustment;

	/** Whether to allow automatic weapons to catch up with shorter refire cycles */
	UPROPERTY(Config)
	bool bAllowAutomaticWeaponCatchup = true;

	/** check if weapon has infinite ammo (include owner's cheats) */
	bool HasInfiniteAmmo() const;

	/** check if weapon has infinite clip (include owner's cheats) */
	bool HasInfiniteClip() const;

	/** set the weapon's owning pawn */
	void SetOwningPawn(ACharacter* ACharacter);

	/** gets last time when this weapon was switched to */
	float GetEquipStartedTime() const;

	/** gets the duration of equipping weapon*/
	float GetEquipDuration() const;

	void GetAttachments(TArray<class ATrueFPSWeaponAttachmentBase*>& OutAttachments) const;

	template<typename T>
	void GetAttachments(TArray<T*>& OutAttachments) const;

	void GetAttachmentsOfClass(TArray<class ATrueFPSWeaponAttachmentBase*>& OutAttachments, const TSubclassOf<class ATrueFPSWeaponAttachmentBase>& Class) const;

	void GetAttachmentPoints(TArray<class UTrueFPSWeaponAttachmentPoint*>& OutAttachmentPoints) const;
	
	FORCEINLINE bool IsCloseToWall() const { return GetWallOffsetTransformAlpha() > 0.f; }

protected:

	/** pawn owner */
	UPROPERTY(Transient, ReplicatedUsing=OnRep_MyPawn)
	class ACharacter* MyPawn;

	/** weapon data */
	UPROPERTY(EditDefaultsOnly, Category=Config)
	FWeaponData WeaponConfig;

	/** weapon anim properties */
	UPROPERTY(EditDefaultsOnly, Category=Config)
	FWeaponAnimProperties WeaponAnimProperties;

private:
	
	/** weapon mesh: 1st person view */
	UPROPERTY(VisibleDefaultsOnly, Category=Mesh)
	USkeletalMeshComponent* Mesh1P;

	/** weapon mesh: 3rd person view */
	UPROPERTY(VisibleDefaultsOnly, Category=Mesh)
	USkeletalMeshComponent* Mesh3P;
	
protected:

	/** firing audio (bLoopedFireSound set) */
	UPROPERTY(Transient)
	UAudioComponent* FireAC;

	/** name of bone/socket for muzzle in weapon mesh */
	UPROPERTY(EditDefaultsOnly, Category=Effects)
	FName MuzzleAttachPoint;

	/** FX for muzzle flash */
	UPROPERTY(EditDefaultsOnly, Category=Effects)
	UParticleSystem* MuzzleFX;

	/** spawned component for muzzle FX */
	UPROPERTY(Transient)
	UParticleSystemComponent* MuzzlePSC;

	/** spawned component for second muzzle FX (Needed for split screen) */
	UPROPERTY(Transient)
	UParticleSystemComponent* MuzzlePSCSecondary;

	/** camera shake on firing */
	UPROPERTY(EditDefaultsOnly, Category=Effects)
	TSubclassOf<UCameraShakeBase> FireCameraShake;

	/** force feedback effect to play when the weapon is fired */
	UPROPERTY(EditDefaultsOnly, Category=Effects)
	UForceFeedbackEffect *FireForceFeedback;

	/** single fire sound (bLoopedFireSound not set) */
	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* FireSound;

	/** looped fire sound (bLoopedFireSound set) */
	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* FireLoopSound;

	/** finished burst sound (bLoopedFireSound set) */
	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* FireFinishSound;

	/** out of ammo sound */
	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* OutOfAmmoSound;

	/** reload sound */
	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* ReloadSound;

	/** reload animations */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FWeaponAnim ReloadAnim;

	/** equip sound */
	UPROPERTY(EditDefaultsOnly, Category=Sound)
	USoundCue* EquipSound;

	/** equip animations */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FWeaponAnim EquipAnim;

	/** fire animations */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	FWeaponAnim FireAnim;

	/** is muzzle FX looped? */
	UPROPERTY(EditDefaultsOnly, Category=Effects)
	uint32 bLoopedMuzzleFX : 1;

	/** is fire sound looped? */
	UPROPERTY(EditDefaultsOnly, Category=Sound)
	uint32 bLoopedFireSound : 1;

	/** is fire animation looped? */
	UPROPERTY(EditDefaultsOnly, Category=Animation)
	uint32 bLoopedFireAnim : 1;

	/** is fire animation playing? */
	uint32 bPlayingFireAnim : 1;

	/** is weapon currently equipped? */
	uint32 bIsEquipped : 1;

	/** is weapon fire active? */
	uint32 bWantsToFire : 1;

	/** is reload animation playing? */
	UPROPERTY(Transient, ReplicatedUsing=OnRep_Reload)
	uint32 bPendingReload : 1;

	/** is equip animation playing? */
	uint32 bPendingEquip : 1;

	/** weapon is refiring */
	uint32 bRefiring;

	/** current weapon state */
	EWeaponState::Type CurrentState;

	/** time of last successful weapon fire */
	float LastFireTime;

	/** last time when this weapon was switched to */
	float EquipStartedTime;

	/** how much time weapon needs to be equipped */
	float EquipDuration;

	/** current total ammo */
	UPROPERTY(Transient, Replicated)
	int32 CurrentAmmo;

	/** current ammo - inside clip */
	UPROPERTY(Transient, Replicated)
	int32 CurrentAmmoInClip;

	/** burst counter, used for replicating fire events to remote clients */
	UPROPERTY(Transient, ReplicatedUsing=OnRep_BurstCounter)
	int32 BurstCounter;

	/** target sights relative transform */
	UPROPERTY(Replicated)
	FTransform TargetSightsRelativeTransform;

	/** [server] current sights being used (nullptr for default)*/
	UPROPERTY()
	class ATrueFPSSightsAttachment* CurrentSights = nullptr;

	/** Handle for efficient management of OnEquipFinished timer */
	FTimerHandle TimerHandle_OnEquipFinished;

	/** Handle for efficient management of StopReload timer */
	FTimerHandle TimerHandle_StopReload;

	/** Handle for efficient management of ReloadWeapon timer */
	FTimerHandle TimerHandle_ReloadWeapon;

	/** Handle for efficient management of HandleFiring timer */
	FTimerHandle TimerHandle_HandleFiring;

private:
	
	TArray<FRecoilInstance> RecoilInstances;

protected:

	//////////////////////////////////////////////////////////////////////////
	// Input - server side

	UFUNCTION(reliable, server)
	void ServerStartFire();

	UFUNCTION(reliable, server)
	void ServerStopFire();

	UFUNCTION(reliable, server)
	void ServerStartReload();

	UFUNCTION(reliable, server)
	void ServerStopReload();

	UFUNCTION(reliable, server)
	void ServerToggleSights();


	//////////////////////////////////////////////////////////////////////////
	// Replication & effects

	UFUNCTION()
	void OnRep_MyPawn();

	UFUNCTION()
	void OnRep_BurstCounter();

	UFUNCTION()
	void OnRep_Reload();

	/** Called in network play to do the cosmetic fx for firing */
	virtual void SimulateWeaponFire();

	/** Called in network play to stop cosmetic fx (e.g. for a looping shot). */
	virtual void StopSimulatingWeaponFire();

	
	//////////////////////////////////////////////////////////////////////////
	// Weapon usage

	/** [local] weapon specific fire implementation */
	virtual void FireWeapon() PURE_VIRTUAL(ATrueFPSWeaponBase::FireWeapon,);

	/** [server] fire & update ammo */
	UFUNCTION(reliable, server)
	void ServerHandleFiring();

	/** [local + server] handle weapon refire, compensating for slack time if the timer can't sample fast enough */
	void HandleReFiring();

	/** [local + server] handle weapon fire */
	void HandleFiring();

	/** [local + server] firing started */
	virtual void OnBurstStarted();

	/** [local + server] firing finished */
	virtual void OnBurstFinished();

	/** update weapon state */
	void SetWeaponState(EWeaponState::Type NewState);

	/** determine current weapon state */
	void DetermineWeaponState();

	/** adds new recoil instance */
	FORCEINLINE void AddRecoilInstance(const FRecoilParams& RecoilParams)
	{
		if (!RecoilParams.IsValid()) return;
		RecoilInstances.Empty();
		RecoilInstances.Add(FRecoilInstance(RecoilParams));
	}

	/** handles recoil instances */
	void HandleRecoil(float DeltaSeconds);


	//////////////////////////////////////////////////////////////////////////
	// Inventory

	/** attaches weapon mesh to pawn's mesh */
	void AttachMeshToPawn();

	/** detaches weapon mesh from pawn */
	void DetachMeshFromPawn();


	//////////////////////////////////////////////////////////////////////////
	// Weapon usage helpers

	/** play weapon sounds */
	UAudioComponent* PlayWeaponSound(USoundCue* Sound);

	/** play weapon animations */
	float PlayWeaponAnimation(const FWeaponAnim& Animation);

	/** stop playing weapon animations */
	void StopWeaponAnimation(const FWeaponAnim& Animation);

	/** Get the aim of the weapon, allowing for adjustments to be made by the weapon */
	virtual FVector GetAdjustedAim() const;

	/** Get the aim of the camera */
	FVector GetCameraAim() const;

	/** get the originating location for camera damage */
	FVector GetCameraDamageStartLocation(const FVector& AimDir) const;

	/** get the muzzle location of the weapon */
	FVector GetMuzzleLocation() const;

	/** get direction of weapon's muzzle */
	FVector GetMuzzleDirection() const;

	/** find hit */
	FHitResult WeaponTrace(const FVector& TraceFrom, const FVector& TraceTo) const;

	//////////////////////////////////////////////////////////////////////////
	// Weapon anim helpers

public:

	// The total offset from the accumulated recoil instances
	FTransform RecoilOffset;

	// The offset applied to the Off-Hand
	FTransform OffHandAdditiveTransform;

	// The sights transform relative to the weapon actor transform
	FTransform SightsRelativeTransform;

	// Custom weapon offset transform
	FTransform OffsetTransform;

	FTransform GetOffsetTransform() const;

	FTransform GetDefaultSightsRelativeTransform() const;

	FTransform GetSightsWorldTransform() const;

	FTransform GetOriginRelativeTransform() const;

	FTransform GetOriginWorldTransform() const;
	
	FTransform GetOrientationRelativeTransform(const float AimingValue = 0.f) const;
	
	FTransform GetOrientationWorldTransform(const float AimingValue = 0.f) const;

	FWeaponAnimProperties GetAnimProperties() const { return WeaponAnimProperties; }

	float GetWallOffsetTransformAlpha() const;

	//////////////////////////////////////////////////////////////////////////
	// Debug

protected:

#if WITH_EDITORONLY_DATA
	class UStaticMeshComponent* OriginWidgetComp = nullptr;

	// Whether or not to show the visual-representation of the Origin Transform
	UPROPERTY(EditDefaultsOnly, Meta = (DisplayAfter = "OriginRelativeTransform"), Category = "Configurations|Anim|Weapon Origin Visualizer")
	bool bShowOriginWidget = false;

	// Setting this will override the default origin widget mesh
	UPROPERTY(EditDefaultsOnly, Meta = (DisplayAfter = "bShowOriginWidget", EditCondition = "bShowOriginWidget"), Category = "Configurations|Anim|Weapon Origin Visualizer")
	class UStaticMesh* OriginVisualizationMesh;
	
	virtual void RegisterAllComponents() override;
	
#endif// WITH_EDITORONLY_DATA

public:

#if WITH_EDITORONLY_DATA
	class UStaticMeshComponent* AimOffsetComp = nullptr;

	UPROPERTY(EditDefaultsOnly, Meta = (DisplayName = "Show Aim Offset Visualization Mesh"), Category = "Configurations|Anim|Aiming|Visualization")
	bool bShowAimOffset = false;
	
	UPROPERTY(EditDefaultsOnly, Meta = (DisplayName = "Aim Offset Visualization Mesh", EditCondition = bShowAimOffset), Category = "Configurations|Anim|Aiming|Visualization")
	class UStaticMesh* AimOffsetMesh;
#endif// WITH_EDITORONLY_DATA

protected:
	
	/** Returns Mesh1P subobject **/
	FORCEINLINE USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns Mesh3P subobject **/
	FORCEINLINE USkeletalMeshComponent* GetMesh3P() const { return Mesh3P; }

};
