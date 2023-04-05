// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TrueFPSTypes.h"
#include "InputActionValue.h"
#include "TrueFPSCharacterInterface.h"
#include "Components/TimelineComponent.h"
#include "TrueFPSCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class USceneComponent;
class UCameraComponent;
class UAnimMontage;
class USoundBase;
class USoundCue;

UCLASS()
class TRUEFPSSYSTEM_API ATrueFPSCharacter : public ACharacter, public ITrueFPSCharacterInterface
{
	GENERATED_BODY()

	UPROPERTY(VisibleDefaultsOnly, Category = Mesh)
	USkeletalMeshComponent* ClientMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* Camera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	class UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	class UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	class UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	class UInputAction* RunAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* AttackAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* AimingAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* NextWeaponAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* PreviousWeaponAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* ReloadAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LeanLeftAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LeanRightAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* CrouchAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* ToggleSightsAction;

public:
	
	ATrueFPSCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginDestroy() override;

	/** spawn inventory, setup initial variables */
	virtual void PostInitializeComponents() override;

	/** Update the character. (Running, health etc). */
	virtual void Tick(float DeltaSeconds) override;

	/** cleanup inventory */
	virtual void Destroyed() override;

	/** update mesh for first person view */
	virtual void PawnClientRestart() override;

	/** [server] perform PlayerState related setup */
	virtual void PossessedBy(class AController* C) override;

	/** [client] perform PlayerState related setup */
	virtual void OnRep_PlayerState() override;

	/** [server] called to determine if we should pause replication this actor to a specific player */
	virtual bool IsReplicationPausedForConnection(const FNetViewer& ConnectionOwnerNetViewer) override;

	/** [client] called when replication is paused for this actor */
	virtual void OnReplicationPausedChanged(bool bIsReplicationPaused) override;
	
	FORCEINLINE USkeletalMeshComponent* GetClientMesh() const { return ClientMesh; }
	FORCEINLINE UCameraComponent* GetCamera() const { return Camera; }
	
protected:
	
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	//////////////////////////////////////////////////////////////////////////
	// Locomotion

protected:
	
	void Move(const FInputActionValue& Value);

	void LookUpDown(const float Value);
	void TurnRightLeft(const float Value);

public:

	// The last updated pitch input value
	UPROPERTY(BlueprintReadOnly, Category = "Character|Locomotion")
	float CurrentPitchInputValue = 0.f;

	// The last updated yaw input value
	UPROPERTY(BlueprintReadOnly, Category = "Character|Locomotion")
	float CurrentYawInputValue = 0.f;

	// The last updated roll input value
	UPROPERTY(BlueprintReadOnly, Category = "Character|Locomotion")
	float CurrentRollInputValue = 0.f;
	
	virtual void AddControllerPitchInput(float Val) override
	{
		Super::AddControllerPitchInput(Val);
		CurrentPitchInputValue = Val;
	}
	virtual void AddControllerYawInput(float Val) override
	{
		Super::AddControllerYawInput(Val);
		CurrentYawInputValue = Val;
	}
	virtual void AddControllerRollInput(float Val) override
	{
		Super::AddControllerRollInput(Val);
		CurrentRollInputValue = Val;
	}

	//////////////////////////////////////////////////////////////////////////
	// Inventory

public:

	/**
	* [server] add weapon to inventory
	*
	* @param Weapon	Weapon to add.
	*/
	void AddWeapon(class ATrueFPSWeaponBase* Weapon);

	/**
	* [server] remove weapon from inventory
	*
	* @param Weapon	Weapon to remove.
	*/
	void RemoveWeapon(class ATrueFPSWeaponBase* Weapon);

	/**
	* Find in inventory
	*
	* @param WeaponClass	Class of weapon to find.
	*/
	class ATrueFPSWeaponBase* FindWeapon(TSubclassOf<class ATrueFPSWeaponBase> WeaponClass);

	/**
	* [server + local] equips weapon from inventory
	*
	* @param Weapon	Weapon to equip
	*/
	void EquipWeapon(class ATrueFPSWeaponBase* Weapon);

