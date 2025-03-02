// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "HoudiniInput.h"

#include "HoudiniInputPCGDataAsset.generated.h"


class UPCGDataAsset;

UCLASS()
class UHoudiniInputPCGDataAsset : public UHoudiniInputHolder
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	TSoftObjectPtr<UPCGDataAsset> PCGDataAsset;

	TArray<int32> NodeIds;

public:
	void SetAsset(UPCGDataAsset* NewPCGDataAsset);  // Used by IHoudiniContentInputBuilder::CreateOrUpdateHolder, must have a method name called "SetAsset"

	virtual TSoftObjectPtr<UObject> GetObject() const override;

	virtual bool IsObjectExists() const override;

	virtual bool HapiUpload() override;

	virtual bool HapiDestroy() override;

	virtual void Invalidate() override;
};

class FHoudiniPCGDataAssetInputBuilder : public IHoudiniContentInputBuilder
{
public:
	virtual void AppendAllowClasses(TArray<const UClass*>& InOutAllowClasses) override;

	virtual UHoudiniInputHolder* CreateOrUpdate(UHoudiniInput* Input, UObject* Asset, UHoudiniInputHolder* OldHolder) override;

	//virtual bool GetInfo(const UObject* Asset, FString& OutInfoStr) override;
};
