// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/TrueFPSCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Character/Animation/TrueFPSAnimInstanceBase.h"
#include "TrueFPSSystem.h"
#include "Weapons/TrueFPSWeaponBase.h"
#include "Camera/CameraComponent.h"
#include "Character/TrueFPSCharacterMovement.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundCue.h"
#include "GameFramework/DamageType.h"
#include "Kismet/KismetMathLibrary.h"


ATrueFPSCharacter::ATrueFPSCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTrueFPSCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	GetMesh()->bReceivesDecals = false;
	GetMesh()->bVisibleInReflectionCaptures = true;
	GetMesh()->SetCastHiddenShadow(true);
	GetMesh()->SetCollisionObjectType(ECC_Pawn);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetCollisionResponseToChannel(COLLISION_WEAPON, ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(COLLISION_PROJECTILE, ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Create the ClientMesh
	ClientMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Client Mesh"));
	ClientMesh->SetupAttachment(GetMesh());
	ClientMesh->bCastDynamicShadow = false;
	ClientMesh->bReceivesDecals = false;
	ClientMesh->SetCollisionObjectType(ECC_Pawn);
	ClientMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ClientMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(COLLISION_PROJECTILE, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(COLLISION_WEAPON, ECR_Ignore);

	// Create a CameraComponent	
	// Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	// Camera->SetupAttachment(GetMesh(), FName("head"));
	// Camera->bUsePawnControlRotation = true;

	AimingSpeedModifier = 0.5f;
	bIsAiming = false;
	RunningSpeedModifier = 1.5f;
	bWantsToRun = false;
	bWantsToFire = false;
	LowHealthPercentage = 0.5f;
}

void ATrueFPSCharacter::BeginDestroy()
{
	Super::BeginDestroy();

	if (!GExitPurge)
	{
		const uint32 UniqueID = GetUniqueID();
		FAudioThread::RunCommandOnAudioThread([UniqueID]()
		{
			// TODO: Finish
			// USoundNodeLocalPlayer::GetLocallyControlledActorCache().Remove(UniqueID);
		});
	}
}

void ATrueFPSCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (GetLocalRole() == ROLE_Authority)
	{
		Health = GetMaxHealth();

		// Needs to happen after character is added to repgraph
		GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::SpawnDefaultInventory);
	}
}

void ATrueFPSCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	// Leaning
	const float OldLeanValue = LeanValue;
	LeanValue = UKismetMathLibrary::FInterpTo(LeanValue, LeanValueTarget, DeltaSeconds, 8.f);
	CurrentRollInputValue = LeanValue - OldLeanValue;
	AddControllerRollInput(CurrentRollInputValue * 0.2f);

	// Crouching
	CrouchValue = UKismetMathLibrary::FInterpTo(CrouchValue, CrouchValueTarget, DeltaSeconds, 8.f);

	// TODO: Implement Health Regen
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->HasHealthRegen())
	// {
	// 	if (this->Health < this->GetMaxHealth())
	// 	{
	// 		this->Health += 5 * DeltaSeconds;
	// 		if (Health > this->GetMaxHealth())
	// 		{
	// 			Health = this->GetMaxHealth();
	// 		}
	// 	}
	// }

	const APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bLocallyControlled = (PC ? PC->IsLocalController() : false);
	const uint32 UniqueID = GetUniqueID();
	FAudioThread::RunCommandOnAudioThread([UniqueID, bLocallyControlled]()
	{
		// TODO: Implement
	    // USoundNodeLocalPlayer::GetLocallyControlledActorCache().Add(UniqueID, bLocallyControlled);
	});
	
	TArray<FVector> PointsToTest;
	BuildPauseReplicationCheckPoints(PointsToTest);

	// if (NetVisualizeRelevancyTestPoints == 1)
	// {
	// 	for (FVector PointToTest : PointsToTest)
	// 	{
	// 		DrawDebugSphere(GetWorld(), PointToTest, 10.0f, 8, FColor::Red);
	// 	}
	// }

	// AimingTimeline tick handling
	AimingTimeline.TickTimeline(DeltaSeconds);
}

void ATrueFPSCharacter::Destroyed()
{
	Super::Destroyed();
	
	DestroyInventory();
}

void ATrueFPSCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	// reattach weapon if needed
	SetCurrentWeapon(CurrentWeapon);
}

void ATrueFPSCharacter::PossessedBy(AController* C)
{
	Super::PossessedBy(C);
}

void ATrueFPSCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
}

bool ATrueFPSCharacter::IsReplicationPausedForConnection(const FNetViewer& ConnectionOwnerNetViewer)
{
	return Super::IsReplicationPausedForConnection(ConnectionOwnerNetViewer);
}