	//////////////////////////////////////////////////////////////////////////
	// Weapon usage

	/** [local] starts weapon fire */
	void StartWeaponFire();

	/** [local] stops weapon fire */
	void StopWeaponFire();

	/** check if pawn can fire weapon */
	bool CanFire() const;

	virtual bool CanFire_Implementation() override { return CanFire(); }

	/** check if pawn can reload weapon */
	bool CanReload() const;

	virtual bool CanReload_Implementation() override { return CanReload(); }

	/** [server + local] change aiming state */
	void SetAiming(bool bNewAiming);

	//////////////////////////////////////////////////////////////////////////
	// Movement

	/** [server + local] change running state */
	void SetRunning(bool bNewRunning, bool bToggle);

	//////////////////////////////////////////////////////////////////////////
	// Animations

	/** play anim montage */
	virtual float PlayAnimMontage(class UAnimMontage* AnimMontage, float InPlayRate = 1.f, FName StartSectionName = NAME_None) override;

	/** stop playing montage */
	virtual void StopAnimMontage(class UAnimMontage* AnimMontage) override;

	/** stop playing all montages */
	void StopAllAnimMontages();

	//////////////////////////////////////////////////////////////////////////
	// Input handlers

	/** player pressed start fire action */
	void OnStartFire();

	/** player released start fire action */
	void OnStopFire();

	/** player pressed aiming action */
	void OnStartAiming();

	/** player released aiming action */
	void OnStopAiming();

	/** player pressed next weapon action */
	void OnNextWeapon();

	/** player pressed prev weapon action */
	void OnPrevWeapon();

	/** player pressed reload action */
	void OnReload();

	/** player pressed jump action */
	void OnStartJump();

	/** player released jump action */
	void OnStopJump();

	/** player pressed run action */
	void OnStartRunning();

	/** player released run action */
	void OnStopRunning();

	/** player pressed left lean action */
	void OnLeanLeft();

	/** player pressed right lean action */
	void OnLeanRight();

	/** player released lean action */
	void OnStopLean();

	/** player pressed crouch action */
	void OnStartCrouching();

	/** player released crouch action */
	void OnStopCrouching();

	/** player toggled sights */
	void OnToggleSights();

	//////////////////////////////////////////////////////////////////////////
	// Reading data

	/** get mesh component */
	USkeletalMeshComponent* GetPawnMesh() const;

	/** get currently equipped weapon */
	UFUNCTION(BlueprintCallable, Category = "Game|Weapon")
	class ATrueFPSWeaponBase* GetWeapon() const;

	/** get weapon attach point */
	FName GetWeaponAttachPoint() const;

	virtual FName GetWeaponAttachPoint_Implementation() override { return GetWeaponAttachPoint(); }

	/** get total number of inventory items */
	int32 GetInventoryCount() const;

	/**
	* get weapon from inventory at index. Index validity is not checked.
	*
	* @param Index Inventory index
	*/
	class ATrueFPSWeaponBase* GetInventoryWeapon(int32 index) const;

	/** get weapon taget modifier speed	*/
	UFUNCTION(BlueprintCallable, Category = "Game|Weapon")
	float GetAimingSpeedModifier() const;

	/** get aiming state */
	UFUNCTION(BlueprintCallable, Category = "Game|Weapon")
	bool IsPlayerAiming() const;

	virtual bool IsAiming_Implementation() override { return IsPlayerAiming(); }

	/** get firing state */
	UFUNCTION(BlueprintCallable, Category = "Game|Weapon")
	bool IsFiring() const;

	/** get the modifier value for running speed */
	UFUNCTION(BlueprintCallable, Category = Pawn)
	float GetRunningSpeedModifier() const;

	/** get running state */
	UFUNCTION(BlueprintCallable, Category = Pawn)
	bool IsPlayerRunning() const;

	virtual bool IsRunning_Implementation() override { return IsPlayerRunning(); }

