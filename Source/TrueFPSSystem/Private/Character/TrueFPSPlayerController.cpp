// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/TrueFPSPlayerController.h"

#include "Character/TrueFPSPlayerCameraManager.h"

ATrueFPSPlayerController::ATrueFPSPlayerController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PlayerCameraManagerClass = ATrueFPSPlayerCameraManager::StaticClass();
}
