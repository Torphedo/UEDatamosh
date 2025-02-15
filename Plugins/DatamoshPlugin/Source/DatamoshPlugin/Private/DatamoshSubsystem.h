#pragma once
// Copyright 2023 - 2024 Ossi Luoto

#include <CoreMinimal.h>
#include <Subsystems/EngineSubsystem.h>
#include "DatamoshSubsystem.generated.h"

// We need to register an engine subsystem with Unreal to keep our rendering extension alive. Even though this file
// isn't referenced by any other code, the shader won't be applied if it's deleted. It seems like the static declaration
// here causes it to be automatically registered with the engine.

UCLASS()
class UTemplateSubsystem : public UEngineSubsystem {
	GENERATED_BODY()
public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	TSharedPtr<class FCustomSceneViewExtension, ESPMode::ThreadSafe> CustomSceneViewExtension;
};