	/** get camera view type */
	UFUNCTION(BlueprintCallable, Category = Mesh)
	virtual bool IsFirstPersonPlayer() const;

	virtual bool IsFirstPerson_Implementation() override { return IsFirstPersonPlayer(); }

	virtual USkeletalMeshComponent* GetSpecificPawnMesh_Implementation(bool WantFirstPerson) const override { return WantFirstPerson ? ClientMesh : GetMesh(); }

	/** get max health */
	int32 GetMaxHealth() const;

	/** check if pawn is still alive */
	bool IsAlive() const;

	/** returns percentage of health when low health effects should start */
	float GetLowHealthPercentage() const;

	virtual FRotator GetLastUpdatedRotationValues_Implementation() override { return FRotator(CurrentPitchInputValue, CurrentYawInputValue, CurrentRollInputValue); }

	virtual FTransform GetDomHandTransform_Implementation() override { return GetMesh()->GetSocketTransform(bRightHanded ? RightHandSocket : LeftHandSocket); }

	virtual float GetADSValue_Implementation() override { return ADSValue; }

protected:

	/** default inventory list */
	UPROPERTY(EditDefaultsOnly, Category = Inventory)
	TArray<TSubclassOf<class ATrueFPSWeaponBase> > DefaultInventoryClasses;

	/** weapons in inventory */
	UPROPERTY(Transient, Replicated)
	TArray<class ATrueFPSWeaponBase*> Inventory;

	/** currently equipped weapon */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_CurrentWeapon)
	class ATrueFPSWeaponBase* CurrentWeapon;

	/** Replicate where this pawn was last hit and damaged */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_LastTakeHitInfo)
	FTakeHitInfo LastTakeHitInfo;

	/** Time at which point the last take hit info for the actor times out and won't be replicated; Used to stop join-in-progress effects all over the screen */
	float LastTakeHitTimeTimeout;

	/** modifier for max movement speed */
	UPROPERTY(EditDefaultsOnly, Category = Inventory)
	float AimingSpeedModifier;

	/** current aiming state */
	UPROPERTY(Transient, Replicated)
	uint8 bIsAiming : 1;

	/** modifier for max movement speed */
	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	float RunningSpeedModifier;

	/** current running state */
	UPROPERTY(Transient, Replicated)
	uint8 bWantsToRun : 1;

	/** current aiming state alpha */
	UPROPERTY(Transient, Replicated)
	float ADSValue = 0.f;

	/** current firing state */
	uint8 bWantsToFire : 1;

	/** when low health effects should start */
	float LowHealthPercentage;

	/** animation played on death */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	UAnimMontage* DeathAnim;

	/** sound played when aiming state changes */
	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	USoundCue* AimingSound;

	/** sound played on death, local player only */
	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	USoundCue* DeathSound;

	/** socket or bone name for attaching weapon mesh */
	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	FName WeaponAttachPoint;

	/** right hand socket name */
	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	FName RightHandSocket = FName("hand_r");

	/** left hand socket name */
	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	FName LeftHandSocket = FName("hand_l");

	/** dominant hand */
	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	bool bRightHanded = true;

	UPROPERTY(EditDefaultsOnly, Category = Pawn)
	float LeanAmount = 45.f;
	
	UPROPERTY(BlueprintReadWrite)
	float LeanValue = 0.f;
	
	UPROPERTY(BlueprintReadWrite, Replicated)
	float LeanValueTarget = 0.f;

	void ChangeLeanValue(float NewLeanValue);

	UFUNCTION(Server, Reliable)
	void ServerChangeLeanValue(float NewLeanValue);
	
	UPROPERTY(BlueprintReadWrite)
	float CrouchValue = 0.f;
	
	UPROPERTY(BlueprintReadWrite, Replicated)
	float CrouchValueTarget = 0.f;

	void ToggleCrouching(bool bNewCrouching);

	UFUNCTION(Server, Reliable)
	void ServerToggleCrouching(bool bNewCrouching);

public:
	
	virtual float GetCrouchValue_Implementation() const override { return CrouchValue; }