void ATrueFPSCharacter::OnReplicationPausedChanged(bool bIsReplicationPaused)
{
	GetMesh()->SetHiddenInGame(bIsReplicationPaused, true);
}

void ATrueFPSCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Add Input Mapping Context
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}

	// Setup Client Mesh
	if (IsLocallyControlled())
	{
		GetMesh()->SetVisibility(false);
		ClientMesh->HideBoneByName(FName("neck_01"), PBO_None);
	}
	else
	{
		ClientMesh->DestroyComponent();
		ClientMesh = nullptr;
	}
}

void ATrueFPSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up legacy axis (for aim rotation prediction to work)
	PlayerInputComponent->BindAxis(FName("LookUp"), this, &ThisClass::LookUpDown);
	PlayerInputComponent->BindAxis(FName("LookRight"), this, &ThisClass::TurnRightLeft);
	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ThisClass::OnStartJump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ThisClass::OnStopJump);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);

		// Jumping
		EnhancedInputComponent->BindAction(RunAction, ETriggerEvent::Triggered, this, &ThisClass::OnStartRunning);
		EnhancedInputComponent->BindAction(RunAction, ETriggerEvent::Completed, this, &ThisClass::OnStopRunning);

		// Looking
		// EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);

		// Attacking
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &ThisClass::OnStartFire);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Completed, this, &ThisClass::OnStopFire);

		// Aiming
		EnhancedInputComponent->BindAction(AimingAction, ETriggerEvent::Triggered, this, &ThisClass::OnStartAiming);
		EnhancedInputComponent->BindAction(AimingAction, ETriggerEvent::Completed, this, &ThisClass::OnStopAiming);

		// Weapon Changing
		EnhancedInputComponent->BindAction(NextWeaponAction, ETriggerEvent::Triggered, this, &ThisClass::OnNextWeapon);
		EnhancedInputComponent->BindAction(PreviousWeaponAction, ETriggerEvent::Triggered, this, &ThisClass::OnPrevWeapon);

		// Reloading
		EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Triggered, this, &ThisClass::OnReload);

		// Leaning
		EnhancedInputComponent->BindAction(LeanLeftAction, ETriggerEvent::Triggered, this, &ThisClass::OnLeanLeft);
		EnhancedInputComponent->BindAction(LeanLeftAction, ETriggerEvent::Completed, this, &ThisClass::OnStopLean);
		EnhancedInputComponent->BindAction(LeanRightAction, ETriggerEvent::Triggered, this, &ThisClass::OnLeanRight);
		EnhancedInputComponent->BindAction(LeanRightAction, ETriggerEvent::Completed, this, &ThisClass::OnStopLean);

		// Crouching
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Triggered, this, &ThisClass::OnStartCrouching);
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ThisClass::OnStopCrouching);

		// Toggle Sights
		EnhancedInputComponent->BindAction(ToggleSightsAction, ETriggerEvent::Triggered, this, &ThisClass::OnToggleSights);
	}
}

void ATrueFPSCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add movement 
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

void ATrueFPSCharacter::LookUpDown(const float Value)
{
	AddControllerPitchInput(Value);
}

void ATrueFPSCharacter::TurnRightLeft(const float Value)
{
	AddControllerYawInput(Value);
}

void ATrueFPSCharacter::AddWeapon(ATrueFPSWeaponBase* Weapon)
{
	if (Weapon && GetLocalRole() == ROLE_Authority)
	{
		Weapon->OnEnterInventory(this);
		Inventory.AddUnique(Weapon);
	}
}

void ATrueFPSCharacter::RemoveWeapon(ATrueFPSWeaponBase* Weapon)
{
	if (Weapon && GetLocalRole() == ROLE_Authority)
	{
		Weapon->OnLeaveInventory();
		Inventory.RemoveSingle(Weapon);
	}
}

ATrueFPSWeaponBase* ATrueFPSCharacter::FindWeapon(TSubclassOf<ATrueFPSWeaponBase> WeaponClass)
{
	for (int32 i = 0; i < Inventory.Num(); i++)
	{
		if (Inventory[i] && Inventory[i]->IsA(WeaponClass))
		{
			return Inventory[i];
		}
	}

	return nullptr;
}

void ATrueFPSCharacter::EquipWeapon(ATrueFPSWeaponBase* Weapon)
{
	if (Weapon)
	{
		if (GetLocalRole() == ROLE_Authority)
		{
			SetCurrentWeapon(Weapon, CurrentWeapon);
		}
		else
		{
			ServerEquipWeapon(Weapon);
		}
	}
}

void ATrueFPSCharacter::StartWeaponFire()
{
	if (!bWantsToFire)
	{
		bWantsToFire = true;
		if (CurrentWeapon)
		{
			CurrentWeapon->StartFire();
		}
	}
}

