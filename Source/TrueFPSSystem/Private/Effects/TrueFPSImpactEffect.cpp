// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/TrueFPSImpactEffect.h"

#include "ParticleDefinitions.h"
#include "SoundDefinitions.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundCue.h"

ATrueFPSImpactEffect::ATrueFPSImpactEffect(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetAutoDestroyWhenFinished(true);
}

void ATrueFPSImpactEffect::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	UPhysicalMaterial* HitPhysMat = SurfaceHit.PhysMaterial.Get();
	EPhysicalSurface HitSurfaceType = UPhysicalMaterial::DetermineSurfaceType(HitPhysMat);

	// show particles
	UParticleSystem* ImpactFX = GetImpactFX(HitSurfaceType);
	if (ImpactFX)
	{
		UGameplayStatics::SpawnEmitterAtLocation(this, ImpactFX, GetActorLocation(), GetActorRotation());
	}

	// play sound
	USoundCue* ImpactSound = GetImpactSound(HitSurfaceType);
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, GetActorLocation());
	}

	if (DefaultDecal.DecalMaterial)
	{
		FRotator RandomDecalRotation = SurfaceHit.ImpactNormal.Rotation();
		RandomDecalRotation.Roll = FMath::FRandRange(-180.0f, 180.0f);

		UGameplayStatics::SpawnDecalAttached(DefaultDecal.DecalMaterial, FVector(1.0f, DefaultDecal.DecalSize, DefaultDecal.DecalSize),
			SurfaceHit.Component.Get(), SurfaceHit.BoneName,
			SurfaceHit.ImpactPoint, RandomDecalRotation, EAttachLocation::KeepWorldPosition,
			DefaultDecal.LifeSpan);
	}
}

UParticleSystem* ATrueFPSImpactEffect::GetImpactFX(TEnumAsByte<EPhysicalSurface> SurfaceType) const
{
	UParticleSystem* ImpactFX = nullptr;

	switch (SurfaceType)
	{
		case TRUEFPS_SURFACE_Concrete:	ImpactFX = ConcreteFX; break;
		case TRUEFPS_SURFACE_Dirt:		ImpactFX = DirtFX; break;
		case TRUEFPS_SURFACE_Water:		ImpactFX = WaterFX; break;
		case TRUEFPS_SURFACE_Metal:		ImpactFX = MetalFX; break;
		case TRUEFPS_SURFACE_Wood:		ImpactFX = WoodFX; break;
		case TRUEFPS_SURFACE_Grass:		ImpactFX = GrassFX; break;
		case TRUEFPS_SURFACE_Glass:		ImpactFX = GlassFX; break;
		case TRUEFPS_SURFACE_Flesh:		ImpactFX = FleshFX; break;
		default:						ImpactFX = DefaultFX; break;
	}

	return ImpactFX;
}

USoundCue* ATrueFPSImpactEffect::GetImpactSound(TEnumAsByte<EPhysicalSurface> SurfaceType) const
{
	USoundCue* ImpactSound = nullptr;

	switch (SurfaceType)
	{
		case TRUEFPS_SURFACE_Concrete:	ImpactSound = ConcreteSound; break;
		case TRUEFPS_SURFACE_Dirt:		ImpactSound = DirtSound; break;
		case TRUEFPS_SURFACE_Water:		ImpactSound = WaterSound; break;
		case TRUEFPS_SURFACE_Metal:		ImpactSound = MetalSound; break;
		case TRUEFPS_SURFACE_Wood:		ImpactSound = WoodSound; break;
		case TRUEFPS_SURFACE_Grass:		ImpactSound = GrassSound; break;
		case TRUEFPS_SURFACE_Glass:		ImpactSound = GlassSound; break;
		case TRUEFPS_SURFACE_Flesh:		ImpactSound = FleshSound; break;
		default:						ImpactSound = DefaultSound; break;
	}

	return ImpactSound;
}
