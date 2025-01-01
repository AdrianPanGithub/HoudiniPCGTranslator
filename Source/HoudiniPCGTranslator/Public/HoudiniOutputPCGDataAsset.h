// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "HoudiniOutput.h"


class FHoudiniPCGDataAssetOutputBuilder : public IHoudiniOutputBuilder
{
public:
	virtual bool HapiIsPartValid(const int32& NodeId, const HAPI_PartInfo& PartInfo, bool& bOutIsValid, bool& bOutShouldHoldByOutput) override;

	virtual bool HapiRetrieve(AHoudiniNode* Node, const FString& OutputName, const HAPI_GeoInfo& GeoInfo, const TArray<HAPI_PartInfo>& PartInfos) override;
};
