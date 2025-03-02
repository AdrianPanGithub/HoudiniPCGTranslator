// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniInputPCGComponent.h"

#include "HoudiniApi.h"
#include "HoudiniEngine.h"
#include "HoudiniAttribute.h"
#include "HoudiniEngineUtils.h"

#include "HoudiniPCGCommon.h"

#include "PCGComponent.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "Data/PCGDynamicMeshData.h"
#include "UDynamicMesh.h"
#endif


bool FHoudiniPCGComponentInput::HapiDestroy(UHoudiniInput* Input) const  // Will then delete this, so we need NOT to reset node ids to -1
{
	for (const int32& NodeId : NodeIds)
	{
		if (NodeId >= 0)
		{
			Input->NotifyMergedNodeDestroyed();
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::DeleteNode(FHoudiniEngine::Get().GetSession(), NodeId));
		}
	}

	// Will then delete this, so we need NOT to empty NodeIds

	return true;
}

bool FHoudiniPCGComponentInputBuilder::IsValidInput(const UActorComponent* Component)
{
	return IsValid(Component) && (Component->IsA<UPCGComponent>() ||
		Component->GetClass()->GetFName() == FName("PCGProceduralISMComponent"));  // PCG ISMC does NOT support, so we should collect it, but skip it later
}




namespace HoudiniPCGDataInputUtils
{
	template<typename StrValueType>
	static bool HapiUploadStringAttribValue(const UPCGMetadata* MetaData, const FName& AttribName,
		const int32& NodeId, HAPI_AttributeInfo& AttribInfo, TFunctionRef<FString(const StrValueType&)> ConvertFunc);

	template<typename ValueType, typename HapiValueType, int TupleSize, HAPI_StorageType Storage, HAPI_AttributeTypeInfo AttribType,
		typename SetUniqueAttribValueHapi, typename SetAttribValueHapi>
	static bool HapiUploadNumericAttribValue(const UPCGMetadata* MetaData, const FName& AttribName,
		const int32& NodeId, HAPI_AttributeInfo& AttribInfo, TFunctionRef<void(const ValueType&, TArray<HapiValueType>&)> ConvertFunc,
		SetUniqueAttribValueHapi SetUniqueAttribValueHapiFunc, SetAttribValueHapi SetAttribValueHapiFunc);
}

template<typename StrValueType>
static bool HoudiniPCGDataInputUtils::HapiUploadStringAttribValue(const UPCGMetadata* MetaData, const FName& AttribName,
	const int32& NodeId, HAPI_AttributeInfo& AttribInfo, TFunctionRef<FString(const StrValueType&)> ConvertFunc)
{
	if (const FPCGMetadataAttribute<StrValueType>* Attrib = MetaData->GetConstTypedAttribute<StrValueType>(AttribName))
	{
		const std::string AttribNameStr = HAPI_ATTRIB_PREFIX_UNREAL_PCG_ATTRIBUTE + std::string(TCHAR_TO_UTF8(*AttribName.ToString()));

		if (Attrib->GetEntryToValueKeyMap_NotThreadSafe().IsEmpty())  // Means all value is in default
		{
			AttribInfo.tupleSize = 1;
			AttribInfo.storage = HAPI_STORAGETYPE_STRING;

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo));

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeStringUniqueData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo, TCHAR_TO_UTF8(*ConvertFunc(Attrib->GetValue(PCGDefaultValueKey))), 1, 0, AttribInfo.count));
		}
		else
		{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
			TArray<const PCGMetadataEntryKey> EntryKeys;
#else
			TArray<PCGMetadataEntryKey> EntryKeys;
#endif
			for (PCGMetadataEntryKey EntryKey = 0; EntryKey < AttribInfo.count; ++EntryKey)
				EntryKeys.Add(EntryKey);
			TArray<PCGMetadataValueKey> ValueKeys;
			Attrib->GetValueKeys(EntryKeys, ValueKeys);
			TMap<PCGMetadataValueKey, std::string> KeyValueMap;
			std::string DefaultValue;
			{
				TArray<PCGMetadataValueKey> UniqueKeys = TSet<PCGMetadataValueKey>(ValueKeys).Array();
				const bool bHasDefaultValue = (UniqueKeys.RemoveAll([](const PCGMetadataValueKey& Key) { return (Key < 0); }) >= 1);
				if (!UniqueKeys.IsEmpty())
				{
					TArray<StrValueType> UniqueValues;
					UniqueValues.SetNum(UniqueKeys.Num());
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
					Attrib->GetValues(UniqueKeys, UniqueValues);
#else
					for (int32 UniqueIdx = 0; UniqueIdx < UniqueKeys.Num(); ++UniqueIdx)
						UniqueValues[UniqueIdx] = Attrib->GetValue(UniqueKeys[UniqueIdx]);
#endif
					for (int32 UniqueIdx = 0; UniqueIdx < UniqueKeys.Num(); ++UniqueIdx)
						KeyValueMap.Add(UniqueKeys[UniqueIdx], std::string(TCHAR_TO_UTF8(*ConvertFunc(UniqueValues[UniqueIdx]))));
				}

				if (bHasDefaultValue)
					DefaultValue = TCHAR_TO_UTF8(*ConvertFunc(Attrib->GetValue(PCGDefaultValueKey)));
			}

			TArray<const char*> StrValues;
			for (const PCGMetadataValueKey& ValueKey : ValueKeys)
				StrValues.Add((ValueKey < 0) ? DefaultValue.c_str() : KeyValueMap[ValueKey].c_str());

			AttribInfo.tupleSize = 1;
			AttribInfo.storage = HAPI_STORAGETYPE_STRING;

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo));

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeStringData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo, StrValues.GetData(), 0, AttribInfo.count));
		}
	}
	return true;
}

