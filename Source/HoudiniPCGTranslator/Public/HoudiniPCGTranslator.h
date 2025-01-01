// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"


class FHoudiniPCGDataAssetInputBuilder;
class FHoudiniPCGComponentInputBuilder;
class FHoudiniPCGDataAssetOutputBuilder;

class FHoudiniPCGTranslator : public IModuleInterface
{
protected:
	TSharedPtr<FHoudiniPCGDataAssetInputBuilder> ContentInputBuilder;
	TSharedPtr<FHoudiniPCGComponentInputBuilder> ComponentInputBuilder;
	TSharedPtr<FHoudiniPCGDataAssetOutputBuilder> OutputBuilder;

public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
