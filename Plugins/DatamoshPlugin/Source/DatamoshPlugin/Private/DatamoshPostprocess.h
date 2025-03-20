#pragma once
// Custom SceneViewExtension Template for Unreal Engine
// Copyright 2023 - 2024 Ossi Luoto
// 
// Custom SceneViewExtension implementation

#include <CoreMinimal.h>
#include <RenderGraphUtils.h>
#include <SceneViewExtension.h>
#include <Runtime/Core/Public/Math/MathFwd.h>

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
#include <DataDrivenShaderPlatformInfo.h>
#endif

class FCustomSceneViewExtension : public FSceneViewExtensionBase {
public:
	FCustomSceneViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};

	// See SceneViewExtension.h for hooks to different stages of rendering
	// f.ex. PrePostProcessPass_RenderThread happens just when rendering is finished but PostProcessing hasn't started yet

	// The pass we want to hook into
	static const EPostProcessingPass target_pass = EPostProcessingPass::MotionBlur;

	// Simulated i-frame that persists across frames
	TRefCountPtr<IPooledRenderTarget> historyBuffer = nullptr;
	
	// This is the method to hook into PostProcessing pass. Engine v5.5 added a parameter so we have 2 signatures
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled);
#else
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled);
#endif

	// This is our callback during the rendering pass, called every frame we're active
	FScreenPassTexture CustomPostProcessing(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);
};

// Shader declarations

// Struct to include common parameters, useful when doing multiple shaders
BEGIN_SHADER_PARAMETER_STRUCT(FCommonShaderParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
END_SHADER_PARAMETER_STRUCT()

// Custom Post Process Shader
class DATAMOSHPLUGIN_API FCustomShader : public FGlobalShader {
public:
	DECLARE_GLOBAL_SHADER(FCustomShader)
		SHADER_USE_PARAMETER_STRUCT(FCustomShader, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_INCLUDE(FCommonShaderParameters, CommonParameters)
			SHADER_PARAMETER(FIntRect, ViewportRect)

			SHADER_PARAMETER(FVector2f, ViewportInvSize)
			SHADER_PARAMETER(FVector2f, SceneColorUVScale)
	
			SHADER_PARAMETER(FMatrix44f, curr_screen_to_world)
			SHADER_PARAMETER(FMatrix44f, prev_world_to_screen)

			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OriginalSceneColor)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Velocity)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTex)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, historyBuffer)
		END_SHADER_PARAMETER_STRUCT()

	// Basic shader stuff
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		// Needed for compute shader that can write back to a texture
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};