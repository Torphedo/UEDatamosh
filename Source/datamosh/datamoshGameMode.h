#pragma once
// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/ConstructorHelpers.h"
#include "datamoshGameMode.generated.h"

UCLASS(minimalapi)
class AdatamoshGameMode : public AGameModeBase {
	GENERATED_BODY()

public:
	// Very simple constructor, we can just define it here
	AdatamoshGameMode() {
		// set default pawn class to our Blueprinted character
		static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
		if (PlayerPawnBPClass.Class != nullptr) {
			DefaultPawnClass = PlayerPawnBPClass.Class;
		}
	}
};