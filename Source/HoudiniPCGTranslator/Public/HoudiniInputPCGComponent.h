// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "HoudiniInput.h"


struct FPCGDataCollection;

class FHoudiniPCGComponentInput : public FHoudiniComponentInput
{
public:
	TArray<int32> NodeIds;

	virtual void Invalidate() const override {}  // Will then delete this, so we need NOT to empty NodeIds

	virtual bool HapiDestroy(UHoudiniInput* Input) const override;  // Will then delete this, so we need NOT to empty NodeIds

	// TODO: should use my shared memory input API like other input translators in my houdini engine, since the performance of HAPI is NOT ideal
	HOUDINIPCGTRANSLATOR_API static bool HapiRetreiveData(UHoudiniInput* Input, const UObject* InputObject,
		const FPCGDataCollection& Data, TArray<int32>& InOutNodeIds, int32& InOutDataIdx);
};

class FHoudiniPCGComponentInputBuilder : public IHoudiniComponentInputBuilder
{
public:
	virtual bool IsValidInput(const UActorComponent* Component) override;

	virtual bool HapiUpload(UHoudiniInput* Input, const bool& bIsSingleComponent,  // Is there only one single valid component in the whole blueprint/actor
		const TArray<const UActorComponent*>& Components, const TArray<FTransform>& Transforms, const TArray<int32>& ComponentIndices,  // Components and Transforms are all of the components in blueprint/actor, and ComponentIndices are ref the valid indices from IsValidInput
		int32& InOutInstancerNodeId, TArray<TSharedPtr<FHoudiniComponentInput>>& InOutComponentInputs, TArray<FHoudiniComponentInputPoint>& InOutPoints) override;

	virtual void AppendInfo(const TArray<const UActorComponent*>& Components, const TArray<FTransform>& Transforms,
		const TArray<int32>& ComponentIndices, const TSharedPtr<FJsonObject>& JsonObject) {};
};