template<typename ValueType, typename HapiValueType, int TupleSize, HAPI_StorageType Storage, HAPI_AttributeTypeInfo AttribType,
	typename SetUniqueAttribValueHapi, typename SetAttribValueHapi>
static bool HoudiniPCGDataInputUtils::HapiUploadNumericAttribValue(const UPCGMetadata* MetaData, const FName& AttribName,
	const int32& NodeId, HAPI_AttributeInfo& AttribInfo, TFunctionRef<void(const ValueType&, TArray<HapiValueType>&)> ConvertFunc,
	SetUniqueAttribValueHapi SetUniqueAttribValueHapiFunc, SetAttribValueHapi SetAttribValueHapiFunc)
{
	if (const FPCGMetadataAttribute<ValueType>* Attrib = MetaData->GetConstTypedAttribute<ValueType>(AttribName))
	{
		const std::string AttribNameStr = HAPI_ATTRIB_PREFIX_UNREAL_PCG_ATTRIBUTE + std::string(TCHAR_TO_UTF8(*AttribName.ToString()));

		TArray<HapiValueType> DefaultValues;
		ConvertFunc(Attrib->GetValue(PCGDefaultValueKey), DefaultValues);
		if (Attrib->GetEntryToValueKeyMap_NotThreadSafe().IsEmpty())  // Means all value is in default
		{
			AttribInfo.tupleSize = TupleSize;
			AttribInfo.storage = Storage;
			AttribInfo.typeInfo = AttribType;

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo));

			HAPI_SESSION_FAIL_RETURN(SetUniqueAttribValueHapiFunc(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo, DefaultValues.GetData(), 1, 0, AttribInfo.count));
		}
		else
		{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
			TArray<const PCGMetadataEntryKey> EntryKeys;
#else
			TArray<PCGMetadataEntryKey> EntryKeys;
#endif
			for (PCGMetadataEntryKey EntryKey = 0; EntryKey < AttribInfo.count; ++EntryKey)
				EntryKeys.Add(EntryKey);
			TArray<PCGMetadataValueKey> ValueKeys;
			Attrib->GetValueKeys(EntryKeys, ValueKeys);
			TArray<HapiValueType> Values;
			for (const PCGMetadataValueKey& ValueKey : ValueKeys)
			{
				if (ValueKey < 0)
					Values.Append(DefaultValues);
				else
					ConvertFunc(Attrib->GetValue(ValueKey), Values);
			}

			AttribInfo.tupleSize = TupleSize;
			AttribInfo.storage = Storage;
			AttribInfo.typeInfo = AttribType;

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo));

			HAPI_SESSION_FAIL_RETURN(SetAttribValueHapiFunc(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				AttribNameStr.c_str(), &AttribInfo, Values.GetData(), 0, AttribInfo.count));
		}
	}
	return true;
}

using namespace HoudiniPCGDataInputUtils;