void ATrueFPSCharacter::StopWeaponFire()
{
	if (bWantsToFire)
	{
		bWantsToFire = false;
		if (CurrentWeapon)
		{
			CurrentWeapon->StopFire();
		}
	}
}

bool ATrueFPSCharacter::CanFire() const
{
	return IsAlive();
}

bool ATrueFPSCharacter::CanReload() const
{
	return true;
}

void ATrueFPSCharacter::SetAiming(bool bNewAiming)
{
	bIsAiming = bNewAiming;
	
	if (bIsAiming)
	{
		AimingTimeline.Play();
	}
	else
	{
		AimingTimeline.Reverse();
	}
	
	if (AimingSound)
	{
		UGameplayStatics::SpawnSoundAttached(AimingSound, GetRootComponent());
	}

	if (GetLocalRole() < ROLE_Authority)
	{
		ServerSetAiming(bNewAiming);
	}
}

void ATrueFPSCharacter::SetRunning(bool bNewRunning, bool bToggle)
{
	bWantsToRun = bNewRunning;

	if (GetLocalRole() < ROLE_Authority)
	{
		ServerSetRunning(bNewRunning, bToggle);
	}
}

float ATrueFPSCharacter::PlayAnimMontage(UAnimMontage* AnimMontage, float InPlayRate, FName StartSectionName)
{
	// USkeletalMeshComponent* UseMesh = GetPawnMesh();
	USkeletalMeshComponent* UseMesh = GetMesh();
	if (AnimMontage && UseMesh && UseMesh->AnimScriptInstance)
	{
		return UseMesh->AnimScriptInstance->Montage_Play(AnimMontage, InPlayRate);
	}

	return 0.0f;
}

void ATrueFPSCharacter::StopAnimMontage(UAnimMontage* AnimMontage)
{
	// USkeletalMeshComponent* UseMesh = GetPawnMesh();
	USkeletalMeshComponent* UseMesh = GetMesh();
	if (AnimMontage && UseMesh && UseMesh->AnimScriptInstance &&
		UseMesh->AnimScriptInstance->Montage_IsPlaying(AnimMontage))
	{
		UseMesh->AnimScriptInstance->Montage_Stop(AnimMontage->BlendOut.GetBlendTime(), AnimMontage);
	}
}

void ATrueFPSCharacter::StopAllAnimMontages()
{
	// USkeletalMeshComponent* UseMesh = GetPawnMesh();
	USkeletalMeshComponent* UseMesh = GetMesh();
	if (UseMesh && UseMesh->AnimScriptInstance)
	{
		UseMesh->AnimScriptInstance->Montage_Stop(0.0f);
	}
}

void ATrueFPSCharacter::OnStartFire()
{
	if (IsPlayerRunning())
	{
		SetRunning(false, false);
	}
	
	StartWeaponFire();
	
	// TODO: Implement
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->IsGameInputAllowed())
	// {
	// 	if (IsPlayerRunning())
	// 	{
	// 		SetRunning(false, false);
	// 	}
	// 	StartWeaponFire();
	// }
}

void ATrueFPSCharacter::OnStopFire()
{
	StopWeaponFire();
}

void ATrueFPSCharacter::OnStartAiming()
{
	if (IsPlayerRunning())
	{
		SetRunning(false, false);
	}
	
	SetAiming(true);
	
	// TODO: Implement
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->IsGameInputAllowed())
	// {
	// 	if (IsPlayerRunning())
	// 	{
	// 		SetRunning(false, false);
	// 	}
	// 	SetAiming(true);
	// }
}

void ATrueFPSCharacter::OnStopAiming()
{
	SetAiming(false);
}

void ATrueFPSCharacter::OnNextWeapon()
{
	if (Inventory.Num() >= 2 && (CurrentWeapon == nullptr || CurrentWeapon->GetCurrentState() != EWeaponState::Equipping))
	{
		const int32 CurrentWeaponIdx = Inventory.IndexOfByKey(CurrentWeapon);
		ATrueFPSWeaponBase* NextWeapon = Inventory[(CurrentWeaponIdx + 1) % Inventory.Num()];
		EquipWeapon(NextWeapon);
	}
	
	// TODO: Implement
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->IsGameInputAllowed())
	// {
	// 	if (Inventory.Num() >= 2 && (CurrentWeapon == nullptr || CurrentWeapon->GetCurrentState() != EWeaponState::Equipping))
	// 	{
	// 		const int32 CurrentWeaponIdx = Inventory.IndexOfByKey(CurrentWeapon);
	// 		AShooterWeapon* NextWeapon = Inventory[(CurrentWeaponIdx + 1) % Inventory.Num()];
	// 		EquipWeapon(NextWeapon);
	// 	}
	// }
}

