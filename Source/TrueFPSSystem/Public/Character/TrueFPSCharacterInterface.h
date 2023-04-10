// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TrueFPSCharacterInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, BlueprintType)
class UTrueFPSCharacterInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class TRUEFPSSYSTEM_API ITrueFPSCharacterInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
	
public:

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character")
	bool IsFirstPerson();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character")
	class USkeletalMeshComponent* GetSpecificPawnMesh(bool WantFirstPerson) const;

	//////////////////////////////////////////////////////////////////////////
	// Animations

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations")
	FRotator GetLastUpdatedRotationValues();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations")
	FTransform GetDomHandTransform();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations")
	bool IsRunning();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations")
	float GetCrouchValue() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations")
	FVector GetFirstPersonCameraTarget() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations")
	FRotator GetFirstPersonCameraTargetRotation() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations|Weapons")
	float GetADSValue();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations|Weapons")
	float GetLeanValue();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Animations|Weapons")
	FName GetWeaponAttachPoint();

	//////////////////////////////////////////////////////////////////////////
	// Weapons

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Weapons")
	bool IsAiming() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Weapons")
	bool HasCurrentWeapon();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Weapons")
	class ATrueFPSWeaponBase* GetCurrentWeapon();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Weapons")
	bool CanFire();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="TrueFPS Character|Weapons")
	bool CanReload();
	
};
