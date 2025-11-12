// Copyright Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "PCGHoudiniNode.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "PCGComponent.h"
#include "PCGDataAsset.h"

#include "HoudiniEngine.h"
#include "HoudiniAsset.h"
#include "HoudiniNode.h"
#include "HoudiniInput.h"
#include "HoudiniInputPCGDataAsset.h"

#define LOCTEXT_NAMESPACE "PCGHoudiniNode"

#if WITH_EDITOR
FName UPCGHoudiniNodeSettings::GetDefaultNodeName() const
{
	return TEXT("HoudiniNode");
}

FText UPCGHoudiniNodeSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("HoudiniNode", "Houdini Node");
}

FText UPCGHoudiniNodeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("HoudiniNodeHelp", "Spawn Houdini Node and read inputs/parameters");
}

#endif // WITH_EDITOR

FPCGElementPtr UPCGHoudiniNodeSettings::CreateElement() const
{
	return MakeShared<FPCGHoudiniNodeElement>();
}

static const FName HoudiniParametersPin("Parameters");

TArray<FPCGPinProperties> UPCGHoudiniNodeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> InputPins = Inputs;
	FPCGPinProperties ParmPin(HoudiniParametersPin, EPCGDataType::Param);
	ParmPin.PinStatus = EPCGPinStatus::Advanced;
	InputPins.Add(ParmPin);
	
	return InputPins;
}

TArray<FPCGPinProperties> UPCGHoudiniNodeSettings::OutputPinProperties() const
{
	return { FPCGPinProperties("HoudiniNode", EPCGDataType::Param) };
}

AHoudiniNode* GetHoudiniNodeByName(const FName& NodeActorName)
{
	for (const TWeakObjectPtr<AHoudiniNode>& Node : FHoudiniEngine::Get().GetCurrentNodes())
	{
		if (Node.IsValid() && (Node->GetFName() == NodeActorName))
			return Node.Get();
	}

	return nullptr;
}

template<typename ValueType>
static void RetrieveHoudiniGenericParameter(const UPCGMetadata* Metadata, const FName& AttribName,
	const EHoudiniGenericParameterType& ParmType, const int32& TupleSize, TFunctionRef<void(const ValueType&, FHoudiniGenericParameter&)> Func,
	TMap<FName, FHoudiniGenericParameter>& InOutParms)
{
	if (const FPCGMetadataAttribute<ValueType>* Attrib = Metadata->GetConstTypedAttribute<ValueType>(AttribName))
	{
		FHoudiniGenericParameter GenericParm;
		GenericParm.Type = ParmType;
		GenericParm.Size = TupleSize;
		TArray<PCGMetadataValueKey> ValueKeys;
#if ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 5)) || (ENGINE_MAJOR_VERSION > 5)
		Attrib->GetValueKeys(TArray<const PCGMetadataEntryKey>{ 0 }, ValueKeys);
#else
		Attrib->GetValueKeys(TArray<PCGMetadataEntryKey>{ 0 }, ValueKeys);
#endif
		Func(Attrib->GetValue((ValueKeys[0] < 0) ? PCGDefaultValueKey : ValueKeys[0]), GenericParm);
		InOutParms.Add(AttribName, GenericParm);
	}
}