void ATrueFPSCharacter::OnPrevWeapon()
{
	if (Inventory.Num() >= 2 && (CurrentWeapon == nullptr || CurrentWeapon->GetCurrentState() != EWeaponState::Equipping))
	{
		const int32 CurrentWeaponIdx = Inventory.IndexOfByKey(CurrentWeapon);
		ATrueFPSWeaponBase* PrevWeapon = Inventory[(CurrentWeaponIdx - 1 + Inventory.Num()) % Inventory.Num()];
		EquipWeapon(PrevWeapon);
	}

	// TODO: Implement
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->IsGameInputAllowed())
	// {
	// 	if (Inventory.Num() >= 2 && (CurrentWeapon == nullptr || CurrentWeapon->GetCurrentState() != EWeaponState::Equipping))
	// 	{
	// 		const int32 CurrentWeaponIdx = Inventory.IndexOfByKey(CurrentWeapon);
	// 		AShooterWeapon* PrevWeapon = Inventory[(CurrentWeaponIdx - 1 + Inventory.Num()) % Inventory.Num()];
	// 		EquipWeapon(PrevWeapon);
	// 	}
	// }
}

void ATrueFPSCharacter::OnReload()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->StartReload();
	}

	// TODO: Implement
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->IsGameInputAllowed())
	// {
	// 	if (CurrentWeapon)
	// 	{
	// 		CurrentWeapon->StartReload();
	// 	}
	// }
}

void ATrueFPSCharacter::OnStartJump()
{
	bPressedJump = true;
	
	// TODO: Implement
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->IsGameInputAllowed())
	// {
	// 	bPressedJump = true;
	// }
}

void ATrueFPSCharacter::OnStopJump()
{
	bPressedJump = false;
	StopJumping();
}

void ATrueFPSCharacter::OnStartRunning()
{
	if (IsPlayerAiming())
	{
		SetAiming(false);
	}
	
	StopWeaponFire();
	SetRunning(true, false);
	
	// TODO: Implement
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->IsGameInputAllowed())
	// {
	// 	if (IsTargeting())
	// 	{
	// 		SetTargeting(false);
	// 	}
	// 	StopWeaponFire();
	// 	SetRunning(true, false);
	// }
}

void ATrueFPSCharacter::OnStopRunning()
{
	SetRunning(false, false);
}

void ATrueFPSCharacter::OnLeanLeft()
{
	ChangeLeanValue(-LeanAmount);
}

void ATrueFPSCharacter::OnLeanRight()
{
	ChangeLeanValue(LeanAmount);
}

void ATrueFPSCharacter::OnStopLean()
{
	ChangeLeanValue(0.f);
}

void ATrueFPSCharacter::OnStartCrouching()
{
	ToggleCrouching(true);
}

void ATrueFPSCharacter::OnStopCrouching()
{
	ToggleCrouching(false);
}

void ATrueFPSCharacter::OnToggleSights()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->ToggleSights();
	}
}

USkeletalMeshComponent* ATrueFPSCharacter::GetPawnMesh() const
{
	return IsFirstPersonPlayer() ? ClientMesh : GetMesh();
}

ATrueFPSWeaponBase* ATrueFPSCharacter::GetWeapon() const
{
	return CurrentWeapon;
}

FName ATrueFPSCharacter::GetWeaponAttachPoint() const
{
	return WeaponAttachPoint;
}

int32 ATrueFPSCharacter::GetInventoryCount() const
{
	return Inventory.Num();
}

ATrueFPSWeaponBase* ATrueFPSCharacter::GetInventoryWeapon(int32 index) const
{
	return Inventory[index];
}

float ATrueFPSCharacter::GetAimingSpeedModifier() const
{
	return AimingSpeedModifier;
}

bool ATrueFPSCharacter::IsPlayerAiming() const
{
	return bIsAiming;
}

bool ATrueFPSCharacter::IsFiring() const
{
	return bWantsToFire;
}

float ATrueFPSCharacter::GetRunningSpeedModifier() const
{
	return RunningSpeedModifier;
}

bool ATrueFPSCharacter::IsPlayerRunning() const
{
	if (!GetCharacterMovement())
	{
		return false;
	}

	return (bWantsToRun/* || bWantsToRunToggled*/) && !GetVelocity().IsZero() && (GetVelocity().GetSafeNormal2D() | GetActorForwardVector()) > -0.1;
}

bool ATrueFPSCharacter::IsFirstPersonPlayer() const
{
	// return IsAlive() && Controller && Controller->IsLocalPlayerController();
	return IsAlive() && IsLocallyControlled();
	// return true;
}

int32 ATrueFPSCharacter::GetMaxHealth() const
{
	return GetClass()->GetDefaultObject<ATrueFPSCharacter>()->Health;
}

