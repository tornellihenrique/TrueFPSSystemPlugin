// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/Animation/TrueFPSAnimInstanceBase.h"

#include "KismetAnimationLibrary.h"
#include "Character/TrueFPSCharacterInterface.h"
#include "Weapons/TrueFPSWeaponBase.h"
#include "Camera/CameraComponent.h"
#include "Curves/CurveVector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

UTrueFPSAnimInstanceBase::UTrueFPSAnimInstanceBase()
{
}

void UTrueFPSAnimInstanceBase::NativeBeginPlay()
{
	Super::NativeBeginPlay();
}

void UTrueFPSAnimInstanceBase::NativeUpdateAnimation(float DeltaTime)
{
	Super::NativeUpdateAnimation(DeltaTime);

	DebugInfo.Empty();

	if (!Character)
	{
		Character = Cast<ACharacter>(TryGetPawnOwner());
		
		if (Character) OnCharacterInitialized();
		else return;
	}

	UpdateVariables(DeltaTime);

	UpdateTurnInPlace(DeltaTime);

	// Calculate OutOffsetLocation and OutOffsetRotation variables
	FVector OutOffsetLocation = FVector::ZeroVector;
	FRotator OutOffsetRotation = FRotator::ZeroRotator;
	UpdateOffsetTransform(DeltaTime, OutOffsetLocation, OutOffsetRotation);

	// Update the OffsetTransform with the updated offset location and rotation
	if (Character->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_HasCurrentWeapon(Character))
		OffsetTransform = ITrueFPSCharacterInterface::Execute_GetCurrentWeapon(Character)->GetOffsetTransform() * FTransform(OutOffsetRotation, OutOffsetLocation);
	else
		OffsetTransform = FTransform();

	FVector OutPlacementLocation = FVector::ZeroVector;
	FRotator OutPlacementRotation = FRotator::ZeroRotator;
	UpdatePlacementTransform(DeltaTime, OutPlacementLocation, OutPlacementRotation);

	PlacementTransform = CurrentWeaponCustomOffsetTransform * FTransform(OutPlacementRotation, OutPlacementLocation);

	PostUpdateAnimation(DeltaTime);
}

void UTrueFPSAnimInstanceBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UTrueFPSAnimInstanceBase::PostUpdateAnimation(const float DeltaTime)
{
	// Set old vars for next anim update
	LastCameraRotation = CameraRotation;
	LastVelocity = Character->GetCharacterMovement()->Velocity;
}

void UTrueFPSAnimInstanceBase::OnCharacterInitialized()
{
	Mesh = Character->GetMesh();
}

