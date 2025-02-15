#pragma once
// Custom SceneViewExtension Template for Unreal Engine
// Copyright 2023 - 2024 Ossi Luoto
// 
// Subsystem to keep custom SceneViewExtension alive

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "TemplateSubsystem.generated.h"

 UCLASS()
class UTemplateSubsystem : public UEngineSubsystem {
	GENERATED_BODY()
public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	TSharedPtr<class FCustomSceneViewExtension, ESPMode::ThreadSafe> CustomSceneViewExtension;
};