bool ATrueFPSCharacter::IsAlive() const
{
	return Health > 0.f;
}

float ATrueFPSCharacter::GetLowHealthPercentage() const
{
	return LowHealthPercentage;
}

FVector ATrueFPSCharacter::GetFirstPersonCameraTarget_Implementation() const
{
	return GetMesh()->GetSocketLocation(FName("head"));
}

void ATrueFPSCharacter::ChangeLeanValue(float NewLeanValue)
{
	LeanValueTarget = NewLeanValue;
	
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerChangeLeanValue(NewLeanValue);
	}
}

void ATrueFPSCharacter::ServerChangeLeanValue_Implementation(float NewLeanValue)
{
	ChangeLeanValue(NewLeanValue);
}

void ATrueFPSCharacter::ToggleCrouching(bool bNewCrouching)
{
	if (bNewCrouching)
	{
		CrouchValueTarget = 1.f;
		Crouch();
	}
	else
	{
		CrouchValueTarget = 0.f;
		UnCrouch();
	}
	
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerToggleCrouching(bNewCrouching);
	}
}

void ATrueFPSCharacter::ServerToggleCrouching_Implementation(bool bNewCrouching)
{
	ToggleCrouching(bNewCrouching);
}

void ATrueFPSCharacter::TornOff()
{
	SetLifeSpan(25.f);
}

bool ATrueFPSCharacter::IsMoving()
{
	return FMath::Abs(GetLastMovementInputVector().Size()) > 0.f;
}

void ATrueFPSCharacter::AimingTimelineProgress(const float Value)
{
	ADSValue = Value;
}

void ATrueFPSCharacter::AimingTimelineComplete_Implementation()
{
}

float ATrueFPSCharacter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator,
                                    AActor* DamageCauser)
{
	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// if (MyPC && MyPC->HasGodMode())
	// {
	// 	return 0.f;
	// }

	if (Health <= 0.f)
	{
		return 0.f;
	}

	// Modify based on game rules.
	// AShooterGameMode* const Game = GetWorld()->GetAuthGameMode<AShooterGameMode>();
	// Damage = Game ? Game->ModifyDamage(Damage, this, DamageEvent, EventInstigator, DamageCauser) : 0.f;

	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage > 0.f)
	{
		Health -= ActualDamage;
		if (Health <= 0)
		{
			Die(ActualDamage, DamageEvent, EventInstigator, DamageCauser);
		}
		else
		{
			PlayHit(ActualDamage, DamageEvent, EventInstigator ? EventInstigator->GetPawn() : nullptr, DamageCauser);
		}

		MakeNoise(1.0f, EventInstigator ? EventInstigator->GetPawn() : this);
	}

	return ActualDamage;
}

void ATrueFPSCharacter::Suicide()
{
	KilledBy(this);
}

void ATrueFPSCharacter::KilledBy(APawn* EventInstigator)
{
	if (GetLocalRole() == ROLE_Authority && !bIsDying)
	{
		AController* Killer = nullptr;
		if (EventInstigator != nullptr)
		{
			Killer = EventInstigator->Controller;
			LastHitBy = nullptr;
		}

		Die(Health, FDamageEvent(UDamageType::StaticClass()), Killer, nullptr);
	}
}

bool ATrueFPSCharacter::CanDie(float KillingDamage, FDamageEvent const& DamageEvent, AController* Killer,
	AActor* DamageCauser) const
{
	if (bIsDying										// already dying
		|| IsValid(this)								// already destroyed
		|| GetLocalRole() != ROLE_Authority)			// not authority	// level transition occurring
	{
		return false;
	}

	return true;
}

bool ATrueFPSCharacter::Die(float KillingDamage, FDamageEvent const& DamageEvent, AController* Killer,
	AActor* DamageCauser)
{
	if (!CanDie(KillingDamage, DamageEvent, Killer, DamageCauser))
	{
		return false;
	}

	Health = FMath::Min(0.0f, Health);

	// if this is an environmental death then refer to the previous killer so that they receive credit (knocked into lava pits, etc)
	UDamageType const* const DamageType = DamageEvent.DamageTypeClass ? DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
	Killer = GetDamageInstigator(Killer, *DamageType);

	// AController* const KilledPlayer = (Controller != nullptr) ? Controller : Cast<AController>(GetOwner());
	// GetWorld()->GetAuthGameMode<AShooterGameMode>()->Killed(Killer, KilledPlayer, this, DamageType);

	NetUpdateFrequency = GetDefault<ATrueFPSCharacter>()->NetUpdateFrequency;
	GetCharacterMovement()->ForceReplicationUpdate();

	OnDeath(KillingDamage, DamageEvent, Killer ? Killer->GetPawn() : nullptr, DamageCauser);
	return true;
}