void UTrueFPSAnimInstanceBase::UpdateVariables(const float DeltaTime)
{
	if (Character->IsLocallyControlled())
	{
		const APlayerController* PlayerController = Character->GetController<APlayerController>();
		if (!PlayerController)
		{
			UE_LOG(LogTemp, Error, TEXT("Owning Pawn is not controlled by a valid Player Controller!"));
			return;
		}

		const FRotator LastUpdatedRotationValues = Character->Implements<UTrueFPSCharacterInterface>() ? ITrueFPSCharacterInterface::Execute_GetLastUpdatedRotationValues(Character) : FRotator::ZeroRotator;
		
		const FRotator AddRot(
			LastUpdatedRotationValues.Pitch * PlayerController->InputPitchScale_DEPRECATED,
			LastUpdatedRotationValues.Yaw * PlayerController->InputYawScale_DEPRECATED,
			LastUpdatedRotationValues.Roll * PlayerController->InputRollScale_DEPRECATED);
		
		const FRotator PreCalculatedCameraRot = Character->GetBaseAimRotation() + AddRot;

		constexpr float ClampAngle = 89.9f;
		CameraRotation = FRotator(FMath::ClampAngle(PreCalculatedCameraRot.Pitch, -ClampAngle, ClampAngle), PreCalculatedCameraRot.Yaw, PreCalculatedCameraRot.Roll);
	}
	else
	{
		// Clamp pitch to prevent weird yaw rotation glitch on simulated proxies
		constexpr float ClampAngle = 89.9f;
		FRotator ClampedBaseAimRotation = Character->GetBaseAimRotation();
		ClampedBaseAimRotation.Pitch = FMath::ClampAngle(ClampedBaseAimRotation.Pitch, -ClampAngle, ClampAngle);

		// Interp camera rotation to prevent a hitchy look on non-local viewports
		CameraRotation = UKismetMathLibrary::RInterpTo(CameraRotation, ClampedBaseAimRotation, DeltaTime, NonLocalCameraRotationInterpSpeed);
	}

	if (Character->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_HasCurrentWeapon(Character))
	{
		const FTransform DomHandTransform = ITrueFPSCharacterInterface::Execute_GetDomHandTransform(Character);

		const ATrueFPSWeaponBase* CurrentWeapon = ITrueFPSCharacterInterface::Execute_GetCurrentWeapon(Character);
		const FTransform OrientationWorldTransform = CurrentWeapon->GetOrientationWorldTransform(AimingValue);
		
		DebugInfo.Add(TEXT("OrientationWorldTransform"), OrientationWorldTransform.ToString());
		
		OriginRelativeTransform = OrientationWorldTransform.GetRelativeTransform(DomHandTransform);
		if (AimingValue > 0.f) SightsRelativeTransform = ITrueFPSCharacterInterface::Execute_GetCurrentWeapon(Character)->GetSightsWorldTransform().GetRelativeTransform(DomHandTransform);
	}
	
	// Used for mesh calculations
	AimRotation = CameraRotation - Character->GetActorRotation();

	// Applying Leaning manually because Character Roll Rotation is not replicated like Pitch Rotation
	if (Character->Implements<UTrueFPSCharacterInterface>())
		AimRotation = FRotator(AimRotation.Pitch, AimRotation.Yaw, ITrueFPSCharacterInterface::Execute_GetLeanValue(Character));
	
	/**
	/* Locomotion Variables
	/*/

	MovementDirection = UKismetAnimationLibrary::CalculateDirection(Character->GetCharacterMovement()->Velocity, FRotator(0.f, Character->GetBaseAimRotation().Yaw, 0.f));

	MovementSpeed = Character->GetCharacterMovement()->Velocity.Size();

	bIsFalling = Character->GetCharacterMovement()->IsFalling();
	bIsRunning = Character->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_IsRunning(Character);
	CrouchValue = Character->Implements<UTrueFPSCharacterInterface>() ? ITrueFPSCharacterInterface::Execute_GetCrouchValue(Character) : 0.f;

	MovementWeaponSwayProgressTime += DeltaTime * (MovementSpeedInterp / MaxMoveSpeed);

	/**
	/* IK Variables
	/*/

	if (Character->Implements<UTrueFPSCharacterInterface>())
		AimingValue = ITrueFPSCharacterInterface::Execute_GetADSValue(Character);
	else
		AimingValue = 0.f;
	
	/**
	/* Accumulative Offsets Variables
	/*/

	constexpr float ANGLE_CLAMP = 4.5f * 60.f;
	const float AngleClampDelta = ANGLE_CLAMP * DeltaTime;
	
	const FRotator AddRotation = (CameraRotation - LastCameraRotation);
	FRotator AddRotationClamped = FRotator(FMath::ClampAngle(AddRotation.Pitch, -AngleClampDelta, AngleClampDelta) * 1.5f,
		FMath::ClampAngle(AddRotation.Yaw, -AngleClampDelta, AngleClampDelta),
			FMath::ClampAngle(AddRotation.Roll, -AngleClampDelta, AngleClampDelta));
	AddRotationClamped.Roll += AddRotationClamped.Yaw * 0.7f;

	// Multiplied by DeltaTime to remain frame-independent
	AddRotationClamped *= DeltaTime * 50.f;
	
	AccumulativeRotation += AddRotationClamped;
	AccumulativeRotation = UKismetMathLibrary::RInterpTo(AccumulativeRotation, FRotator::ZeroRotator, DeltaTime, AccumulativeRotationReturnInterpSpeed);
	AccumulativeRotationInterp = UKismetMathLibrary::RInterpTo(AccumulativeRotationInterp, AccumulativeRotation, DeltaTime, AccumulativeRotationInterpSpeed);

	MovementSpeedInterp = UKismetMathLibrary::FInterpTo(MovementSpeedInterp, bIsFalling ? 0.f : Character->GetCharacterMovement()->Velocity.Size(), DeltaTime, 3.f);

	const FVector Velocity = Character->GetCharacterMovement()->Velocity;
	const FVector Difference = (Velocity - LastVelocity) * DeltaTime;
	
	VelocityTarget = UKismetMathLibrary::VInterpTo(VelocityTarget, Character->GetCharacterMovement()->Velocity, DeltaTime, VelocityInterpSpeed);
	if (Difference.Z > 1.5f) // Jumping / landing impulse
		VelocityTarget.Z += Difference.Z * 400.f * LandingImpactBobMultiplier;

	// 25% when fully aiming
	VelocityInterp = UKismetMathLibrary::VInterpTo(VelocityInterp, VelocityTarget * (1.f - AimingValue * 0.75f), DeltaTime, 3.f);

	DebugInfo.Add(TEXT("AimRotation"), AimRotation.ToString());
	DebugInfo.Add(TEXT("AimingValue"), FString::SanitizeFloat(AimingValue));
	DebugInfo.Add(TEXT("OriginRelativeTransform"), OriginRelativeTransform.ToString());
	DebugInfo.Add(TEXT("SightsRelativeTransform"), SightsRelativeTransform.ToString());

	if (Character->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_HasCurrentWeapon(Character))
	{
		const ATrueFPSWeaponBase* CurrentWeapon = ITrueFPSCharacterInterface::Execute_GetCurrentWeapon(Character);

		CurrentWeaponOffHandAdditiveTransform = CurrentWeapon->OffHandAdditiveTransform;
	}
}

