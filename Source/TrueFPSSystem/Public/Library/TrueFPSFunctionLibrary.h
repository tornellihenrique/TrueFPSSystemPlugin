// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstanceProxy.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Net/RepLayout.h"
#include "TrueFPSFunctionLibrary.generated.h"

#define PRINT(...) if(GEngine) GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.f, FColor::White, FString::Printf(##__VA_ARGS__))
#define PRINTAUTH(...) if(GEngine) GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.f, FColor::White, AUTH + ": " + FString::Printf(##__VA_ARGS__))
#define PRINT_COLOR(Color, ...) if(GEngine) GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.f, Color, FString::Printf(##__VA_ARGS__))
#define AUTHTOSTRING(bAuth) FString(bAuth ? "Server" : "Client")
#define AUTH AUTHTOSTRING(HasAuthority())
#define PRINTLINE PRINT(TEXT("%s"), *FString(__FUNCTION__))
#define PRINTLINEAUTH PRINT(TEXT("%s: %s"), *AUTH, *FString(__FUNCTION__))

// Default Object: GetClass()->GetDefaultObject<ThisClass>()
#define DEFOBJ GetClass()->GetDefaultObject<ThisClass>()

#define CONCATE_(X, Y) X##Y
#define CONCATE(X, Y) CONCATE_(X, Y)

// Pre-requisite to calling ACCESS_PRIVATE_MEMBER to be able to access a private member. CLASS, MEMBER, TYPES
#define ALLOW_PRIVATE_ACCESS(CLASS, MEMBER, ...) \
	template<typename Only, __VA_ARGS__ CLASS::*Member> \
	struct CONCATE(MEMBER, __LINE__) { friend __VA_ARGS__ CLASS::*Access(Only*) { return Member; } }; \
	template<typename> struct Only_##MEMBER; \
	template<> struct Only_##MEMBER<CLASS> { friend __VA_ARGS__ CLASS::*Access(Only_##MEMBER<CLASS>*); }; \
	template struct CONCATE(MEMBER, __LINE__)<Only_##MEMBER<CLASS>, &CLASS::MEMBER>

// Access private member from an object. Must call ALLOW_PRIVATE_ACCESS in global-scope first
#define ACCESS_PRIVATE_MEMBER(OBJECT, MEMBER) \
	((OBJECT).*Access((Only_##MEMBER<std::remove_reference<decltype(OBJECT)>::type>*)nullptr))

UENUM(BlueprintType)
enum class EBoneSpaceTransform : uint8
{
	ParentBoneSpace		UMETA(DisplayName = "Parent Bone-Space"),
	WorldSpace			UMETA(DisplayName = "World-Space"),
};


DECLARE_DYNAMIC_DELEGATE(FPlaceholderDelegate);



ALLOW_PRIVATE_ACCESS(FAnimInstanceProxy, PoseSnapshots, TArray<FPoseSnapshot>);
class UAnimInstanceAccessor : public UAnimInstance { friend class UTrueFPSFunctionLibrary; };

ALLOW_PRIVATE_ACCESS(FRepLayout, Parents, TArray<FRepParentCmd>);

/**
 * 
 */
UCLASS()
class TRUEFPSSYSTEM_API UTrueFPSFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	template<typename EnumType>
	static FORCEINLINE FString EnumValueToString(const EnumType EnumValue)
	{
		static_assert(TIsEnum<EnumType>::Value, TEXT("EnumValueToString: Input is not an enum type"));
		return StaticEnum<EnumType>()->GetNameStringByIndex((int32)EnumValue);
	}

	template<typename EnumType>
	static FORCEINLINE FName EnumValueName(const EnumType EnumValue)
	{
		return FName(EnumValueToString<EnumType>(EnumValue));
	}

	// Returns nullptr if AnimInstance or AnimInstanceProxy are invalid. Direct access.
	static FORCEINLINE TArray<FPoseSnapshot>* GetPoseSnapshots(class UAnimInstance* AnimInstance)
	{
		if (IsValid(AnimInstance))
			if (FAnimInstanceProxy* Proxy = UAnimInstanceAccessor::GetProxyOnGameThreadStatic<FAnimInstanceProxy>(AnimInstance))
				return &ACCESS_PRIVATE_MEMBER(*Proxy, PoseSnapshots);// Black-magic to access private-member variable
		return nullptr;
	}

	// Caches pose snapshot within the passed in AnimInstance that can be accessed within it using the Pose Snapshot node. If a
	// pose snapshot has already been cached with the same name this will override it with the new pose snapshot.
	UFUNCTION(BlueprintCallable, Meta = (AutoCreateRefTerm = "SnapshotName"), Category = "TrueFPS Function Library|Animation")
	static void CachePoseSnapshot(const FPoseSnapshot& PoseSnapshot, class UAnimInstance* AnimInstance, const FName& SnapshotName)
	{
		if (!AnimInstance || !PoseSnapshot.bIsValid)
		{
			if (!AnimInstance) { UE_LOG(LogTemp, Error, TEXT("%s: AnimInstance is invalid"), *FString(__FUNCTION__)); }
			if (!PoseSnapshot.bIsValid) { UE_LOG(LogTemp, Error, TEXT("%s: PoseSnapshot is invalid"), *FString(__FUNCTION__)); }
			return;
		}
		
		TArray<FPoseSnapshot>* ProxySnapshots = GetPoseSnapshots(AnimInstance);
		if (!ProxySnapshots) return;
		
		FPoseSnapshot* Snapshot = ProxySnapshots->FindByPredicate([SnapshotName](const FPoseSnapshot& Snapshot)->bool { return Snapshot.SnapshotName == SnapshotName; });
		if (!Snapshot)
			Snapshot = &(*ProxySnapshots)[ProxySnapshots->AddDefaulted()];
		
		*Snapshot = PoseSnapshot;
		Snapshot->SnapshotName = SnapshotName;
	}

	static const FPoseSnapshot EmptyPose;
};