bool FHoudiniPCGComponentInput::HapiRetrieveData(UHoudiniInput* Input, const UObject* InputObject,
	const FPCGDataCollection& Data, TArray<int32>& InOutNodeIds, int32& InOutDataIdx)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HoudiniInputPCGData);

	// TODO: should use my shared memory input API like other input translators in my houdini engine, to import data faster


	HAPI_AttributeInfo AttribInfo;
	for (const FPCGTaggedData& TaggedData : Data.TaggedData)
	{
		if (const UPCGPointData* PointData = Cast<UPCGPointData>(TaggedData.Data))
		{
			const TArray<FPCGPoint>& Points = PointData->GetPoints();
			if (Points.IsEmpty())
				continue;

			int32 NodeId = InOutNodeIds.IsValidIndex(InOutDataIdx) ? InOutNodeIds[InOutDataIdx] : -1;
			const bool bCreateNewNode = (NodeId < 0);
			if (bCreateNewNode)
				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CreateNode(FHoudiniEngine::Get().GetSession(), Input->GetGeoNodeId(), "null",
					TCHAR_TO_UTF8(*FString::Printf(TEXT("%s_%s_%08X"), *InputObject->GetName(), *TaggedData.Data->GetName(), FPlatformTime::Cycles())),
					false, &NodeId))
			//else
			//	HAPI_SESSION_FAIL_RETURN(FHoudiniApi::RevertGeo(FHoudiniEngine::Get().GetSession(), NodeId));  // Why this can NOT revert geo after next commit?

			{
				TArray<float> PosData; PosData.SetNumUninitialized(Points.Num() * 3);
				TArray<float> RotData; RotData.SetNumUninitialized(Points.Num() * 4);
				TArray<float> ScaleData; ScaleData.SetNumUninitialized(Points.Num() * 3);
				TArray<float> DensityData; DensityData.SetNumUninitialized(Points.Num());
				TArray<float> ColorData; ColorData.SetNumUninitialized(Points.Num() * 3);
				TArray<float> AlphaData; AlphaData.SetNumUninitialized(Points.Num());

				for (int32 PointIdx = 0; PointIdx < Points.Num(); ++PointIdx)
				{
					const FPCGPoint& Point = Points[PointIdx];
					{
						const FVector3f Pos = FVector3f(Point.Transform.GetLocation() * POSITION_SCALE_TO_HOUDINI);
						PosData[PointIdx * 3] = Pos.X; PosData[PointIdx * 3 + 1] = Pos.Z; PosData[PointIdx * 3 + 2] = Pos.Y;
					}
					{
						const FQuat Rot = Point.Transform.GetRotation();
						RotData[PointIdx * 4] = Rot.X; RotData[PointIdx * 4 + 1] = Rot.Z; RotData[PointIdx * 4 + 2] = Rot.Y; RotData[PointIdx * 4 + 3] = -Rot.W;
					}
					{
						const FVector Scale = Point.Transform.GetScale3D();
						ScaleData[PointIdx * 3] = Scale.X; ScaleData[PointIdx * 3 + 1] = Scale.Z; ScaleData[PointIdx * 3 + 2] = Scale.Y;
					}
					DensityData[PointIdx] = Point.Density;
					ColorData[PointIdx * 3] = Point.Color.X; ColorData[PointIdx * 3 + 1] = Point.Color.Y; ColorData[PointIdx * 3 + 2] = Point.Color.Z;
					AlphaData[PointIdx] = Point.Color.W;
				}

				HAPI_PartInfo PartInfo;
				FHoudiniApi::PartInfo_Init(&PartInfo);
				PartInfo.type = HAPI_PARTTYPE_MESH;
				PartInfo.pointCount = Points.Num();

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetPartInfo(FHoudiniEngine::Get().GetSession(), NodeId, 0, &PartInfo));
				AttribInfo.count = PartInfo.pointCount;
				AttribInfo.owner = HAPI_ATTROWNER_POINT;
				{
					// @P
					AttribInfo.tupleSize = 3;
					AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_POSITION, &AttribInfo));

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_POSITION, &AttribInfo, PosData.GetData(), 0, AttribInfo.count));
				}
				{
					// p@rot
					AttribInfo.tupleSize = 4;
					AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_ROT, &AttribInfo));

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_ROT, &AttribInfo, RotData.GetData(), 0, AttribInfo.count));
				}
				{
					// v@scale
					AttribInfo.tupleSize = 3;
					AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_SCALE, &AttribInfo));

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_SCALE, &AttribInfo, ScaleData.GetData(), 0, AttribInfo.count));
				}
				{
					// f@density
					AttribInfo.tupleSize = 1;
					AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_DENSITY, &AttribInfo));

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_DENSITY, &AttribInfo, DensityData.GetData(), 0, AttribInfo.count));
				}
				{
					// v@Cd
					AttribInfo.tupleSize = 3;
					AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_COLOR, &AttribInfo));

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ATTRIB_COLOR, &AttribInfo, ColorData.GetData(), 0, AttribInfo.count));
				}
				{
					// f@Alpha
					AttribInfo.tupleSize = 1;
					AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ALPHA, &AttribInfo));

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
						HAPI_ALPHA, &AttribInfo, AlphaData.GetData(), 0, AttribInfo.count));
				}
			}

			TArray<FName> AttribNames;
			TArray<EPCGMetadataTypes> AttribTypes;
			PointData->Metadata->GetAttributes(AttribNames, AttribTypes);
			for (int32 AttribIdx = 0; AttribIdx < AttribNames.Num(); ++AttribIdx)
			{
				const FName& AttribName = AttribNames[AttribIdx];
				switch (AttribTypes[AttribIdx])
				{
				case EPCGMetadataTypes::Float:
					if (!HapiUploadNumericAttribValue<float, float, 1, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const float& SrcValue, TArray<float>& DstValues) { DstValues.Add(SrcValue); },
						FHoudiniApi::SetAttributeFloatUniqueData, FHoudiniApi::SetAttributeFloatData)) return false;
					break;
				case EPCGMetadataTypes::Double:
					if (!HapiUploadNumericAttribValue<double, double, 1, HAPI_STORAGETYPE_FLOAT64, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const double& SrcValue, TArray<double>& DstValues) { DstValues.Add(SrcValue); },
						FHoudiniApi::SetAttributeFloat64UniqueData, FHoudiniApi::SetAttributeFloat64Data)) return false;
					break;
				case EPCGMetadataTypes::Integer32:
					if (!HapiUploadNumericAttribValue<int32, int, 1, HAPI_STORAGETYPE_INT, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const int32& SrcValue, TArray<int>& DstValues) { DstValues.Add(SrcValue); },
						FHoudiniApi::SetAttributeIntUniqueData, FHoudiniApi::SetAttributeIntData)) return false;
					break;
				case EPCGMetadataTypes::Integer64:
					if (!HapiUploadNumericAttribValue<int64, HAPI_Int64, 1, HAPI_STORAGETYPE_INT64, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const int64& SrcValue, TArray<HAPI_Int64>& DstValues) { DstValues.Add(SrcValue); },
						FHoudiniApi::SetAttributeInt64UniqueData, FHoudiniApi::SetAttributeInt64Data)) return false;
					break;
				case EPCGMetadataTypes::Vector2:
					if (!HapiUploadNumericAttribValue<FVector2d, float, 2, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const FVector2d& SrcValue, TArray<float>& DstValues) { DstValues.Add(SrcValue.X); DstValues.Add(SrcValue.Y); },
						FHoudiniApi::SetAttributeFloatUniqueData, FHoudiniApi::SetAttributeFloatData)) return false;
					break;
				case EPCGMetadataTypes::Vector:
					if (!HapiUploadNumericAttribValue<FVector, float, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const FVector& SrcValue, TArray<float>& DstValues) { DstValues.Add(SrcValue.X); DstValues.Add(SrcValue.Y); DstValues.Add(SrcValue.Z); },
						FHoudiniApi::SetAttributeFloatUniqueData, FHoudiniApi::SetAttributeFloatData)) return false;
					break;
				case EPCGMetadataTypes::Vector4:
					if (!HapiUploadNumericAttribValue<FVector4, float, 4, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const FVector4& SrcValue, TArray<float>& DstValues) { DstValues.Add(SrcValue.X); DstValues.Add(SrcValue.Y); DstValues.Add(SrcValue.Z); DstValues.Add(SrcValue.W); },
						FHoudiniApi::SetAttributeFloatUniqueData, FHoudiniApi::SetAttributeFloatData)) return false;
					break;
				case EPCGMetadataTypes::Quaternion:
					if (!HapiUploadNumericAttribValue<FQuat, float, 4, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_QUATERNION>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const FQuat& SrcValue, TArray<float>& DstValues) { DstValues.Add(SrcValue.X); DstValues.Add(SrcValue.Z); DstValues.Add(SrcValue.Y); DstValues.Add(-SrcValue.W); },
						FHoudiniApi::SetAttributeFloatUniqueData, FHoudiniApi::SetAttributeFloatData)) return false;
					break;
				case EPCGMetadataTypes::Transform:
					if (!HapiUploadNumericAttribValue<FTransform, float, 16, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_MATRIX>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const FTransform& SrcValue, TArray<float>& DstValues)
						{
							const FMatrix44f UnrealXform = FMatrix44f(SrcValue.ToMatrixWithScale());

							FMatrix44f HoudiniXform;
							HoudiniXform.M[0][0] = UnrealXform.M[0][0];
							HoudiniXform.M[0][1] = UnrealXform.M[0][2];
							HoudiniXform.M[0][2] = UnrealXform.M[0][1];
							HoudiniXform.M[0][3] = UnrealXform.M[0][3];

							HoudiniXform.M[1][0] = UnrealXform.M[2][0];
							HoudiniXform.M[1][1] = UnrealXform.M[2][2];
							HoudiniXform.M[1][2] = UnrealXform.M[2][1];
							HoudiniXform.M[1][3] = UnrealXform.M[2][3];

							HoudiniXform.M[2][0] = UnrealXform.M[1][0];
							HoudiniXform.M[2][1] = UnrealXform.M[1][2];
							HoudiniXform.M[2][2] = UnrealXform.M[1][1];
							HoudiniXform.M[2][3] = UnrealXform.M[1][3];

							HoudiniXform.M[3][0] = UnrealXform.M[3][0] * POSITION_SCALE_TO_HOUDINI_F;
							HoudiniXform.M[3][1] = UnrealXform.M[3][2] * POSITION_SCALE_TO_HOUDINI_F;
							HoudiniXform.M[3][2] = UnrealXform.M[3][1] * POSITION_SCALE_TO_HOUDINI_F;
							HoudiniXform.M[3][3] = UnrealXform.M[3][3];
							DstValues.Append(&UnrealXform.M[0][0], 16);
						},
						FHoudiniApi::SetAttributeFloatUniqueData, FHoudiniApi::SetAttributeFloatData)) return false;
					break;
				case EPCGMetadataTypes::String:
					HOUDINI_FAIL_RETURN(HapiUploadStringAttribValue<FString>(PointData->Metadata, AttribName, NodeId, AttribInfo,
						[](const FString& Value) { return Value; }));
					break;
				case EPCGMetadataTypes::Boolean:
					if (!HapiUploadNumericAttribValue<bool, uint8, 1, HAPI_STORAGETYPE_UINT8, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const bool& SrcValue, TArray<uint8>& DstValues) { DstValues.Add(uint8(SrcValue)); },
						FHoudiniApi::SetAttributeUInt8UniqueData, FHoudiniApi::SetAttributeUInt8Data)) return false;
					break;
				case EPCGMetadataTypes::Rotator:
					if (!HapiUploadNumericAttribValue<FRotator, float, 3, HAPI_STORAGETYPE_FLOAT, HAPI_ATTRIBUTE_TYPE_NONE>(
						PointData->Metadata, AttribName, NodeId, AttribInfo, [](const FRotator& SrcValue, TArray<float>& DstValues) { DstValues.Add(SrcValue.Roll); DstValues.Add(SrcValue.Yaw); DstValues.Add(SrcValue.Pitch); },
						FHoudiniApi::SetAttributeFloatUniqueData, FHoudiniApi::SetAttributeFloatData)) return false;
					break;
				case EPCGMetadataTypes::Name:
					HOUDINI_FAIL_RETURN(HapiUploadStringAttribValue<FName>(PointData->Metadata, AttribName, NodeId, AttribInfo,
						[](const FName& Value) { return Value.ToString(); }));
					break;
				case EPCGMetadataTypes::SoftObjectPath:
					HOUDINI_FAIL_RETURN(HapiUploadStringAttribValue<FSoftObjectPath>(PointData->Metadata, AttribName, NodeId, AttribInfo,
						[](const FSoftObjectPath& Value) { return Value.ToString(); }));
					break;
				case EPCGMetadataTypes::SoftClassPath:
					HOUDINI_FAIL_RETURN(HapiUploadStringAttribValue<FSoftClassPath>(PointData->Metadata, AttribName, NodeId, AttribInfo,
						[](const FSoftClassPath& Value) { return Value.ToString(); }));
					break;
				}
			}

			if (!InputObject->IsA<AActor>())  // s@unreal_object_path
			{
				AttribInfo.tupleSize = 1;
				AttribInfo.storage = HAPI_STORAGETYPE_STRING;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_OBJECT_PATH, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeStringUniqueData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_OBJECT_PATH, &AttribInfo, TCHAR_TO_UTF8(*FHoudiniEngineUtils::GetAssetReference(InputObject)), 1, 0, AttribInfo.count));
			}

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CommitGeo(FHoudiniEngine::Get().GetSession(), NodeId));
			if (bCreateNewNode)
			{
				HOUDINI_FAIL_RETURN(Input->HapiConnectToMergeNode(NodeId));
				InOutNodeIds.Add(NodeId);
			}

			++InOutDataIdx;
		}
		else if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(TaggedData.Data))
		{
			const TArray<FInterpCurvePointVector>& Points = SplineData->SplineStruct.GetSplinePointsPosition().Points;
			if (Points.IsEmpty())
				continue;

			int32 NodeId = InOutNodeIds.IsValidIndex(InOutDataIdx) ? InOutNodeIds[InOutDataIdx] : -1;
			const bool bCreateNewNode = (NodeId < 0);
			if (bCreateNewNode)
				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CreateNode(FHoudiniEngine::Get().GetSession(), Input->GetGeoNodeId(), "null",
					TCHAR_TO_UTF8(*FString::Printf(TEXT("%s_%s_%08X"), *InputObject->GetName(), *TaggedData.Data->GetName(), FPlatformTime::Cycles())),
					false, &NodeId))
			//else
			//	HAPI_SESSION_FAIL_RETURN(FHoudiniApi::RevertGeo(FHoudiniEngine::Get().GetSession(), NodeId));  // Why this can NOT revert geo after next commit?

			const FTransform& Transform = SplineData->SplineStruct.Transform;
			
			const TArray<FInterpCurvePointQuat>& Rots = SplineData->SplineStruct.SplineCurves.Rotation.Points;
			const TArray<FInterpCurvePointVector>& Scales = SplineData->SplineStruct.SplineCurves.Scale.Points;
			const bool bImportRotAndScale = (Input->GetSettings().bImportRotAndScale && !Rots.IsEmpty() && !Scales.IsEmpty());
			TArray<float> PosData; PosData.Reserve(Points.Num() * 3);
			TArray<float> ArriveTangentData; ArriveTangentData.Reserve(Points.Num() * 3);
			TArray<float> LeaveTangentData; LeaveTangentData.Reserve(Points.Num() * 3);
			TArray<float> RotData; if (bImportRotAndScale) RotData.Reserve(Points.Num() * 4);
			TArray<float> ScaleData; if (bImportRotAndScale) ScaleData.Reserve(Points.Num() * 3);
			for (int32 PointIdx = 0; PointIdx < Points.Num(); ++PointIdx)
			{
				const FInterpCurvePointVector& Point = Points[PointIdx];
				{
					const FVector3f Pos = FVector3f(Transform.TransformPosition(Point.OutVal) * POSITION_SCALE_TO_HOUDINI);
					PosData.Add(Pos.X);
					PosData.Add(Pos.Z);
					PosData.Add(Pos.Y);
				}
				{
					const FVector3f Tangent = FVector3f(Transform.TransformVector(Point.ArriveTangent));
					ArriveTangentData.Add(Tangent.X); ArriveTangentData.Add(Tangent.Z); ArriveTangentData.Add(Tangent.Y);
				}
				{
					const FVector3f Tangent = FVector3f(Transform.TransformVector(Point.LeaveTangent));
					LeaveTangentData.Add(Tangent.X); LeaveTangentData.Add(Tangent.Z); LeaveTangentData.Add(Tangent.Y);
				}
				if (bImportRotAndScale)
				{
					const FQuat4f Rot = Rots.IsValidIndex(PointIdx) ? FQuat4f(Transform.TransformRotation(Rots[PointIdx].OutVal)) : FQuat4f::Identity;
					const FVector3f Scale = Scales.IsValidIndex(PointIdx) ? FVector3f(Transform.GetScale3D() * Scales[PointIdx].OutVal) : FVector3f::OneVector;
					RotData.Add(Rot.X); RotData.Add(Rot.Z); RotData.Add(Rot.Y); RotData.Add(-Rot.W);
					ScaleData.Add(Scale.X); ScaleData.Add(Scale.Z); ScaleData.Add(Scale.Y);
				}
			}

			HAPI_PartInfo PartInfo;
			FHoudiniApi::PartInfo_Init(&PartInfo);
			PartInfo.type = HAPI_PARTTYPE_CURVE;
			PartInfo.faceCount = 1;
			PartInfo.vertexCount = Points.Num();
			PartInfo.pointCount = Points.Num();

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetPartInfo(FHoudiniEngine::Get().GetSession(), NodeId, 0, &PartInfo));
			AttribInfo.count = PartInfo.pointCount;
			AttribInfo.owner = HAPI_ATTROWNER_POINT;
			{
				// @P
				AttribInfo.tupleSize = 3;
				AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_POSITION, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_POSITION, &AttribInfo, PosData.GetData(), 0, AttribInfo.count));
			}

			{
				HAPI_CurveInfo CurveInfo;
				FHoudiniApi::CurveInfo_Init(&CurveInfo);
				CurveInfo.curveType = HAPI_CURVETYPE_LINEAR;
				CurveInfo.curveCount = PartInfo.faceCount;
				CurveInfo.vertexCount = PartInfo.vertexCount;
				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetCurveInfo(FHoudiniEngine::Get().GetSession(), NodeId, 0, &CurveInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetCurveCounts(
					FHoudiniEngine::Get().GetSession(), NodeId, 0, &PartInfo.pointCount, 0, PartInfo.faceCount));
			}

			{
				// v@unreal_spline_point_arrive_tangent
				AttribInfo.tupleSize = 3;
				AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_SPLINE_POINT_ARRIVE_TANGENT, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_SPLINE_POINT_ARRIVE_TANGENT, &AttribInfo, ArriveTangentData.GetData(), 0, AttribInfo.count));
			}

			{
				// v@unreal_spline_point_leave_tangent
				AttribInfo.tupleSize = 3;
				AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_SPLINE_POINT_LEAVE_TANGENT, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_SPLINE_POINT_LEAVE_TANGENT, &AttribInfo, LeaveTangentData.GetData(), 0, AttribInfo.count));
			}

			if (bImportRotAndScale)
			{
				// p@rot
				AttribInfo.tupleSize = 4;
				AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_ROT, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_ROT, &AttribInfo, RotData.GetData(), 0, AttribInfo.count));

				// v@scale
				AttribInfo.tupleSize = 3;
				AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_SCALE, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_SCALE, &AttribInfo, ScaleData.GetData(), 0, AttribInfo.count));
			}

			if (!InputObject->IsA<AActor>())  // s@unreal_object_path
			{
				AttribInfo.tupleSize = 1;
				AttribInfo.storage = HAPI_STORAGETYPE_STRING;
				AttribInfo.owner = HAPI_ATTROWNER_PRIM;
				AttribInfo.count = PartInfo.faceCount;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_OBJECT_PATH, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeStringUniqueData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_UNREAL_OBJECT_PATH, &AttribInfo, TCHAR_TO_UTF8(*FHoudiniEngineUtils::GetAssetReference(InputObject)), 1, 0, AttribInfo.count));
			}

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CommitGeo(FHoudiniEngine::Get().GetSession(), NodeId));
			if (bCreateNewNode)
			{
				HOUDINI_FAIL_RETURN(Input->HapiConnectToMergeNode(NodeId));
				InOutNodeIds.Add(NodeId);
			}

			++InOutDataIdx;
		}
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5		
		else if (const UPCGDynamicMeshData* DMData = Cast<UPCGDynamicMeshData>(TaggedData.Data))
		{
			if (!IsValid(DMData->GetDynamicMesh()))
				continue;

			const FDynamicMesh3* DM = DMData->GetDynamicMesh()->GetMeshPtr();
			if (!DM)
				continue;

			int32 NodeId = InOutNodeIds.IsValidIndex(InOutDataIdx) ? InOutNodeIds[InOutDataIdx] : -1;
			const bool bCreateNewNode = (NodeId < 0);
			if (bCreateNewNode)
				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CreateNode(FHoudiniEngine::Get().GetSession(), Input->GetGeoNodeId(), "null",
					TCHAR_TO_UTF8(*FString::Printf(TEXT("%s_%s_%08X"), *InputObject->GetName(), *TaggedData.Data->GetName(), FPlatformTime::Cycles())),
					false, &NodeId))
			//else
			//	HAPI_SESSION_FAIL_RETURN(FHoudiniApi::RevertGeo(FHoudiniEngine::Get().GetSession(), NodeId));  // Why this can NOT revert geo after next commit?

			HAPI_PartInfo PartInfo;
			FHoudiniApi::PartInfo_Init(&PartInfo);
			PartInfo.type = HAPI_PARTTYPE_MESH;
			PartInfo.faceCount = DM->TriangleCount();
			PartInfo.vertexCount = PartInfo.faceCount * 3;
			PartInfo.pointCount = DM->VertexCount();

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetPartInfo(FHoudiniEngine::Get().GetSession(), NodeId, 0, &PartInfo));

			{  // @P
				TArray<float> PosData;
				for (int PointId = 0; PointId < PartInfo.pointCount; ++PointId)
				{
					const FVector3f Position = FVector3f(DM->GetVertexRef(PointId) * POSITION_SCALE_TO_HOUDINI);
					PosData.Add(Position.X);
					PosData.Add(Position.Z);
					PosData.Add(Position.Y);
				}

				AttribInfo.count = PartInfo.pointCount;
				AttribInfo.owner = HAPI_ATTROWNER_POINT;
				AttribInfo.tupleSize = 3;
				AttribInfo.storage = HAPI_STORAGETYPE_FLOAT;

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_POSITION, &AttribInfo));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					HAPI_ATTRIB_POSITION, &AttribInfo, PosData.GetData(), 0, AttribInfo.count));
			}

			if (PartInfo.faceCount >= 1)  // Sometimes maybe dynamic mesh only has points
			{
				TArray<int32> FaceCounts; FaceCounts.SetNumUninitialized(PartInfo.faceCount);
				TArray<int32> Vertices; Vertices.SetNumUninitialized(PartInfo.vertexCount);

				for (int TriId = 0; TriId < PartInfo.faceCount; ++TriId)
				{
					const UE::Geometry::FIndex3i Triangle = DM->GetTriangle(TriId);
					Vertices[TriId * 3] = Triangle.C;
					Vertices[TriId * 3 + 1] = Triangle.B;
					Vertices[TriId * 3 + 2] = Triangle.A;
					FaceCounts[TriId] = 3;
				}

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetVertexList(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					Vertices.GetData(), 0, Vertices.Num()));

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetFaceCounts(FHoudiniEngine::Get().GetSession(), NodeId, 0,
					FaceCounts.GetData(), 0, FaceCounts.Num()));
			}

			// TODO: Retrieve all attributes v@N, v@uv, s@unreal_material, etc.

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CommitGeo(FHoudiniEngine::Get().GetSession(), NodeId));
			if (bCreateNewNode)
			{
				HOUDINI_FAIL_RETURN(Input->HapiConnectToMergeNode(NodeId));
				InOutNodeIds.Add(NodeId);
			}

			++InOutDataIdx;
		}
