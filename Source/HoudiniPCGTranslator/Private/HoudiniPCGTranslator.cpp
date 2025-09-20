// Copyright Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniPCGTranslator.h"

#include "HoudiniEngine.h"

#include "HoudiniInputPCGDataAsset.h"
#include "HoudiniInputPCGComponent.h"
#include "HoudiniOutputPCGDataAsset.h"


#define LOCTEXT_NAMESPACE "FHoudiniPCGTranslatorModule"

void FHoudiniPCGTranslator::StartupModule()
{
	FHoudiniEngine& HoudiniEngine = FHoudiniEngine::IsLoaded() ? FHoudiniEngine::Get() :
		FModuleManager::LoadModuleChecked<FHoudiniEngine>("HoudiniEngine");

	ContentInputBuilder = MakeShared<FHoudiniPCGDataAssetInputBuilder>();
	HoudiniEngine.RegisterInputBuilder(ContentInputBuilder);

	ComponentInputBuilder = MakeShared<FHoudiniPCGComponentInputBuilder>();
	HoudiniEngine.RegisterInputBuilder(ComponentInputBuilder);

	OutputBuilder = MakeShared<FHoudiniPCGDataAssetOutputBuilder>();
	HoudiniEngine.RegisterOutputBuilder(OutputBuilder);
}

void FHoudiniPCGTranslator::ShutdownModule()
{
	if (FHoudiniEngine::IsLoaded())
	{
		FHoudiniEngine::Get().UnregisterInputBuilder(ContentInputBuilder);
		FHoudiniEngine::Get().UnregisterInputBuilder(ComponentInputBuilder);
		FHoudiniEngine::Get().UnregisterOutputBuilder(OutputBuilder);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FHoudiniPCGTranslator, HoudiniPCGTranslator)