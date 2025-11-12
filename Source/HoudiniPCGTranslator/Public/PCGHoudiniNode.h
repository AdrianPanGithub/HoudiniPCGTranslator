// Copyright Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "PCGSettings.h"
#include "PCGManagedResource.h"

#include "HoudiniEngineCommon.h"

#include "PCGHoudiniNode.generated.h"


class UPCGDataAsset;
class UHoudiniAsset;
class AHoudiniNode;


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGHoudiniNodeSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(2.0f, 0.2f, 0.0f); }

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TObjectPtr<UHoudiniAsset> Asset;

	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPCGPinProperties> Inputs;

	UPROPERTY(EditAnywhere, Category = Settings)
	TMap<FName, FHoudiniGenericParameter> Parameters;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

UCLASS(BlueprintType)
class UPCGHoudiniNodeManagedResource : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedResource interface
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	virtual bool ReleaseIfUnused(TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	virtual void MarkAsUsed() override;
	
	UPROPERTY(VisibleAnywhere, Category = GeneratedData)
	FName PCGNodeName;

	UPROPERTY(VisibleAnywhere, Category = GeneratedData)
	FName NodeActorName;

	mutable TWeakObjectPtr<AHoudiniNode> Node;

	UPROPERTY()
	TArray<TObjectPtr<UPCGDataAsset>> PCGDAs;
};

class FPCGHoudiniNodeElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