#endif
	}

	return true;
}

bool FHoudiniPCGComponentInputBuilder::HapiUpload(UHoudiniInput* Input, const bool& bIsSingleComponent,  // Is there only one single valid component in the whole blueprint/actor
	const TArray<const UActorComponent*>& Components, const TArray<FTransform>& Transforms, const TArray<int32>& ComponentIndices,  // Components and Transforms are all of the components in blueprint/actor, and ComponentIndices are ref the valid indices from IsValidInput
	int32& InOutInstancerNodeId, TArray<TSharedPtr<FHoudiniComponentInput>>& InOutComponentInputs, TArray<FHoudiniComponentInputPoint>& InOutPoints)
{
	TSharedPtr<FHoudiniPCGComponentInput> CompInput;
	if (InOutComponentInputs.IsValidIndex(0))
		CompInput = StaticCastSharedPtr<FHoudiniPCGComponentInput>(InOutComponentInputs[0]);
	else
	{
		CompInput = MakeShared<FHoudiniPCGComponentInput>();
		InOutComponentInputs.Add(CompInput);
	}

	TArray<int32>& NodeIds = CompInput->NodeIds;

	int32 NumDatas = 0;
	for (const int32& CompIdx : ComponentIndices)
	{
		if (const UPCGComponent* PCGComp = Cast<UPCGComponent>(Components[CompIdx]))
		{
			HOUDINI_FAIL_RETURN(FHoudiniPCGComponentInput::HapiRetrieveData(Input,
				PCGComp->GetOuter(), PCGComp->GetGeneratedGraphOutput(), NodeIds, NumDatas));
		}
	}

	for (int32 NodeIdx = NodeIds.Num() - 1; NodeIdx >= NumDatas; --NodeIdx)
	{
		const int32& NodeId = NodeIds[NodeIdx];
		if (NodeId >= 0)
		{
			Input->NotifyMergedNodeDestroyed();
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::DeleteNode(FHoudiniEngine::Get().GetSession(), NodeId));
		}
		NodeIds.Pop();
	}

	return true;
}
