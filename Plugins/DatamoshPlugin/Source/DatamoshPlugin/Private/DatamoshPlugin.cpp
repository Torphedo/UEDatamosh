// Custom SceneViewExtension Template for Unreal Engine
// Copyright 2023 - 2024 Ossi Luoto
// 
// Main Module to set Shader Directories

#include "DatamoshPlugin.h"
#include <Interfaces/IPluginManager.h>

#define LOCTEXT_NAMESPACE "DatamoshPlugin"

void FDatamoshPlugin::StartupModule() {
	// Set up the Shader Directories
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("DatamoshPlugin"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugins/DatamoshPlugin"), PluginShaderDir);
}

void FDatamoshPlugin::ShutdownModule() {
	// We don't need to do anything...
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDatamoshPlugin, DatamoshPlugin);