void UTrueFPSAnimInstanceBase::UpdateOffsetTransform(const float DeltaTime, FVector& OutOffsetLocation,
	FRotator& OutOffsetRotation)
{
	// Get inverse to apply the opposite of the rotational influence to the weapon sway
	const FRotator AccumulativeRotationInterpInverse = AccumulativeRotationInterp.GetInverse();

	// Apply location offset from accumulative rotation inverse
	OutOffsetLocation += FVector(0.f, AccumulativeRotationInterpInverse.Yaw, AccumulativeRotationInterpInverse.Pitch) / 6.f;

	// Apply location offset from interp orientation velocity
	const FVector OrientationVelocityInterp = (FRotator(0.f, CameraRotation.Yaw, 0.f) - VelocityInterp.Rotation()).Vector() * VelocityInterp.Size() * FVector(1.f, -1.f, 1.f);
	const FVector MovementOffset = (OrientationVelocityInterp / MaxMoveSpeed) * FMath::Max<float>(1.f - AimingValue, 0.6f);
	OutOffsetLocation += MovementOffset;
        
	// Add accumulative rotation
	OutOffsetRotation += AccumulativeRotationInterpInverse;

	// Add movement offset to rotation
	OutOffsetRotation += FRotator(MovementOffset.Z * 5.f, MovementOffset.Y, MovementOffset.Y * 2.f);

	// Apply weight scale of weapon to offsets before weapon sway curves
	OutOffsetLocation *= CurrentWeightScale;
	OutOffsetRotation.Pitch *= CurrentWeightScale;
	OutOffsetRotation.Yaw *= CurrentWeightScale;
	OutOffsetRotation.Roll *= CurrentWeightScale;

	// Apply idle vector curve anim to offset location
	if (IdleWeaponSwayCurve)
	{
		const FVector SwayOffset = IdleWeaponSwayCurve->GetVectorValue(GetWorld()->GetTimeSeconds()) * 8.f * FMath::Max<float>(1.f - AimingValue, 0.1f);
		OutOffsetLocation += SwayOffset;
		OutOffsetRotation += FRotator(SwayOffset.Z * 0.3f, SwayOffset.Y * 0.5f, SwayOffset.Y * 1.2f);
	}
	
	// Apply movement offset to offset location
	if (MovementWeaponSwayCurve)
	{
		const FVector SwayOffset = MovementWeaponSwayCurve->GetVectorValue(MovementWeaponSwayProgressTime) * (MovementSpeedInterp / MaxMoveSpeed) * 5.f * FMath::Max<float>(1.f - AimingValue, 0.1f);
		OutOffsetLocation += SwayOffset * 0.7f;
		OutOffsetRotation += FRotator(SwayOffset.Z * 0.5f, SwayOffset.Y * 0.8f, SwayOffset.Y * 1.f);
	}

	// Hit reaction stuff
	HandleHitReaction(DeltaTime, OutOffsetLocation, OutOffsetRotation);
}

void UTrueFPSAnimInstanceBase::UpdatePlacementTransform(const float DeltaTime, FVector& OutPlacementLocation,
	FRotator& OutPlacementRotation)
{
	OutPlacementRotation.Pitch = CameraRotation.Pitch * WeaponPitchTiltMultiplier;
	OutPlacementLocation.Z = -CameraRotation.Pitch * (WeaponPitchTiltMultiplier / 3.f);
}