void ATrueFPSCharacter::FellOutOfWorld(const UDamageType& dmgType)
{
	Die(Health, FDamageEvent(dmgType.GetClass()), nullptr, nullptr);
}

void ATrueFPSCharacter::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// only to local owner: weapon change requests are locally instigated, other clients don't need it
	DOREPLIFETIME_CONDITION(ThisClass, Inventory, COND_OwnerOnly);

	// everyone except local owner: flag change is locally instigated
	DOREPLIFETIME_CONDITION(ThisClass, bIsAiming, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ThisClass, ADSValue, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ThisClass, bWantsToRun, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ThisClass, LeanValueTarget, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ThisClass, CrouchValueTarget, COND_SkipOwner);

	DOREPLIFETIME_CONDITION(ThisClass, LastTakeHitInfo, COND_Custom);

	// everyone
	DOREPLIFETIME(ThisClass, CurrentWeapon);
	DOREPLIFETIME(ThisClass, Health);
}

void ATrueFPSCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Only replicate this property for a short duration after it changes so join in progress players don't get spammed with fx when joining late
	DOREPLIFETIME_ACTIVE_OVERRIDE(ThisClass, LastTakeHitInfo, GetWorld() && GetWorld()->GetTimeSeconds() < LastTakeHitTimeTimeout);

	// Only replicate this property if it's whether fully filled or empty
	DOREPLIFETIME_ACTIVE_OVERRIDE(ThisClass, ADSValue, ADSValue == 0.f || ADSValue == 1.f);
}

void ATrueFPSCharacter::OnDeath(float KillingDamage, FDamageEvent const& DamageEvent, APawn* PawnInstigator,
	AActor* DamageCauser)
{
	if (bIsDying)
	{
		return;
	}

	SetReplicatingMovement(false);
	TearOff();
	bIsDying = true;

	if (GetLocalRole() == ROLE_Authority)
	{
		ReplicateHit(KillingDamage, DamageEvent, PawnInstigator, DamageCauser, true);

		// play the force feedback effect on the client player controller
		// AShooterPlayerController* PC = Cast<AShooterPlayerController>(Controller);
		// if (PC && DamageEvent.DamageTypeClass)
		// {
		// 	UShooterDamageType *DamageType = Cast<UShooterDamageType>(DamageEvent.DamageTypeClass->GetDefaultObject());
		// 	if (DamageType && DamageType->KilledForceFeedback && PC->IsVibrationEnabled())
		// 	{
		// 		FForceFeedbackParameters FFParams;
		// 		FFParams.Tag = "Damage";
		// 		PC->ClientPlayForceFeedback(DamageType->KilledForceFeedback, FFParams);
		// 	}
		// }
	}

	// cannot use IsLocallyControlled here, because even local client's controller may be nullptr here
	if (GetNetMode() != NM_DedicatedServer && DeathSound && ClientMesh && ClientMesh->IsVisible())
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation());
	}

	// remove all weapons
	DestroyInventory();

	DetachFromControllerPendingDestroy();
	StopAllAnimMontages();

	if (GetMesh())
	{
		static FName CollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetCollisionProfileName(CollisionProfileName);
	}
	SetActorEnableCollision(true);

	// Death anim
	float DeathAnimDuration = PlayAnimMontage(DeathAnim);

	// Ragdoll
	if (DeathAnimDuration > 0.f)
	{
		// Trigger ragdoll a little before the animation early so the character doesn't
		// blend back to its normal position.
		const float TriggerRagdollTime = DeathAnimDuration - 0.7f;

		// Enable blend physics so the bones are properly blending against the montage.
		GetMesh()->bBlendPhysics = true;

		// Use a local timer handle as we don't need to store it for later but we don't need to look for something to clear
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, this, &ThisClass::SetRagdollPhysics, FMath::Max(0.1f, TriggerRagdollTime), false);
	}
	else
	{
		SetRagdollPhysics();
	}

	// disable collisions on capsule
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
}

