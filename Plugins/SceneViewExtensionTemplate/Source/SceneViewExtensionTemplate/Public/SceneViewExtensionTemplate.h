#pragma once
// Custom SceneViewExtension Template for Unreal Engine
// Copyright 2023 - 2024 Ossi Luoto
// 
// Main Module to set Shader Directories

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

struct FSceneViewExtensionTemplate : IModuleInterface {
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};