static bool HapiSetHoudiniPCGInput(AHoudiniNode* Node, UPCGHoudiniNodeManagedResource* NodeResource, FPCGContext* Context, const UPCGHoudiniNodeSettings* Settings)
{
	TArray<UPCGDataAsset*> PCGDAs;
	PCGDAs.SetNumZeroed(Settings->Inputs.Num());
	
	TMap<FName, FHoudiniGenericParameter> Preset = Settings->Parameters;

	int32 NumInputs = 0;
	for (const FPCGTaggedData& Input : Context->InputData.TaggedData)
	{
		if (Input.Pin == HoudiniParametersPin)
		{
			if (const UPCGParamData* ParamData = Cast<UPCGParamData>(Input.Data))
			{
				TArray<FName> AttribNames;
				TArray<EPCGMetadataTypes> AttribTypes;
				ParamData->Metadata->GetAttributes(AttribNames, AttribTypes);
				for (int32 AttribIdx = 0; AttribIdx < AttribNames.Num(); ++AttribIdx)
				{
					const FName& AttribName = AttribNames[AttribIdx];
					if (Preset.Contains(AttribName))
						continue;

					switch (AttribTypes[AttribIdx])
					{
					case EPCGMetadataTypes::Float: RetrieveHoudiniGenericParameter<float>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Float, 1,
						[](const float& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value; }, Preset); break;
					case EPCGMetadataTypes::Double: RetrieveHoudiniGenericParameter<double>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Float, 1,
						[](const double& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value; }, Preset); break;
					case EPCGMetadataTypes::Integer32: RetrieveHoudiniGenericParameter<int32>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Int, 1,
						[](const int32& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value; }, Preset); break;
					case EPCGMetadataTypes::Integer64: RetrieveHoudiniGenericParameter<int64>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Int, 1,
						[](const int64& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value; }, Preset); break;
					case EPCGMetadataTypes::Vector2: RetrieveHoudiniGenericParameter<FVector2d>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Float, 2,
						[](const FVector2d& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value.X; OutGenericParm.NumericValues.Y = Value.Y; }, Preset); break;
					case EPCGMetadataTypes::Vector: RetrieveHoudiniGenericParameter<FVector>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Float, 3,
						[](const FVector& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value.X; OutGenericParm.NumericValues.Y = Value.Y;  OutGenericParm.NumericValues.Z = Value.Z; }, Preset); break;
					case EPCGMetadataTypes::Vector4: RetrieveHoudiniGenericParameter<FVector4>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Float, 4,
						[](const FVector4& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value.X; OutGenericParm.NumericValues.Y = Value.Y;  OutGenericParm.NumericValues.Z = Value.Z;  OutGenericParm.NumericValues.W = Value.W; }, Preset); break;
					case EPCGMetadataTypes::Quaternion: RetrieveHoudiniGenericParameter<FQuat>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Float, 4,
						[](const FQuat& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value.X; OutGenericParm.NumericValues.Y = Value.Y;  OutGenericParm.NumericValues.Z = Value.Z;  OutGenericParm.NumericValues.W = Value.W; }, Preset); break;
					case EPCGMetadataTypes::Transform: break;
					case EPCGMetadataTypes::String: RetrieveHoudiniGenericParameter<FString>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::String, 1,
						[](const FString& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.StringValue = Value; }, Preset); break;
					case EPCGMetadataTypes::Boolean: RetrieveHoudiniGenericParameter<bool>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Int, 1,
						[](const bool& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = int32(Value); }, Preset); break;
					case EPCGMetadataTypes::Rotator: RetrieveHoudiniGenericParameter<FRotator>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::Float, 3,
						[](const FRotator& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.NumericValues.X = Value.Pitch; OutGenericParm.NumericValues.Y = Value.Yaw;  OutGenericParm.NumericValues.Z = Value.Roll; }, Preset); break;
					case EPCGMetadataTypes::Name: RetrieveHoudiniGenericParameter<FName>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::String, 1,
						[](const FName& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.StringValue = Value.ToString(); }, Preset); break;
					case EPCGMetadataTypes::SoftObjectPath: RetrieveHoudiniGenericParameter<FSoftObjectPath>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::String, 1,
						[](const FSoftObjectPath& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.ObjectValue = Value; }, Preset); break;
					case EPCGMetadataTypes::SoftClassPath: RetrieveHoudiniGenericParameter<FSoftClassPath>(ParamData->Metadata, AttribName, EHoudiniGenericParameterType::String, 1,
						[](const FSoftClassPath& Value, FHoudiniGenericParameter& OutGenericParm) { OutGenericParm.ObjectValue = Value; }, Preset); break;
					}
				}
			}
		}
		else
		{
			int32 PinIdx = Settings->Inputs.IndexOfByPredicate([&](const FPCGPinProperties& Pin) { return Pin.Label == Input.Pin; });
			if (!Settings->Inputs.IsValidIndex(PinIdx))
				PinIdx = 0;

			UPCGDataAsset*& PCGDA = PCGDAs[PinIdx];
			if (!PCGDA)
			{
				if (NodeResource->PCGDAs.IsValidIndex(NumInputs))
					PCGDA = NodeResource->PCGDAs[NumInputs];
				else
				{
					PCGDA = NewObject<UPCGDataAsset>(NodeResource);
					NodeResource->PCGDAs.Add(PCGDA);
				}
			}
#if ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 5)) || (ENGINE_MAJOR_VERSION > 5)
			PCGDA->Data.AddData(Input, FPCGCrc());
#else
			PCGDA->Data.AddData(TArray<FPCGTaggedData>{ Input }, TArray<FPCGCrc>{ FPCGCrc() });
#endif
			++NumInputs;
		}
	}
	if (NodeResource->PCGDAs.Num() > Settings->Inputs.Num())
		NodeResource->PCGDAs.SetNum(Settings->Inputs.Num());

	if (!Preset.IsEmpty())
		Node->SetParameterValues(Preset);

	TArray<UHoudiniInput*> NodeInputs;
	for (UHoudiniInput* Input : Node->GetInputs())
	{
		if (!Input->IsParameter())
			NodeInputs.Add(Input);
	}

	for (int32 InputIdx = 0; InputIdx < PCGDAs.Num(); ++InputIdx)
	{
		if (!IsValid(PCGDAs[InputIdx]))
		{
			if (NodeInputs.IsValidIndex(InputIdx))
				HOUDINI_FAIL_RETURN(NodeInputs[InputIdx]->HapiDestroy());
			continue;
		}

		const FName& InputName = Settings->Inputs[InputIdx].Label;
		if (NodeInputs.IsValidIndex(InputIdx))
		{
			// Convert Input to PCG Input
			UHoudiniInput* Input = NodeInputs[InputIdx];
			Input->SetType(EHoudiniInputType::Content);
			UHoudiniInputPCGDataAsset* PCGDAInput = nullptr;
			for (int32 HolderIdx = Input->Holders.Num() - 1; HolderIdx >= 0; --HolderIdx)
			{
				if (IsValid(Input->Holders[HolderIdx]))
				{
					if (!PCGDAInput)
						PCGDAInput = Cast<UHoudiniInputPCGDataAsset>(Input->Holders[HolderIdx]);
					if (!PCGDAInput)
						HOUDINI_FAIL_RETURN(Input->HapiRemoveHolder(HolderIdx))
				}
			}
			if (PCGDAInput)
			{
				if (PCGDAInput->GetObject() != PCGDAs[InputIdx])
					PCGDAInput->SetAsset(PCGDAs[InputIdx]);
				else
					PCGDAInput->RequestReimport();
			}
			else
				PCGDAInput = (UHoudiniInputPCGDataAsset*)IHoudiniContentInputBuilder::CreateOrUpdateHolder<UPCGDataAsset, UHoudiniInputPCGDataAsset>(
					Input, PCGDAs[InputIdx], nullptr);
			Input->Holders = TArray<UHoudiniInputHolder*>{ PCGDAInput };
		}
		else
		{
			// Create a new input
			UHoudiniInput* Input = NewObject<UHoudiniInput>(Node);
			Input->UpdateNodeInput(InputIdx, InputName.IsNone() ? FString() : InputName.ToString());
			if (Input->Holders.IsValidIndex(0))
				Input->Holders[0] = IHoudiniContentInputBuilder::CreateOrUpdateHolder<UPCGDataAsset, UHoudiniInputPCGDataAsset>(
					Input, PCGDAs[InputIdx], nullptr);
			else
				Input->Holders.Add(IHoudiniContentInputBuilder::CreateOrUpdateHolder<UPCGDataAsset, UHoudiniInputPCGDataAsset>(
					Input, PCGDAs[InputIdx], nullptr));

			((TArray<TObjectPtr<UHoudiniInput>>*)(&Node->GetInputs()))->Add(Input);
		}
	}

	return true;
}

bool FPCGHoudiniNodeElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGHoudiniNodeElement::Execute);

	check(Context);

	const UPCGHoudiniNodeSettings* Settings = Context->GetInputSettings<UPCGHoudiniNodeSettings>();
	check(Settings);

	if (!IsValid(Settings->Asset))
		return true;

	AHoudiniNode* Node = nullptr;
	UPCGHoudiniNodeManagedResource* NodeResource = nullptr;
#if ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 6)) || (ENGINE_MAJOR_VERSION > 5)
	UPCGComponent* SourceComponent = Cast<UPCGComponent>(Context->ExecutionSource.Get());
#else
	UPCGComponent* SourceComponent = Cast<UPCGComponent>(Context->SourceComponent.Get());
#endif
	SourceComponent->ForEachManagedResource([&](UPCGManagedResource* Resource)
		{
			if (UPCGHoudiniNodeManagedResource* HoudiniNodeResource = Cast<UPCGHoudiniNodeManagedResource>(Resource))
			{
				if (HoudiniNodeResource->PCGNodeName == Context->Node->GetFName())
				{
					NodeResource = HoudiniNodeResource;
					Node = GetHoudiniNodeByName(HoudiniNodeResource->NodeActorName);
				}
			}
		});

	if (NodeResource)
	{
		for (UPCGDataAsset* PCGDA : NodeResource->PCGDAs)
			PCGDA->Data.Reset();
	}
	else
#if ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 5)) || (ENGINE_MAJOR_VERSION > 5)
		NodeResource = FPCGContext::NewObject_AnyThread<UPCGHoudiniNodeManagedResource>(Context, SourceComponent);
#else
		NodeResource = NewObject<UPCGHoudiniNodeManagedResource>(SourceComponent);
