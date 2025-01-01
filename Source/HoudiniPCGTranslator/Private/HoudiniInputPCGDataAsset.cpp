// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniInputPCGDataAsset.h"

#include "HoudiniApi.h"
#include "HoudiniEngine.h"
#include "HoudiniAttribute.h"
#include "HoudiniEngineUtils.h"

#include "HoudiniInputPCGComponent.h"

#include "PCGDataAsset.h"


void FHoudiniPCGDataAssetInputBuilder::AppendAllowClasses(TArray<const UClass*>& InOutAllowclasses)
{
	InOutAllowclasses.Add(UPCGDataAsset::StaticClass());
}

UHoudiniInputHolder* FHoudiniPCGDataAssetInputBuilder::CreateOrUpdate(UHoudiniInput* Input, UObject* Asset, UHoudiniInputHolder* OldHolder)
{
	return CreateOrUpdateHolder<UPCGDataAsset, UHoudiniInputPCGDataAsset>(Input, Asset, OldHolder);
}

void UHoudiniInputPCGDataAsset::SetAsset(UPCGDataAsset* NewPCGDataAsset)  // Used by IHoudiniContentInputBuilder::CreateOrUpdateHolder
{
	if (PCGDataAsset != NewPCGDataAsset)
	{
		PCGDataAsset = NewPCGDataAsset;
		RequestReimport();
	}
}

TSoftObjectPtr<UObject> UHoudiniInputPCGDataAsset::GetObject() const
{
	return PCGDataAsset;
}

bool UHoudiniInputPCGDataAsset::IsObjectExists() const
{
	return IsValid(PCGDataAsset.LoadSynchronous());
}

bool UHoudiniInputPCGDataAsset::HapiUpload()
{
	UPCGDataAsset* PCGDA = PCGDataAsset.LoadSynchronous();
	if (!IsValid(PCGDA))
		return HapiDestroy();

	int32 NumDatas = 0;
	HOUDINI_FAIL_RETURN(FHoudiniPCGComponentInput::HapiRetreiveData(GetInput(), PCGDA, PCGDA->Data, NodeIds, NumDatas));

	for (int32 NodeIdx = NodeIds.Num() - 1; NodeIdx >= NumDatas; --NodeIdx)
	{
		const int32& NodeId = NodeIds[NodeIdx];
		if (NodeId >= 0)
		{
			GetInput()->NotifyMergedNodeDestroyed();
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::DeleteNode(FHoudiniEngine::Get().GetSession(), NodeId));
		}
		NodeIds.Pop();
	}

	bHasChanged = false;

	return true;
}

bool UHoudiniInputPCGDataAsset::HapiDestroy()
{
	for (const int32& NodeId : NodeIds)
	{
		if (NodeId >= 0)
		{
			GetInput()->NotifyMergedNodeDestroyed();
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::DeleteNode(FHoudiniEngine::Get().GetSession(), NodeId));
		}
	}

	Invalidate();

	return true;
}

void UHoudiniInputPCGDataAsset::Invalidate()
{
	NodeIds.Empty();
}