void ATrueFPSCharacter::PlayHit(float DamageTaken, FDamageEvent const& DamageEvent, APawn* PawnInstigator,
	AActor* DamageCauser)
{
	if (GetLocalRole() == ROLE_Authority)
	{
		ReplicateHit(DamageTaken, DamageEvent, PawnInstigator, DamageCauser, false);

		// play the force feedback effect on the client player controller
		// AShooterPlayerController* PC = Cast<AShooterPlayerController>(Controller);
		// if (PC && DamageEvent.DamageTypeClass)
		// {
		// 	UShooterDamageType *DamageType = Cast<UShooterDamageType>(DamageEvent.DamageTypeClass->GetDefaultObject());
		// 	if (DamageType && DamageType->HitForceFeedback && PC->IsVibrationEnabled())
		// 	{
		// 		FForceFeedbackParameters FFParams;
		// 		FFParams.Tag = "Damage";
		// 		PC->ClientPlayForceFeedback(DamageType->HitForceFeedback, FFParams);
		// 	}
		// }
	}

	if (DamageTaken > 0.f)
	{
		ApplyDamageMomentum(DamageTaken, DamageEvent, PawnInstigator, DamageCauser);
	}

	// AShooterPlayerController* MyPC = Cast<AShooterPlayerController>(Controller);
	// AShooterHUD* MyHUD = MyPC ? Cast<AShooterHUD>(MyPC->GetHUD()) : nullptr;
	// if (MyHUD)
	// {
	// 	MyHUD->NotifyWeaponHit(DamageTaken, DamageEvent, PawnInstigator);
	// }
	//
	// if (PawnInstigator && PawnInstigator != this && PawnInstigator->IsLocallyControlled())
	// {
	// 	AShooterPlayerController* InstigatorPC = Cast<AShooterPlayerController>(PawnInstigator->Controller);
	// 	AShooterHUD* InstigatorHUD = InstigatorPC ? Cast<AShooterHUD>(InstigatorPC->GetHUD()) : nullptr;
	// 	if (InstigatorHUD)
	// 	{
	// 		InstigatorHUD->NotifyEnemyHit();
	// 	}
	// }
}

void ATrueFPSCharacter::SetRagdollPhysics()
{
	bool bInRagdoll = false;

	if (IsValid(this))
	{
		bInRagdoll = false;
	}
	else if (!GetMesh() || !GetMesh()->GetPhysicsAsset())
	{
		bInRagdoll = false;
	}
	else
	{
		// initialize physics/etc
		GetMesh()->SetSimulatePhysics(true);
		GetMesh()->WakeAllRigidBodies();
		GetMesh()->bBlendPhysics = true;

		bInRagdoll = true;
	}

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->SetComponentTickEnabled(false);

	if (!bInRagdoll)
	{
		// hide and set short lifespan
		TurnOff();
		SetActorHiddenInGame(true);
		SetLifeSpan(1.0f);
	}
	else
	{
		SetLifeSpan(10.0f);
	}
}

void ATrueFPSCharacter::ReplicateHit(float Damage, FDamageEvent const& DamageEvent, APawn* PawnInstigator,
	AActor* DamageCauser, bool bKilled)
{
	const float TimeoutTime = GetWorld()->GetTimeSeconds() + 0.5f;

	FDamageEvent const& LastDamageEvent = LastTakeHitInfo.GetDamageEvent();
	if ((PawnInstigator == LastTakeHitInfo.PawnInstigator.Get()) && (LastDamageEvent.DamageTypeClass == LastTakeHitInfo.DamageTypeClass) && (LastTakeHitTimeTimeout == TimeoutTime))
	{
		// same frame damage
		if (bKilled && LastTakeHitInfo.bKilled)
		{
			// Redundant death take hit, just ignore it
			return;
		}

		// otherwise, accumulate damage done this frame
		Damage += LastTakeHitInfo.ActualDamage;
	}

	LastTakeHitInfo.ActualDamage = Damage;
	LastTakeHitInfo.PawnInstigator = Cast<ATrueFPSCharacter>(PawnInstigator);
	LastTakeHitInfo.DamageCauser = DamageCauser;
	LastTakeHitInfo.SetDamageEvent(DamageEvent);
	LastTakeHitInfo.bKilled = bKilled;
	LastTakeHitInfo.EnsureReplication();

	LastTakeHitTimeTimeout = TimeoutTime;
}

void ATrueFPSCharacter::OnRep_LastTakeHitInfo()
{
	if (LastTakeHitInfo.bKilled)
	{
		OnDeath(LastTakeHitInfo.ActualDamage, LastTakeHitInfo.GetDamageEvent(), LastTakeHitInfo.PawnInstigator.Get(), LastTakeHitInfo.DamageCauser.Get());
	}
	else
	{
		PlayHit(LastTakeHitInfo.ActualDamage, LastTakeHitInfo.GetDamageEvent(), LastTakeHitInfo.PawnInstigator.Get(), LastTakeHitInfo.DamageCauser.Get());
	}
}