void UTrueFPSAnimInstanceBase::UpdateTurnInPlace(const float DeltaTime)
{
	if (!bIsTurningInPlace && abs(RootYawOffset) < 2.f && Character->GetCharacterMovement()->Velocity.Size() < StationaryVelocityThreshold)
	{
		bIsTurningInPlace = true;
	}

	if (bIsTurningInPlace)
	{
		RootYawOffset += FRotator::NormalizeAxis(LastCameraRotation.Yaw - CameraRotation.Yaw);

		// If exceeded rotation or velocity thresholds, set turn in place to false and set rot speed to desired speed
		if (Character->GetCharacterMovement()->Velocity.Size() >= StationaryVelocityThreshold)
		{
			bIsTurningInPlace = false;
			StationaryYawInterpSpeed = 8.f;
		}
		else if (abs(RootYawOffset) >= StationaryYawThreshold)
		{
			bIsTurningInPlace = false;
			StationaryYawInterpSpeed = 5.f;
		}

		// If no longer turning in place, set the rotation amount for turning animation usage
		if (!bIsTurningInPlace)
		{
			StationaryYawAmount = -RootYawOffset;
		}
	}

	if (!bIsTurningInPlace && RootYawOffset)
	{
		const float YawDifference = FRotator::NormalizeAxis(LastCameraRotation.Yaw - CameraRotation.Yaw);
		RootYawOffset += YawDifference;
		
		if (-YawDifference > 0.f == StationaryYawAmount > 0.f)
			StationaryYawAmount += -YawDifference;

		StationaryYawSpeedNormal = FMath::Clamp<float>(StationaryYawAmount / 180.f, 1.5f, 3.f);
            
		// Never allow the yaw offset to exceed the yaw threshold
		RootYawOffset = FMath::ClampAngle(RootYawOffset, -StationaryYawThreshold, StationaryYawThreshold);
            
		// Never allow yaw offset to exceed yaw threshold
		RootYawOffset = UKismetMathLibrary::FInterpTo(RootYawOffset, 0.f, DeltaTime, StationaryYawInterpSpeed);

		// Once matched rotation, clear vars
		if (abs(RootYawOffset) < 2.f)
		{
			StationaryYawInterpSpeed = 0.f;
			StationaryYawAmount = 0.f;
			RootYawOffset = 0.f;
		}
	}
}

void UTrueFPSAnimInstanceBase::OnCharacterLanded(ACharacter* InCharacter, const FHitResult& Hit)
{
	BP_OnCharacterLanded(InCharacter, Hit);
}

void UTrueFPSAnimInstanceBase::OnCharacterCurrentWeaponChanged()
{
	if (Character->Implements<UTrueFPSCharacterInterface>() && ITrueFPSCharacterInterface::Execute_HasCurrentWeapon(Character))
	{
		const ATrueFPSWeaponBase* CurrentWeapon = ITrueFPSCharacterInterface::Execute_GetCurrentWeapon(Character);
		const FWeaponAnimProperties AnimProperties = CurrentWeapon->GetAnimProperties();
		CurrentWeightScale = AnimProperties.WeightScale;
		CurrentWeaponCustomOffsetTransform = AnimProperties.CustomOffsetTransform;
		CurrentAnimState = AnimProperties.AnimState;

		// Get the sight transform relative to right hand for control rig calculations
		const FTransform DomHandTransform = ITrueFPSCharacterInterface::Execute_GetDomHandTransform(Character);
		SightsRelativeTransform = CurrentWeapon->GetSightsWorldTransform().GetRelativeTransform(DomHandTransform);
		OriginRelativeTransform = CurrentWeapon->GetOrientationWorldTransform(AimingValue).GetRelativeTransform(DomHandTransform);
	}
	else
	{
		CurrentWeightScale = 0.f;
		CurrentWeaponCustomOffsetTransform = FTransform::Identity;
		SightsRelativeTransform = FTransform::Identity;
		OriginRelativeTransform = FTransform::Identity;
	}
}

void UTrueFPSAnimInstanceBase::ApplyHitReaction(UCurveFloat* Curve, const FVector& Direction, const float Magnitude,
                                                const float PlaySpeed)
{
	if (!Curve || Direction.Size() == 0 || Magnitude == 0) return;
	HitReactions.Add(FHitReaction(Curve, Direction, Magnitude, PlaySpeed));
}

void UTrueFPSAnimInstanceBase::HandleHitReaction(const float DeltaTime, FVector& OutOffsetLocation,
	FRotator& OutOffsetRotation)
{
	for (int32 i = 0; i < HitReactions.Num(); i++)
	{
		const FVector& Impulse = HitReactions[i].Impulse;
		const float Value = HitReactions[i].Curve->GetFloatValue(HitReactions[i].Position);

		const FRotator Rot = FRotator(Impulse.X + Impulse.Z, 0.f, -Impulse.Y) * Value;
		
		AimRotation += Rot;
		OutOffsetRotation += Rot.GetInverse();
		OutOffsetLocation += -Impulse * Value;
		
		float Lifespan,Temp;
		HitReactions[i].Curve->GetTimeRange(Temp, Lifespan);
		float& Position = HitReactions[i].Position;
		
		if (Position >= Lifespan)
		{
			HitReactions.RemoveAt(i--);
			continue;
		}
		Position = FMath::Min<float>(Lifespan, Position + DeltaTime * HitReactions[i].PlaySpeed);
	}
}
