// Custom SceneViewExtension Template for Unreal Engine
// Copyright 2023 - 2024 Ossi Luoto
// 
// Custom SceneViewExtension implementation

#include "DatamoshPostprocess.h"
#include <ScreenPass.h>
#include <PostProcess/PostProcessMaterial.h>

IMPLEMENT_GLOBAL_SHADER(FCustomShader, "/Plugins/DatamoshPlugin/PostProcessCS.usf", "MainCS", SF_Compute);

TAutoConsoleVariable<bool> CVarShaderOn(TEXT("r.DoDatamosh"),
	false,
	TEXT("Toggles Datamoshing\n"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<bool> CVarFreezeFrame(TEXT("r.DatamoshFreeze"),
	false,
	TEXT(""),
	ECVF_RenderThreadSafe);


FCustomSceneViewExtension::FCustomSceneViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {
	UE_LOG(LogTemp, Log, TEXT("Datamosh Plugin: registered SceneViewExtension with renderer"));
}

// From engine v5.5, the subscribe to postprocessing pass takes FSceneView as input
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
void FCustomSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Define to what Post Processing stage to hook the SceneViewExtension into. See SceneViewExtension.h and PostProcessing.cpp for more info
	if (PassId == target_pass) {
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FCustomSceneViewExtension::CustomPostProcessing));
	}
}
#else
void FCustomSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Define to what Post Processing stage to hook the SceneViewExtension into. See SceneViewExtension.h and PostProcessing.cpp for more info
	if (PassId == target_pass) {
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FCustomSceneViewExtension::CustomPostProcessing));
	}
}
#endif

// We only bother to factor this out to keep the ifdefs out of other code.
FScreenPassTexture getTexture(FRDGBuilder& GraphBuilder, const FPostProcessMaterialInputs& Inputs, EPostProcessMaterialInput target_input) {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
	return FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(target_input));
#else
	return Inputs.Textures[(uint32)target_input];
#endif
}

FScreenPassTexture FCustomSceneViewExtension::CustomPostProcessing(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture& SceneColor = getTexture(GraphBuilder, Inputs, EPostProcessMaterialInput::SceneColor);
	const FScreenPassTexture& Velocity = getTexture(GraphBuilder, Inputs, EPostProcessMaterialInput::Velocity);

	// Cancel our custom pass based on the CVar
	if (!SceneColor.IsValid() || !CVarShaderOn.GetValueOnRenderThread()) {
		return SceneColor;
	}
	
	// SceneViewExtension gives SceneView, not ViewInfo so we need to setup some basics
	const FSceneViewFamily& ViewFamily = *SceneView.Family;
	const ERHIFeatureLevel::Type FeatureLevel = SceneView.GetFeatureLevel();

	// Here starts the RDG stuff
	RDG_EVENT_SCOPE(GraphBuilder, "Custom Postprocess Effect");
	{
		// Access point for our Shaders
		const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewFamily.GetFeatureLevel());

		// Setup all the descriptors to create a target texture
		FRDGTextureDesc OutputDesc;
		{
			OutputDesc = SceneColor.Texture->Desc;

			OutputDesc.Reset();
			OutputDesc.Flags |= TexCreate_UAV;
			OutputDesc.Flags &= ~(TexCreate_RenderTargetable | TexCreate_FastVRAM);

			OutputDesc.ClearValue = FClearValueBinding::Black;
		}

		// Set the shader parameters
		FCustomShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FCustomShader::FParameters>();

		// Get the input sizes (do note that viewport visible area might not be the full extent of the SceneColor texture
		// https://docs.unrealengine.com/5.1/en-US/screen-percentage-with-temporal-upscale-in-unreal-engine/
		const FIntRect PassViewSize = SceneColor.ViewRect;
		const FIntPoint SrcTextureSize = SceneColor.Texture->Desc.Extent;

		PassParameters->ViewportRect = PassViewSize;
		PassParameters->ViewportInvSize = FVector2f(1.0f / PassViewSize.Width(), 1.0f / PassViewSize.Height());

		// Conversion from the full texture to the actual used size
		// Refer to Screenpass.h to see how UE handles scaling of the different viewport sizes
		PassParameters->SceneColorUVScale = FVector2f(float(PassViewSize.Width()) / float(SrcTextureSize.X), float(PassViewSize.Height()) / float(SrcTextureSize.Y));

		// Method to setup common parameters, we use this to pass ViewUniformBuffer data
		// There is a lot of useful stuff in the ViewUniformBuffer, do note that when getting this from the SceneView, a lot them seem to be unpopulated
		FCommonShaderParameters CommonParameters;
		CommonParameters.ViewUniformBuffer = SceneView.ViewUniformBuffer;
		PassParameters->CommonParameters = CommonParameters;
		
		// Create target texture which will persist between frames.
		// See Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalAA.cpp.
		FRDGTextureRef outputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("Custom Effect Output Texture"), ERDGTextureFlags::MultiFrame);
		// Create UAV from Target Texture
		PassParameters->Output = GraphBuilder.CreateUAV(outputTexture);

		// Copy current framebuffer to output
		AddCopyTexturePass(GraphBuilder, SceneColor.Texture, outputTexture);
		
		if (CVarFreezeFrame.GetValueOnRenderThread() && historyBuffer != nullptr) {
			// Use the output from last frame as if it was the current framebuffer
			PassParameters->OriginalSceneColor = GraphBuilder.RegisterExternalTexture(historyBuffer);
		} else {
			PassParameters->OriginalSceneColor = SceneColor.Texture;
		}
		
		PassParameters->Velocity = Velocity.Texture;


		// Set Compute Shader and execute
		const int32 kDefaultGroupSize = 8;
		const FIntPoint GroupSize(kDefaultGroupSize, kDefaultGroupSize);
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize.Size(), GroupSize);

		const TShaderMapRef<FCustomShader> ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Custom SceneViewExtension Post Processing CS Shader %dx%d", PassViewSize.Width(), PassViewSize.Height()),
			ComputeShader,
			PassParameters,
			GroupCount);

		// Copy the output texture back to SceneColor
		// Returning the new texture as ScreenPassTexture doesn't work, so this is pretty fast alternative
		// Also with f.ex 'PrePostProcessPass_RenderThread' you get only input and something similar needs to be implemented then
		AddCopyTexturePass(GraphBuilder, outputTexture, SceneColor.Texture);

		// Keep around the current texture until next frame
		GraphBuilder.QueueTextureExtraction(outputTexture, &historyBuffer);
	}

	// The call expects ScreenPassTexture as a return, we return with the same texture as we started with, see AddCopyTexturePass above 
	return SceneColor;
}