protected:
	/** Responsible for cleaning up bodies on clients. */
	virtual void TornOff();

private:

	/** Whether or not the character is moving (based on movement input). */
	bool IsMoving();

	/** Used for handling AimingCurve */
	FTimeline AimingTimeline;

	/** AimingTimeline progress function */
	virtual void AimingTimelineProgress(const float Value);

protected:

	/** AimingTimeline complete  function */
	UFUNCTION(BlueprintNativeEvent)
	void AimingTimelineComplete();

	//////////////////////////////////////////////////////////////////////////
	// Damage & death

public:

	/** Identifies if pawn is in its dying state */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Health)
	uint32 bIsDying : 1;

	// Current health of the Pawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = Health)
	float Health;

	/** Take damage, handle death */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, class AActor* DamageCauser) override;

	/** Pawn suicide */
	virtual void Suicide();

	/** Kill this pawn */
	virtual void KilledBy(class APawn* EventInstigator);

	/** Returns True if the pawn can die in the current state */
	virtual bool CanDie(float KillingDamage, FDamageEvent const& DamageEvent, AController* Killer, AActor* DamageCauser) const;

	/**
	* Kills pawn.  Server/authority only.
	* @param KillingDamage - Damage amount of the killing blow
	* @param DamageEvent - Damage event of the killing blow
	* @param Killer - Who killed this pawn
	* @param DamageCauser - the Actor that directly caused the damage (i.e. the Projectile that exploded, the Weapon that fired, etc)
	* @returns true if allowed
	*/
	virtual bool Die(float KillingDamage, struct FDamageEvent const& DamageEvent, class AController* Killer, class AActor* DamageCauser);

	// Die when we fall out of the world.
	virtual void FellOutOfWorld(const class UDamageType& dmgType) override;

	/** Called on the actor right before replication occurs */
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	
protected:
	
	/** notification when killed, for both the server and client. */
	virtual void OnDeath(float KillingDamage, struct FDamageEvent const& DamageEvent, class APawn* InstigatingPawn, class AActor* DamageCauser);

	/** play effects on hit */
	virtual void PlayHit(float DamageTaken, struct FDamageEvent const& DamageEvent, class APawn* PawnInstigator, class AActor* DamageCauser);

	/** switch to ragdoll */
	void SetRagdollPhysics();

	/** sets up the replication for taking a hit */
	void ReplicateHit(float Damage, struct FDamageEvent const& DamageEvent, class APawn* InstigatingPawn, class AActor* DamageCauser, bool bKilled);

	/** play hit or death on client */
	UFUNCTION()
	void OnRep_LastTakeHitInfo();

	//////////////////////////////////////////////////////////////////////////
	// Inventory

	/** updates current weapon */
	void SetCurrentWeapon(class ATrueFPSWeaponBase* NewWeapon, class ATrueFPSWeaponBase* LastWeapon = NULL);

	/** current weapon rep handler */
	UFUNCTION()
	void OnRep_CurrentWeapon(class ATrueFPSWeaponBase* LastWeapon);

	/** [server] spawns default inventory */
	void SpawnDefaultInventory();

	/** [server] remove all weapons from inventory and destroy them */
	void DestroyInventory();

	/** equip weapon */
	UFUNCTION(reliable, server)
	void ServerEquipWeapon(class ATrueFPSWeaponBase* NewWeapon);

	/** update aiming state */
	UFUNCTION(reliable, server)
	void ServerSetAiming(bool bNewAiming);

	/** update targeting state */
	UFUNCTION(reliable, server)
	void ServerSetRunning(bool bNewRunning, bool bToggle);

	/** Builds list of points to check for pausing replication for a connection*/
	void BuildPauseReplicationCheckPoints(TArray<FVector>& RelevancyCheckPoints);

	virtual ATrueFPSWeaponBase* GetCurrentWeapon_Implementation() override { return CurrentWeapon; }

	virtual bool HasCurrentWeapon_Implementation() override { return CurrentWeapon ? true : false; }
	
};
