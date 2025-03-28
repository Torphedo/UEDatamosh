// Copyright 2023 - 2024 Ossi Luoto, 2025 Torphedo
// Implementation of datamoshing postprocess pass

#include "DatamoshPostprocess.h"
#include <ScreenPass.h>
#include <PostProcess/PostProcessMaterial.h>

IMPLEMENT_GLOBAL_SHADER(FCustomShader, "/Plugins/DatamoshPlugin/PostProcessCS.usf", "MainCS", SF_Compute);

TAutoConsoleVariable CVarShaderOn(TEXT("r.DoDatamosh"),
	false,
	TEXT("Toggles Datamoshing\n"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable CVarFreezeFrame(TEXT("r.DatamoshFreeze"),
	false,
	TEXT(""),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable CVarColorInterpolate(TEXT("r.FrameInterpolate.doColor"),
	false,
	TEXT(""),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable CVarFreezeInterval(TEXT("r.DatamoshFreezeInterval"),
	1,
	TEXT(""),
	ECVF_RenderThreadSafe);


FCustomSceneViewExtension::FCustomSceneViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {
	UE_LOG(LogTemp, Log, TEXT("Datamosh Plugin: registered SceneViewExtension with renderer"));
}

// Engine v5.5 added an extra parameter, so we maintain 2 different signatures. Since we don't use the new parameter,
// the body remains the same.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	void FCustomSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
#else
	void FCustomSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
#endif
{
	// The engine rendering pipeline calls us at every stage of every frame, and we register our custom pass callback
	// for the stage we want to inject into.
	// See SceneViewExtension.h and PostProcessing.cpp for more info

	// When the CVar controlling the shader is off, don't even bother registering a callback.
	if (PassId == target_pass && CVarShaderOn.GetValueOnRenderThread()) {
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FCustomSceneViewExtension::CustomPostProcessing));
	}
}

FScreenPassTexture FCustomSceneViewExtension::CustomPostProcessing(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs)
{
	// We save the current & previous frames' inverse MVP matrix, so we can transform each pixel location in the shader
	// into a world space position. Then we can find the difference to find out the screen space movement in the last
	// frame.
	
	static FMatrix44f prev_world_to_screen = FMatrix44f().Identity;
	FMatrix44f cur_screen_to_world = FMatrix44f(SceneView.ViewMatrices.GetInvViewProjectionMatrix());
	
	// This had been ifdef'd behind engine version >= 5.4, but the function seems to have existed since at least v5.0
	// (according to the docs). If you get a crash here, try getting the texture like: Inputs.Textures[target_input]
	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	const FScreenPassTexture& Velocity = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::Velocity));

	// Start building render graph for our custom pass
	RDG_EVENT_SCOPE(GraphBuilder, "Datamoshing Pass");
	{
		// Get access point for our shaders
		const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(SceneView.Family->GetFeatureLevel());

		// Setup target texture descriptors
		FRDGTextureDesc OutputDesc;
		{
			OutputDesc = SceneColor.Texture->Desc;

			// TODO: There's no way a C++ codebase this massive doesn't have a wrapper to make bitmask flags easier
			// to work with. Find their flag utility functions and use them here.
			OutputDesc.Reset();
			OutputDesc.Flags |= TexCreate_UAV; // Needed for arbitrary writes in the compute shader
			OutputDesc.Flags &= ~(TexCreate_RenderTargetable | TexCreate_FastVRAM); // Unset these flags

			OutputDesc.ClearValue = FClearValueBinding::Transparent;
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
		
		// Create target texture and a history buffer which will persist between frames.
		// See Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalAA.cpp, we use the same technique to keep
		// a history buffer around.
		FRDGTextureRef history = nullptr;
		// We have to go through a fairly deep tree of structs to access scene depth
		FRDGTextureRef depthTex = Inputs.SceneTextures.SceneTextures->GetContents()->SceneDepthTexture;

		if (CVarFreezeFrame.GetValueOnRenderThread() && historyBuffer != nullptr) {
			// While frozen, keep the old texture around
			history = GraphBuilder.RegisterExternalTexture(historyBuffer);
		} else {
			// While we're unfrozen, keep history buffer synced with framebuffer
			history = GraphBuilder.CreateTexture(OutputDesc, TEXT("Datamosh Historical Framebuffer"), ERDGTextureFlags::MultiFrame);
			AddCopyTexturePass(GraphBuilder, SceneColor.Texture, history);
		}
		PassParameters->OriginalSceneColor = CVarColorInterpolate.GetValueOnRenderThread() ? SceneColor.Texture : history;
		
		// Create UAV from target texture
        PassParameters->historyBuffer = GraphBuilder.CreateUAV(history);
		
		PassParameters->Velocity = Velocity.Texture;
		PassParameters->DepthTex = depthTex;

		PassParameters->curr_screen_to_world = cur_screen_to_world;
		PassParameters->prev_world_to_screen = prev_world_to_screen;

		// Set Compute Shader and execute
		const int32 kDefaultGroupSize = 8;
		const FIntPoint GroupSize(kDefaultGroupSize, kDefaultGroupSize);
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize.Size(), GroupSize);

		const TShaderMapRef<FCustomShader> ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Custom Datamoshing Compute Shader %dx%d", PassViewSize.Width(), PassViewSize.Height()),
			ComputeShader,
			PassParameters,
			GroupCount);

		// Copy the output texture back to SceneColor
		// Returning the new texture as ScreenPassTexture doesn't work, so this is pretty fast alternative
		// Also with f.ex 'PrePostProcessPass_RenderThread' you get only input and something similar needs to be implemented then
		AddCopyTexturePass(GraphBuilder, history, SceneColor.Texture);
		
        GraphBuilder.QueueTextureExtraction(history, &historyBuffer);
		// Save view-projection matrix for next frame
		prev_world_to_screen = FMatrix44f(SceneView.ViewMatrices.GetViewProjectionMatrix());
	}

	// The call expects ScreenPassTexture as a return, we return with the same texture as we started with, see AddCopyTexturePass above 
	return SceneColor;
}