// Custom SceneViewExtension Template for Unreal Engine
// Copyright 2023 - 2024 Ossi Luoto
// 
// Subsystem to keep custom SceneViewExtension alive

#include "TemplateSubsystem.h"
#include "CustomSceneViewExtension.h"
#include "SceneViewExtension.h"

void UTemplateSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
	CustomSceneViewExtension = FSceneViewExtensions::NewExtension<FCustomSceneViewExtension>();
	UE_LOG(LogTemp, Log, TEXT("SceneViewExtensionTemplate: Subsystem initialized & SceneViewExtension created"));
}

void UTemplateSubsystem::Deinitialize() {
	// Make sure we never claim to be active after de-init.
	
	// Remove any callbacks that could report that we're active to the engine
	CustomSceneViewExtension->IsActiveThisFrameFunctions.Empty();

	// Add a stub function just in case
	FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
	IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension*, const FSceneViewExtensionContext&) {
		return TOptional<bool>(false);
	};
	CustomSceneViewExtension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);

	CustomSceneViewExtension.Reset();
	CustomSceneViewExtension = nullptr;
}