// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/TrueFPSCharacterMovement.h"

#include "Character/TrueFPSCharacter.h"

UTrueFPSCharacterMovement::UTrueFPSCharacterMovement(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

float UTrueFPSCharacterMovement::GetMaxSpeed() const
{
	float MaxSpeed = Super::GetMaxSpeed();

	if (const ATrueFPSCharacter* TrueFPSCharacter = Cast<ATrueFPSCharacter>(PawnOwner))
	{
		if (TrueFPSCharacter->IsPlayerAiming())
		{
			MaxSpeed *= TrueFPSCharacter->GetAimingSpeedModifier();
		}
		
		if (TrueFPSCharacter->IsPlayerRunning())
		{
			MaxSpeed *= TrueFPSCharacter->GetRunningSpeedModifier();
		}
	}

	return MaxSpeed;
}