#endif

	if (IsInGameThread())
	{
		if (Node)
		{
			HOUDINI_FAIL_INVALIDATE(HapiSetHoudiniPCGInput(Node, NodeResource, Context, Settings));
			if (Node->GetAsset() != Settings->Asset)
				Node->Initialize(Settings->Asset);
			else
				Node->RequestCook();
		}
		else
		{
			Node = AHoudiniNode::Create(SourceComponent->GetWorld(), Settings->Asset);
			NodeResource->PCGNodeName = Context->Node->GetFName();
			NodeResource->NodeActorName = Node->GetFName();
			NodeResource->Node = Node;
			SourceComponent->AddToManagedResources(NodeResource);
			HOUDINI_FAIL_INVALIDATE(HapiSetHoudiniPCGInput(Node, NodeResource, Context, Settings));
		}
	}
#if ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 5)) || (ENGINE_MAJOR_VERSION > 5)
	else
	{
		FDelegateHandle OnCookFinishedHandle;
		bool bFinished = false;
		if (Node)
		{
			AsyncTask(ENamedThreads::GameThread, [Context, Settings, NodeResource, &OnCookFinishedHandle, &bFinished, Node]
				{
					HOUDINI_FAIL_INVALIDATE(HapiSetHoudiniPCGInput(Node, NodeResource, Context, Settings));
					if (Node->GetAsset() != Settings->Asset)
						Node->Initialize(Settings->Asset);
					else
						Node->RequestCook();
					OnCookFinishedHandle = FHoudiniEngine::Get().HoudiniNodeEvents.AddLambda(
						[Node, &bFinished, &OnCookFinishedHandle](AHoudiniNode* CurrNode, const EHoudiniNodeEvent Event)
						{
							if ((CurrNode == Node) && (Event == EHoudiniNodeEvent::FinishCook))
							{
								FHoudiniEngine::Get().HoudiniNodeEvents.Remove(OnCookFinishedHandle);
								bFinished = true;
							}
						});
				});

			for (;;)
			{
				FPlatformProcess::SleepNoStats(0.05f);

				if (bFinished)
				{
					UE_LOG(LogHoudiniEngine, Log, TEXT("PCG Houdini Node Finished"));
					break;
				}
			}
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [Context, NodeResource, Settings,
				&OnCookFinishedHandle, &bFinished, &Node, SourceComponent]
				{
					Node = AHoudiniNode::Create(SourceComponent->GetWorld(), Settings->Asset);
					OnCookFinishedHandle = FHoudiniEngine::Get().HoudiniNodeEvents.AddLambda(
						[Node, &bFinished, &OnCookFinishedHandle](AHoudiniNode* CurrNode, const EHoudiniNodeEvent Event)
						{
							if ((CurrNode == Node) && (Event == EHoudiniNodeEvent::FinishCook))
							{
								FHoudiniEngine::Get().HoudiniNodeEvents.Remove(OnCookFinishedHandle);
								bFinished = true;
							}
						});

					HOUDINI_FAIL_INVALIDATE(HapiSetHoudiniPCGInput(Node, NodeResource, Context, Settings));
				});

			for (;;)
			{
				FPlatformProcess::SleepNoStats(0.05f);

				if (bFinished)
				{
					NodeResource->PCGNodeName = Context->Node->GetFName();
					NodeResource->NodeActorName = Node->GetFName();
					NodeResource->Node = Node;
					SourceComponent->AddToManagedResources(NodeResource);

					UE_LOG(LogHoudiniEngine, Log, TEXT("PCG Houdini Node Finished"));
					break;
				}
			}
		}
	}


	UPCGParamData* OutputData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
#else
	UPCGParamData* OutputData = NewObject<UPCGParamData>(SourceComponent);
#endif
	OutputData->Metadata->AddEntry();
	FPCGMetadataAttribute<FSoftObjectPath>* Attrib = OutputData->Metadata->CreateAttribute<FSoftObjectPath>("HoudiniNode", FSoftObjectPath(), true, true);
	Attrib->SetValue(0, FSoftObjectPath(Node));
	FPCGTaggedData Output;
	Output.Data = OutputData;
	Context->OutputData.TaggedData.Add(Output);

	return true;
}


UE_DISABLE_OPTIMIZATION
bool UPCGHoudiniNodeManagedResource::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	return false;
}

bool UPCGHoudiniNodeManagedResource::ReleaseIfUnused(TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	return false;
}

void UPCGHoudiniNodeManagedResource::MarkAsUsed()
{
	Super::MarkAsUsed();
}
UE_ENABLE_OPTIMIZATION
#undef LOCTEXT_NAMESPACE