void ATrueFPSCharacter::SetCurrentWeapon(ATrueFPSWeaponBase* NewWeapon, ATrueFPSWeaponBase* LastWeapon)
{
	ATrueFPSWeaponBase* LocalLastWeapon = nullptr;

	if (LastWeapon != nullptr)
	{
		LocalLastWeapon = LastWeapon;
	}
	else if (NewWeapon != CurrentWeapon)
	{
		LocalLastWeapon = CurrentWeapon;
	}

	// unequip previous
	if (LocalLastWeapon)
	{
		LocalLastWeapon->OnUnEquip();
	}

	CurrentWeapon = NewWeapon;

	// equip new one
	if (NewWeapon)
	{
		NewWeapon->SetOwningPawn(this);	// Make sure weapon's MyPawn is pointing back to us. During replication, we can't guarantee APawn::CurrentWeapon will rep after AWeapon::MyPawn!

		NewWeapon->OnEquip(LastWeapon);

		// Setup Aiming LocationCurve Timeline
		if (UCurveFloat* AimingCurve = NewWeapon->GetAnimProperties().AimingCurve)
		{
			FOnTimelineFloatStatic Progress;
			Progress.BindUObject(this, &ThisClass::AimingTimelineProgress);

			FOnTimelineEvent Event;
			Event.BindDynamic(this, &ThisClass::AimingTimelineComplete);

			AimingTimeline.SetPlayRate(1.f);
			AimingTimeline.AddInterpFloat(AimingCurve, Progress);
			AimingTimeline.AddEvent(AimingTimeline.GetTimelineLength(), Event);
		}
	}

	// notify anim instance
	if (UTrueFPSAnimInstanceBase* AnimInstance = Cast<UTrueFPSAnimInstanceBase>(GetMesh()->GetAnimInstance()))
	{
		AnimInstance->OnCharacterCurrentWeaponChanged();
	}
}

void ATrueFPSCharacter::OnRep_CurrentWeapon(ATrueFPSWeaponBase* LastWeapon)
{
	SetCurrentWeapon(CurrentWeapon, LastWeapon);
}

void ATrueFPSCharacter::SpawnDefaultInventory()
{
	if (GetLocalRole() < ROLE_Authority)
	{
		return;
	}

	int32 NumWeaponClasses = DefaultInventoryClasses.Num();
	for (int32 i = 0; i < NumWeaponClasses; i++)
	{
		if (DefaultInventoryClasses[i])
		{
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATrueFPSWeaponBase* NewWeapon = GetWorld()->SpawnActor<ATrueFPSWeaponBase>(DefaultInventoryClasses[i], SpawnInfo);
			AddWeapon(NewWeapon);
		}
	}

	// equip first weapon in inventory
	if (Inventory.Num() > 0)
	{
		EquipWeapon(Inventory[0]);
	}
}

void ATrueFPSCharacter::DestroyInventory()
{
	if (GetLocalRole() < ROLE_Authority)
	{
		return;
	}

	// remove all weapons from inventory and destroy them
	for (int32 i = Inventory.Num() - 1; i >= 0; i--)
	{
		ATrueFPSWeaponBase* Weapon = Inventory[i];
		if (Weapon)
		{
			RemoveWeapon(Weapon);
			Weapon->Destroy();
		}
	}
}

void ATrueFPSCharacter::ServerEquipWeapon_Implementation(ATrueFPSWeaponBase* Weapon)
{
	EquipWeapon(Weapon);
}

void ATrueFPSCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
	SetAiming(bNewAiming);
}

void ATrueFPSCharacter::ServerSetRunning_Implementation(bool bNewRunning, bool bToggle)
{
	SetRunning(bNewRunning, bToggle);
}

void ATrueFPSCharacter::BuildPauseReplicationCheckPoints(TArray<FVector>& RelevancyCheckPoints)
{
	FBoxSphereBounds Bounds = GetCapsuleComponent()->CalcBounds(GetCapsuleComponent()->GetComponentTransform());
	FBox BoundingBox = Bounds.GetBox();
	float XDiff = Bounds.BoxExtent.X * 2;
	float YDiff = Bounds.BoxExtent.Y * 2;

	RelevancyCheckPoints.Add(BoundingBox.Min);
	RelevancyCheckPoints.Add(FVector(BoundingBox.Min.X + XDiff, BoundingBox.Min.Y, BoundingBox.Min.Z));
	RelevancyCheckPoints.Add(FVector(BoundingBox.Min.X, BoundingBox.Min.Y + YDiff, BoundingBox.Min.Z));
	RelevancyCheckPoints.Add(FVector(BoundingBox.Min.X + XDiff, BoundingBox.Min.Y + YDiff, BoundingBox.Min.Z));
	RelevancyCheckPoints.Add(FVector(BoundingBox.Max.X - XDiff, BoundingBox.Max.Y, BoundingBox.Max.Z));
	RelevancyCheckPoints.Add(FVector(BoundingBox.Max.X, BoundingBox.Max.Y - YDiff, BoundingBox.Max.Z));
	RelevancyCheckPoints.Add(FVector(BoundingBox.Max.X - XDiff, BoundingBox.Max.Y - YDiff, BoundingBox.Max.Z));
	RelevancyCheckPoints.Add(BoundingBox.Max);
}
