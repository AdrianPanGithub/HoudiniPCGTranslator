// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniOutputPCGDataAsset.h"

#include "HoudiniApi.h"
#include "HoudiniEngine.h"
#include "HoudiniAttribute.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniOutputUtils.h"

#include "StaticMeshCompiler.h"

#include "HoudiniPCGCommon.h"

#include "PCGDataAsset.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "Data/PCGDynamicMeshData.h"
#include "UDynamicMesh.h"
#endif


bool FHoudiniPCGDataAssetOutputBuilder::HapiIsPartValid(const int32& NodeId, const HAPI_PartInfo& PartInfo, bool& bOutIsValid, bool& bOutShouldHoldByOutput)
{
	bOutShouldHoldByOutput = false;  // Only output to content as assets
	bOutIsValid = false;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
	if ((PartInfo.type == HAPI_PARTTYPE_MESH) || (PartInfo.type == HAPI_PARTTYPE_CURVE))  // Can output point cloud, splines, or dynamic mesh data
#else
	if (((PartInfo.type == HAPI_PARTTYPE_MESH) && (PartInfo.faceCount <= 0)) || (PartInfo.type == HAPI_PARTTYPE_CURVE))  // Can output point cloud or spline data
#endif
	{
		const int32& PartId = PartInfo.id;

		HAPI_AttributeInfo AttribInfo;
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
			HAPI_ATTRIB_UNREAL_OUTPUT_PCG_DATA_ASSET, HAPI_ATTROWNER_DETAIL, &AttribInfo));

		if (AttribInfo.exists && !FHoudiniEngineUtils::IsArray(AttribInfo.storage) &&
			FHoudiniEngineUtils::ConvertStorageType(AttribInfo.storage) == EHoudiniStorageType::Int)  // Currently only support i@unreal_output_pcg_data_asset = 1 on detail
		{
			int bIsPCGDataAsset = 0;
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeIntData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_UNREAL_OUTPUT_PCG_DATA_ASSET, &AttribInfo, 1, &bIsPCGDataAsset, 0, 1));

			bOutIsValid = bool(bIsPCGDataAsset);
			return true;
		}
	}

	return true;
}

namespace HoudiniPCGDataOutputUtils
{
	template<typename HapiValueType, typename ValueType, typename GetAttribValueHapi>
	static bool HapiCreateNumericPCGAttribute(const int32& NodeId, const int32& PartId, HAPI_AttributeInfo& AttribInfo,
		const std::string& AttribNameStr, GetAttribValueHapi GetAttribValueHapiFunc,
		UPCGMetadata* Metadata, const FName& AttribName, const ValueType& DefaultValue, TArray<PCGMetadataEntryKey>& EntryKeys);

	template<typename HapiValueType, typename ValueType, typename GetAttribValueHapi>
	static bool HapiCreateNumericPCGAttribute(const int32& NodeId, const int32& PartId, HAPI_AttributeInfo& AttribInfo,
		const std::string& AttribNameStr, GetAttribValueHapi GetAttribValueHapiFunc, TFunctionRef<ValueType(const TArray<HapiValueType>&, const int32&)> ConvertFunc,
		UPCGMetadata* Metadata, const FName& AttribName, const ValueType& DefaultValue, TArray<PCGMetadataEntryKey>& EntryKeys);
}

template<typename HapiValueType, typename ValueType, typename GetAttribValueHapi>
static bool HoudiniPCGDataOutputUtils::HapiCreateNumericPCGAttribute(const int32& NodeId, const int32& PartId, HAPI_AttributeInfo& AttribInfo,
	const std::string& AttribNameStr, GetAttribValueHapi GetAttribValueHapiFunc,
	UPCGMetadata* Metadata, const FName& AttribName, const ValueType& DefaultValue, TArray<PCGMetadataEntryKey>& EntryKeys)
{
	if (EntryKeys.IsEmpty())
	{
		EntryKeys.SetNumUninitialized(AttribInfo.count);
		for (PCGMetadataEntryKey EntryKey = 0; EntryKey < AttribInfo.count; ++EntryKey)
			EntryKeys[EntryKey] = EntryKey;
	}
	TArray<HapiValueType> Data;
	Data.SetNumUninitialized(AttribInfo.count * AttribInfo.tupleSize);
	HAPI_SESSION_FAIL_RETURN(GetAttribValueHapiFunc(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
		AttribNameStr.c_str(), &AttribInfo, -1, Data.GetData(), 0, AttribInfo.count));
	FPCGMetadataAttribute<ValueType>* Attrib = Metadata->CreateAttribute<ValueType>(AttribName, DefaultValue, true, true);
	Attrib->SetValues(EntryKeys, TArrayView<ValueType>((ValueType*)Data.GetData(), AttribInfo.count));

	return true;
}

template<typename HapiValueType, typename ValueType, typename GetAttribValueHapi>
static bool HoudiniPCGDataOutputUtils::HapiCreateNumericPCGAttribute(const int32& NodeId, const int32& PartId, HAPI_AttributeInfo& AttribInfo,
	const std::string& AttribNameStr, GetAttribValueHapi GetAttribValueHapiFunc, TFunctionRef<ValueType(const TArray<HapiValueType>&, const int32&)> ConvertFunc,
	UPCGMetadata* Metadata, const FName& AttribName, const ValueType& DefaultValue, TArray<PCGMetadataEntryKey>& EntryKeys)
{
	if (EntryKeys.IsEmpty())
	{
		EntryKeys.SetNumUninitialized(AttribInfo.count);
		for (PCGMetadataEntryKey EntryKey = 0; EntryKey < AttribInfo.count; ++EntryKey)
			EntryKeys[EntryKey] = EntryKey;
	}
	TArray<HapiValueType> Data;
	Data.SetNumUninitialized(AttribInfo.count * AttribInfo.tupleSize);
	HAPI_SESSION_FAIL_RETURN(GetAttribValueHapiFunc(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
		AttribNameStr.c_str(), &AttribInfo, -1, Data.GetData(), 0, AttribInfo.count));
	TArray<ValueType> PCGData;
	PCGData.SetNumUninitialized(AttribInfo.count);
	for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
		PCGData[ElemIdx] = ConvertFunc(Data, ElemIdx * AttribInfo.tupleSize);
	FPCGMetadataAttribute<ValueType>* Attrib = Metadata->CreateAttribute<ValueType>(AttribName, DefaultValue, true, true);
	Attrib->SetValues(EntryKeys, PCGData);

	return true;
}

using namespace HoudiniPCGDataOutputUtils;


bool FHoudiniPCGDataAssetOutputBuilder::HapiRetrieve(AHoudiniNode* Node, const FString& OutputName, const HAPI_GeoInfo& GeoInfo, const TArray<HAPI_PartInfo>& PartInfos)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HoudiniOutputPCGDataAsset);

	const int32& NodeId = GeoInfo.nodeId;

	TArray<UPCGDataAsset*> PCGDAs;  // One single asset may contains multiple data objects
	for (const HAPI_PartInfo& PartInfo : PartInfos)
	{
		const int32& PartId = PartInfo.id;

		TArray<std::string> AttribNames;
		HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetAttributeNames(NodeId, PartId, PartInfo.attributeCounts, AttribNames));
		
		FString ObjectPath;
		HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetStringAttributeValue(NodeId, PartId,
			AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_UNREAL_OBJECT_PATH, ObjectPath));
		if (IS_ASSET_PATH_INVALID(ObjectPath))
			ObjectPath = FHoudiniOutputUtils::GetCookFolderPath(Node) + TEXT("PCGDA_") + OutputName + TEXT("_") + FString::FromInt(PartId);

		UPCGDataAsset* PCGDA = FHoudiniEngineUtils::FindOrCreateAsset<UPCGDataAsset>(ObjectPath);
		if (!PCGDAs.Contains(PCGDA))  // If first time to create, then clear previous data
		{
			PCGDA->Data.Reset();
			PCGDA->Data.DataCrcs.Empty();
			PCGDAs.Add(PCGDA);
		}

		if ((PartInfo.type == HAPI_PARTTYPE_MESH) && (PartInfo.faceCount <= 0))  // Point cloud
		{
			FPCGTaggedData TaggedData;
			UPCGPointData* PointData = NewObject<UPCGPointData>(PCGDA);
			TaggedData.Data = PointData;

			const int32& PointCount = PartInfo.pointCount;

			TArray<FPCGPoint> Points;
			Points.SetNum(PointCount);
			{  // Transform
				TArray<HAPI_Transform> HapiTransforms;
				HapiTransforms.SetNumUninitialized(PointCount);
				if (PartInfo.instancedPartCount >= 1)
					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetInstancerPartTransforms(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
						HAPI_SRT, HapiTransforms.GetData(), 0, PointCount))
				else
					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetInstanceTransformsOnPart(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
						HAPI_SRT, HapiTransforms.GetData(), 0, PointCount))

				for (int32 PointIdx = 0; PointIdx < PointCount; ++PointIdx)
				{
					const HAPI_Transform& HapiTransform = HapiTransforms[PointIdx];
					FTransform& Transform = Points[PointIdx].Transform;
					Transform.SetLocation(FVector(HapiTransform.position[0], HapiTransform.position[2], HapiTransform.position[1]) * POSITION_SCALE_TO_UNREAL_F);
					Transform.SetRotation(FQuat(HapiTransform.rotationQuaternion[0], HapiTransform.rotationQuaternion[2], HapiTransform.rotationQuaternion[1], -HapiTransform.rotationQuaternion[3]));
					Transform.SetScale3D(FVector(HapiTransform.scale[0], HapiTransform.scale[2], HapiTransform.scale[1]));
					Points[PointIdx].MetadataEntry = PointIdx;  // Must add entry for attribute reader
				}
			}

			if (FHoudiniEngineUtils::IsAttributeExists(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_DENSITY, HAPI_ATTROWNER_POINT))  // f@density
			{
				HAPI_AttributeOwner Owner = HAPI_ATTROWNER_POINT;
				TArray<float> Data;
				HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetFloatAttributeData(NodeId, PartId, HAPI_ATTRIB_DENSITY, 1, Data, Owner));
				for (int32 PointIdx = 0; PointIdx < PointCount; ++PointIdx)
					Points[PointIdx].Density = Data[PointIdx];
			}

			{
				TArray<float> ColorData;
				if (FHoudiniEngineUtils::IsAttributeExists(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_COLOR, HAPI_ATTROWNER_POINT))  // v@Cd
				{
					HAPI_AttributeOwner Owner = HAPI_ATTROWNER_POINT;
					HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetFloatAttributeData(NodeId, PartId, HAPI_ATTRIB_COLOR, 3, ColorData, Owner));
				}
				TArray<float> AlphaData;
				if (FHoudiniEngineUtils::IsAttributeExists(AttribNames, PartInfo.attributeCounts, HAPI_ALPHA, HAPI_ATTROWNER_POINT))  // f@Alpha
				{
					HAPI_AttributeOwner Owner = HAPI_ATTROWNER_POINT;
					HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetFloatAttributeData(NodeId, PartId, HAPI_ALPHA, 1, AlphaData, Owner));
				}
				if (!ColorData.IsEmpty() && !AlphaData.IsEmpty())
				{
					for (int32 PointIdx = 0; PointIdx < PointCount; ++PointIdx)
					{
						FVector4& Color = Points[PointIdx].Color;
						if (!ColorData.IsEmpty())
						{
							Color.X = ColorData[PointIdx * 3];
							Color.Y = ColorData[PointIdx * 3 + 1];
							Color.Z = ColorData[PointIdx * 3 + 2];
						}
						if (!AlphaData.IsEmpty())
							Color.W = AlphaData[PointIdx];
					}
				}
			}

			{  // TODO: check whether this is necessary
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
				TArray<int64> ParentEntryKeys;
				for (int32 PointIdx = 0; PointIdx < PointCount; ++PointIdx)
					ParentEntryKeys.Add(-1);
				PointData->Metadata->AddEntries(ParentEntryKeys);
#else
				for (int32 PointIdx = 0; PointIdx < PointCount; ++PointIdx)
					PointData->Metadata->AddEntry(-1);
#endif
			}
			
			TArray<PCGMetadataEntryKey> EntryKeys;
			for (int32 AttribIdx = PartInfo.attributeCounts[HAPI_ATTROWNER_VERTEX];
				AttribIdx < PartInfo.attributeCounts[HAPI_ATTROWNER_VERTEX] + PartInfo.attributeCounts[HAPI_ATTROWNER_POINT]; ++AttribIdx)
			{
				const std::string& AttribNameStr = AttribNames[AttribIdx];
				if (AttribNameStr._Starts_with(HAPI_ATTRIB_PREFIX_UNREAL_PCG_ATTRIBUTE))
				{
					const FName AttribName(AttribNameStr.c_str() + strlen(HAPI_ATTRIB_PREFIX_UNREAL_PCG_ATTRIBUTE));
					if (AttribName.IsNone())
						continue;

					HAPI_AttributeInfo AttribInfo;
					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
						AttribNameStr.c_str(), HAPI_ATTROWNER_POINT, &AttribInfo));

					switch (AttribInfo.storage)
					{
					case HAPI_STORAGETYPE_INT:
					{
						switch (AttribInfo.tupleSize)
						{
						case 1: if (!HapiCreateNumericPCGAttribute<int32, int32>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeIntData, PointData->Metadata, AttribName, 0, EntryKeys)) return false; break;
						case 2: if (!HapiCreateNumericPCGAttribute<int32, FVector2d>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeIntData,
							[](const TArray<int32>& Data, const int32& ValueIdx) { return FVector2d(Data[ValueIdx], Data[ValueIdx + 1]); },
							PointData->Metadata, AttribName, FVector2d::ZeroVector, EntryKeys)) return false; break;
						case 3: if (!HapiCreateNumericPCGAttribute<int32, FVector>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeIntData,
							[](const TArray<int32>& Data, const int32& ValueIdx) { return FVector(Data[ValueIdx], Data[ValueIdx + 1], Data[ValueIdx + 2]); },
							PointData->Metadata, AttribName, FVector::ZeroVector, EntryKeys)) return false; break;
						case 4: if (!HapiCreateNumericPCGAttribute<int32, FVector4>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeIntData,
							[](const TArray<int32>& Data, const int32& ValueIdx) { return FVector4(Data[ValueIdx], Data[ValueIdx + 1], Data[ValueIdx + 2], Data[ValueIdx + 3]); },
							PointData->Metadata, AttribName, FVector4::Zero(), EntryKeys)) return false; break;
						}
					}
					break;
					case HAPI_STORAGETYPE_INT64:
					{
						switch (AttribInfo.tupleSize)
						{
						case 1: if (!HapiCreateNumericPCGAttribute<int64, int64>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeInt64Data, PointData->Metadata, AttribName, 0, EntryKeys)) return false; break;
						case 2: if (!HapiCreateNumericPCGAttribute<int64, FVector2d>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeInt64Data,
							[](const TArray<int64>& Data, const int32& ValueIdx) { return FVector2d(Data[ValueIdx], Data[ValueIdx + 1]); },
							PointData->Metadata, AttribName, FVector2d::ZeroVector, EntryKeys)) return false; break;
						case 3: if (!HapiCreateNumericPCGAttribute<int64, FVector>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeInt64Data,
							[](const TArray<int64>& Data, const int32& ValueIdx) { return FVector(Data[ValueIdx], Data[ValueIdx + 1], Data[ValueIdx + 2]); },
							PointData->Metadata, AttribName, FVector::ZeroVector, EntryKeys)) return false; break;
						case 4: if (!HapiCreateNumericPCGAttribute<int64, FVector4>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeInt64Data,
							[](const TArray<int64>& Data, const int32& ValueIdx) { return FVector4(Data[ValueIdx], Data[ValueIdx + 1], Data[ValueIdx + 2], Data[ValueIdx + 3]); },
							PointData->Metadata, AttribName, FVector4::Zero(), EntryKeys)) return false; break;
						}
					}
					break;
					case HAPI_STORAGETYPE_FLOAT:
					{
						switch (AttribInfo.tupleSize)
						{
						case 1: if (!HapiCreateNumericPCGAttribute<float, float>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloatData, PointData->Metadata, AttribName, 0, EntryKeys)) return false; break;
						case 2: if (!HapiCreateNumericPCGAttribute<float, FVector2d>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloatData,
							[](const TArray<float>& Data, const int32& ValueIdx) { return FVector2d(Data[ValueIdx], Data[ValueIdx + 1]); },
							PointData->Metadata, AttribName, FVector2d::ZeroVector, EntryKeys)) return false; break;
						case 3:
						{
							switch (AttribInfo.typeInfo)
							{
							case HAPI_ATTRIBUTE_TYPE_POINT: if (!HapiCreateNumericPCGAttribute<float, FVector>(NodeId, PartId, AttribInfo,
									AttribNameStr, FHoudiniApi::GetAttributeFloatData,
									[](const TArray<float>& Data, const int32& ValueIdx) { return FVector(Data[ValueIdx], Data[ValueIdx + 2], Data[ValueIdx + 1]) * POSITION_SCALE_TO_UNREAL; },
									PointData->Metadata, AttribName, FVector::ZeroVector, EntryKeys)) return false; break;
							default: if (!HapiCreateNumericPCGAttribute<float, FVector>(NodeId, PartId, AttribInfo,
								AttribNameStr, FHoudiniApi::GetAttributeFloatData,
								[](const TArray<float>& Data, const int32& ValueIdx) { return FVector(Data[ValueIdx], Data[ValueIdx + 1], Data[ValueIdx + 2]); },
								PointData->Metadata, AttribName, FVector::ZeroVector, EntryKeys)) return false; break;
							}
						}
						break;
						case 4: 
						{
							switch (AttribInfo.typeInfo)
							{
							case HAPI_ATTRIBUTE_TYPE_QUATERNION: if (!HapiCreateNumericPCGAttribute<float, FQuat>(NodeId, PartId, AttribInfo,
								AttribNameStr, FHoudiniApi::GetAttributeFloatData,
								[](const TArray<float>& Data, const int32& ValueIdx) { return FQuat(Data[ValueIdx], Data[ValueIdx + 2], Data[ValueIdx + 1], -Data[ValueIdx + 3]); },
								PointData->Metadata, AttribName, FQuat::Identity, EntryKeys)) return false; break;
							default: if (!HapiCreateNumericPCGAttribute<float, FVector4>(NodeId, PartId, AttribInfo,
								AttribNameStr, FHoudiniApi::GetAttributeFloatData,
								[](const TArray<float>& Data, const int32& ValueIdx) { return FVector4(Data[ValueIdx], Data[ValueIdx + 1], Data[ValueIdx + 2], Data[ValueIdx + 3]); },
								PointData->Metadata, AttribName, FVector4::Zero(), EntryKeys)) return false; break;
							}
						}
						break;
						case 16: if (!HapiCreateNumericPCGAttribute<float, FTransform>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloatData,
							[](const TArray<float>& Data, const int32& ValueIdx)
							{
								const FMatrix44f& UnrealXform = *((FMatrix44f*)(Data.GetData() + ValueIdx));

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

								HoudiniXform.M[3][0] = UnrealXform.M[3][0] * POSITION_SCALE_TO_UNREAL_F;
								HoudiniXform.M[3][1] = UnrealXform.M[3][2] * POSITION_SCALE_TO_UNREAL_F;
								HoudiniXform.M[3][2] = UnrealXform.M[3][1] * POSITION_SCALE_TO_UNREAL_F;
								HoudiniXform.M[3][3] = UnrealXform.M[3][3];
								return FTransform(FTransform3f(HoudiniXform));
							},
							PointData->Metadata, AttribName, FTransform::Identity, EntryKeys)) return false; break;
						}
					}
					break;
					case HAPI_STORAGETYPE_FLOAT64:
					{
						switch (AttribInfo.tupleSize)
						{
						case 1: if (!HapiCreateNumericPCGAttribute<double, double>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloat64Data, PointData->Metadata, AttribName, 0.0, EntryKeys)) return false; break;
						case 2: if (!HapiCreateNumericPCGAttribute<double, FVector2d>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloat64Data, PointData->Metadata, AttribName, FVector2d::ZeroVector, EntryKeys)) return false; break;
						case 3: if (!HapiCreateNumericPCGAttribute<double, FVector>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloat64Data, PointData->Metadata, AttribName, FVector::ZeroVector, EntryKeys)) return false; break;
						case 4: if (!HapiCreateNumericPCGAttribute<double, FVector4>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloat64Data, PointData->Metadata, AttribName, FVector4::Zero(), EntryKeys)) return false; break;
						case 16: if (!HapiCreateNumericPCGAttribute<double, FTransform>(NodeId, PartId, AttribInfo,
							AttribNameStr, FHoudiniApi::GetAttributeFloat64Data,
							[](const TArray<double>& Data, const int32& ValueIdx)
							{
								const FMatrix& UnrealXform = *((FMatrix*)(Data.GetData() + ValueIdx));

								FMatrix HoudiniXform;
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

								HoudiniXform.M[3][0] = UnrealXform.M[3][0] * POSITION_SCALE_TO_UNREAL;
								HoudiniXform.M[3][1] = UnrealXform.M[3][2] * POSITION_SCALE_TO_UNREAL;
								HoudiniXform.M[3][2] = UnrealXform.M[3][1] * POSITION_SCALE_TO_UNREAL;
								HoudiniXform.M[3][3] = UnrealXform.M[3][3];
								return FTransform(HoudiniXform);
							},
							PointData->Metadata, AttribName, FTransform::Identity, EntryKeys)) return false; break;
						}
					}
					break;
					case HAPI_STORAGETYPE_STRING:
					{
						if (EntryKeys.IsEmpty())
						{
							EntryKeys.SetNumUninitialized(AttribInfo.count);
							for (PCGMetadataEntryKey EntryKey = 0; EntryKey < AttribInfo.count; ++EntryKey)
								EntryKeys[EntryKey] = EntryKey;
						}

						TArray<HAPI_StringHandle> SHs;
						SHs.SetNumUninitialized(AttribInfo.count);
						HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeStringData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
							AttribNameStr.c_str(), &AttribInfo, SHs.GetData(), 0, AttribInfo.count));
						TArray<HAPI_StringHandle> UniqueSHs = TSet<HAPI_StringHandle>(SHs).Array();
						TArray<FString> UniqueStrs;
						HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiConvertStringHandles(UniqueSHs, UniqueStrs));
						if (!IS_ASSET_PATH_INVALID(UniqueStrs[0]))  // SoftObjectPath;
						{
							auto ConvertHoudiniStringToObjectPath = [](const FString& Str) -> FSoftObjectPath
								{
									int32 SplitIdx = -1;
									if (Str.FindChar(TCHAR(';'), SplitIdx))  // UHoudiniParameterAsset could import ref str with asset info(unreal_ref = import_info), which will append after ';'
										return Str.Left(SplitIdx);
									return Str;
								};

							FPCGMetadataAttribute<FSoftObjectPath>* Attrib = PointData->Metadata->CreateAttribute<FSoftObjectPath>(AttribName, FSoftObjectPath(), true, true);
							TMap<HAPI_StringHandle, FSoftObjectPath> SHAssetMap;
							for (int32 UniqueIdx = 0; UniqueIdx < UniqueSHs.Num(); ++UniqueIdx)
								SHAssetMap.Add(UniqueSHs[UniqueIdx], ConvertHoudiniStringToObjectPath(UniqueStrs[UniqueIdx]));
							TArray<FSoftObjectPath> Data;
							for (const HAPI_StringHandle& SH : SHs)
								Data.Add(SHAssetMap[SH]);
							Attrib->SetValues(EntryKeys, Data);
						}
						else  // String
						{
							FPCGMetadataAttribute<FString>* Attrib = PointData->Metadata->CreateAttribute<FString>(AttribName, FString(), true, true);
							TMap<HAPI_StringHandle, FString> SHAssetMap;
							for (int32 UniqueIdx = 0; UniqueIdx < UniqueSHs.Num(); ++UniqueIdx)
								SHAssetMap.Add(UniqueSHs[UniqueIdx], UniqueStrs[UniqueIdx]);
							TArray<FString> Data;
							for (const HAPI_StringHandle& SH : SHs)
								Data.Add(SHAssetMap[SH]);
							Attrib->SetValues(EntryKeys, Data);
						}
					}
					break;
					case HAPI_STORAGETYPE_UINT8: if (AttribInfo.tupleSize == 1)
					{
						if (!HapiCreateNumericPCGAttribute<uint8, bool>(NodeId, PartId, AttribInfo,
						AttribNameStr, FHoudiniApi::GetAttributeUInt8Data,
						[](const TArray<uint8>& Data, const int32& ValueIdx) { return bool(Data[ValueIdx]); },
						PointData->Metadata, AttribName, false, EntryKeys)) return false;
					}
					break;
					case HAPI_STORAGETYPE_INT8: if (AttribInfo.tupleSize == 1)
					{
						if (!HapiCreateNumericPCGAttribute<int8, bool>(NodeId, PartId, AttribInfo,
						AttribNameStr, FHoudiniApi::GetAttributeInt8Data,
						[](const TArray<int8>& Data, const int32& ValueIdx) { return bool(Data[ValueIdx]); },
						PointData->Metadata, AttribName, false, EntryKeys)) return false;
					}
					break;
					case HAPI_STORAGETYPE_INT16: if (AttribInfo.tupleSize == 1)
					{
						if (!HapiCreateNumericPCGAttribute<int16, int32>(NodeId, PartId, AttribInfo,
						AttribNameStr, FHoudiniApi::GetAttributeInt16Data,
						[](const TArray<int16>& Data, const int32& ValueIdx) { return int32(Data[ValueIdx]); },
						PointData->Metadata, AttribName, false, EntryKeys)) return false;
					}
					break;
					case HAPI_STORAGETYPE_DICTIONARY:
						break;
					}
				}
			}

			PointData->SetPoints(Points);
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
			PCGDA->Data.AddData(TaggedData, TaggedData.ComputeCrc(false));
#else
			PCGDA->Data.AddData({ TaggedData }, { TaggedData.ComputeCrc(false) });
#endif
		}
		else if (PartInfo.type == HAPI_PARTTYPE_CURVE)  // Curves
		{
			HAPI_AttributeInfo AttribInfo;

			// -------- Retrieve vertex list --------
			TArray<int32> CurveCounts;
			CurveCounts.SetNumUninitialized(PartInfo.faceCount);
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetCurveCounts(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				CurveCounts.GetData(), 0, PartInfo.faceCount));

			// -------- Transforms --------
			TArray<float> PositionData;
			PositionData.SetNumUninitialized(PartInfo.pointCount * 3);

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_POSITION, HAPI_ATTROWNER_POINT, &AttribInfo));

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_POSITION, &AttribInfo, -1, PositionData.GetData(), 0, PartInfo.pointCount));

			HAPI_AttributeOwner RotOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_ROT);
			TArray<FQuat> Rots;
			if (RotOwner != HAPI_ATTROWNER_INVALID)
			{
				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
					HAPI_ATTRIB_ROT, RotOwner, &AttribInfo));

				if (AttribInfo.storage == HAPI_STORAGETYPE_FLOAT || AttribInfo.storage == HAPI_STORAGETYPE_FLOAT64 &&
					AttribInfo.tupleSize == 3 || AttribInfo.tupleSize == 4)
				{
					TArray<float> RotData;
					RotData.SetNumUninitialized(AttribInfo.count * AttribInfo.tupleSize);

					HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
						HAPI_ATTRIB_ROT, &AttribInfo, -1, RotData.GetData(), 0, AttribInfo.count));

					Rots.SetNumUninitialized(AttribInfo.count);
					if (AttribInfo.tupleSize == 4)
					{
						for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
							Rots[ElemIdx] = FQuat(RotData[ElemIdx * 4], RotData[ElemIdx * 4 + 2], RotData[ElemIdx * 4 + 1], -RotData[ElemIdx * 4 + 3]);
					}
					else
					{
						for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
							Rots[ElemIdx] = FRotator(FMath::RadiansToDegrees(RotData[ElemIdx * 3]), FMath::RadiansToDegrees(RotData[ElemIdx * 3 + 2]), FMath::RadiansToDegrees(RotData[ElemIdx * 3 + 1])).Quaternion();
					}
				}
				else
					RotOwner = HAPI_ATTROWNER_INVALID;
			}

			HAPI_AttributeOwner ScaleOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_SCALE);
			TArray<float> ScaleData;
			HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetFloatAttributeData(NodeId, PartId, HAPI_ATTRIB_SCALE, 3, ScaleData, ScaleOwner));

			// -------- Curve Intrinsic --------
			HAPI_AttributeOwner CurveClosedOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_CURVE_CLOSED);
			TArray<int8> CurveClosedData;
			HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetEnumAttributeData(NodeId, PartId, HAPI_CURVE_CLOSED, CurveClosedData, CurveClosedOwner));

			HAPI_AttributeOwner ArriveTangentOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_UNREAL_SPLINE_POINT_ARRIVE_TANGENT);
			TArray<float> ArriveTangentData;
			HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetFloatAttributeData(NodeId, PartId, HAPI_ATTRIB_UNREAL_SPLINE_POINT_ARRIVE_TANGENT, 3, ArriveTangentData, ArriveTangentOwner));

			HAPI_AttributeOwner LeaveTangentOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_UNREAL_SPLINE_POINT_ARRIVE_TANGENT);
			TArray<float> LeaveTangentData;
			HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetFloatAttributeData(NodeId, PartId, HAPI_ATTRIB_UNREAL_SPLINE_POINT_ARRIVE_TANGENT, 3, LeaveTangentData, LeaveTangentOwner));

			// If has spline point tangents, then we just set to use ESplinePointType::CurveCustomTangent
			HAPI_AttributeOwner CurveTypeOwner = (!ArriveTangentData.IsEmpty() && !LeaveTangentData.IsEmpty()) ? HAPI_ATTROWNER_INVALID :
				FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_CURVE_TYPE);
			TArray<int8> CurveTypeData;
			HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetEnumAttributeData(NodeId, PartId, HAPI_CURVE_TYPE, CurveTypeData, CurveTypeOwner));

			int32 CurrVtxIdx = 0;
			int32 CurveIdx = 0;
			for (const int32& VertexCount : CurveCounts)
			{
				FPCGTaggedData TaggedData;
				UPCGSplineData* SplineData = NewObject<UPCGSplineData>(PCGDA);
				TaggedData.Data = SplineData;

				for (int32 VtxIdx = CurrVtxIdx; VtxIdx < CurrVtxIdx + VertexCount; ++VtxIdx)
				{
					FSplinePoint Point;
					Point.InputKey = VtxIdx - CurrVtxIdx;
					Point.Position = FVector(PositionData[VtxIdx * 3], PositionData[VtxIdx * 3 + 2], PositionData[VtxIdx * 3 + 1]) * POSITION_SCALE_TO_UNREAL;
					if (!Rots.IsEmpty())
					{
						Point.Rotation = Rots[FHoudiniOutputUtils::CurveAttributeEntryIdx(RotOwner, VtxIdx, CurveIdx)].Rotator();
					}
					if (!ScaleData.IsEmpty())
					{
						const int32 ValueIdx = FHoudiniOutputUtils::CurveAttributeEntryIdx(ScaleOwner, VtxIdx, CurveIdx) * 3;
						Point.Scale = FVector(ScaleData[ValueIdx], ScaleData[ValueIdx + 2], ScaleData[ValueIdx + 1]);
					}
					if (!ArriveTangentData.IsEmpty())
					{
						const int32 ValueIdx = FHoudiniOutputUtils::CurveAttributeEntryIdx(ArriveTangentOwner, VtxIdx, CurveIdx) * 3;
						Point.ArriveTangent = FVector(ArriveTangentData[ValueIdx], ArriveTangentData[ValueIdx + 2], ArriveTangentData[ValueIdx + 1]) * POSITION_SCALE_TO_UNREAL;
					}
					if (!LeaveTangentData.IsEmpty())
					{
						const int32 ValueIdx = FHoudiniOutputUtils::CurveAttributeEntryIdx(LeaveTangentOwner, VtxIdx, CurveIdx) * 3;
						Point.LeaveTangent = FVector(LeaveTangentData[ValueIdx], LeaveTangentData[ValueIdx + 2], LeaveTangentData[ValueIdx + 1]) * POSITION_SCALE_TO_UNREAL;
					}
					Point.Type = (ArriveTangentData.IsEmpty() || LeaveTangentData.IsEmpty()) ?
						((!CurveTypeData.IsEmpty() && (CurveTypeData[FHoudiniOutputUtils::CurveAttributeEntryIdx(CurveTypeOwner, VtxIdx, CurveIdx)] <= 0)) ?
							ESplinePointType::Linear : ESplinePointType::Curve) : ESplinePointType::CurveCustomTangent;

					SplineData->SplineStruct.AddPoint(Point, false);
				}

				if (!CurveClosedData.IsEmpty())
				{
					SplineData->SplineStruct.bClosedLoop = bool(CurveClosedData[FHoudiniOutputUtils::CurveAttributeEntryIdx(CurveClosedOwner, CurrVtxIdx, CurveIdx)]);
				}
				SplineData->SplineStruct.UpdateSpline();
				SplineData->SplineStruct.Bounds = SplineData->SplineStruct.GetBounds();
				SplineData->SplineStruct.LocalBounds = SplineData->SplineStruct.Bounds;
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
				PCGDA->Data.AddData(TaggedData, TaggedData.ComputeCrc(false));
#else
				PCGDA->Data.AddData({ TaggedData }, { TaggedData.ComputeCrc(false) });
#endif
				CurrVtxIdx += VertexCount;
				++CurveIdx;
			}
		}
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
		if ((PartInfo.type == HAPI_PARTTYPE_MESH) && (PartInfo.faceCount >= 1))  // Mesh
		{
			FPCGTaggedData TaggedData;
			UPCGDynamicMeshData* DMData = NewObject<UPCGDynamicMeshData>(PCGDA);
			TaggedData.Data = DMData;

			HAPI_AttributeInfo AttribInfo;

			// TODO: should copy the code from my houdini engine to support full dynamic mesh attributes and materials

			// -------- Retrieve mesh data --------
			TArray<float> PositionData;
			PositionData.SetNumUninitialized(PartInfo.pointCount * 3);

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_POSITION, HAPI_ATTROWNER_POINT, &AttribInfo));

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_POSITION, &AttribInfo, -1, PositionData.GetData(), 0, PartInfo.pointCount));

			TArray<int32> Vertices;
			Vertices.SetNumUninitialized(PartInfo.vertexCount);
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetVertexList(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				Vertices.GetData(), 0, PartInfo.vertexCount));

			FDynamicMesh3 DM;

			TMap<int32, int> PointIdxMap;  // PointIdx map to dynamic mesh vertex idx
			for (int32 TriIdx = 0; TriIdx < PartInfo.faceCount; ++TriIdx)
			{
				const FIntVector3 HoudiniPoints(Vertices[TriIdx * 3], Vertices[TriIdx * 3 + 1], Vertices[TriIdx * 3 + 2]);
				if ((HoudiniPoints.X == HoudiniPoints.Y) || (HoudiniPoints.Y == HoudiniPoints.Z) || (HoudiniPoints.Z == HoudiniPoints.X))  // Skip degenerated triangle
					continue;

				FIntVector3 IsNewPoints = FIntVector3::ZeroValue;
				UE::Geometry::FIndex3i Triangle;
				for (int32 TriVtxIdx = 0; TriVtxIdx < 3; ++TriVtxIdx)
				{
					const int32& GlobalPointIdx = HoudiniPoints[TriVtxIdx];  // This PointIdx is in global
					int LocalPointIdx;
					{
						const int* FoundLocalPointIdxPtr = PointIdxMap.Find(GlobalPointIdx);
						if (!FoundLocalPointIdxPtr)
						{
							LocalPointIdx = DM.AppendVertex(POSITION_SCALE_TO_UNREAL *
								FVector3d(PositionData[GlobalPointIdx * 3], PositionData[GlobalPointIdx * 3 + 2], PositionData[GlobalPointIdx * 3 + 1]));
							PointIdxMap.Add(GlobalPointIdx, LocalPointIdx);
							IsNewPoints[TriVtxIdx] = 1;
						}
						else
							LocalPointIdx = *FoundLocalPointIdxPtr;
					}

					Triangle[2 - TriVtxIdx] = LocalPointIdx;
				}

				DM.AppendTriangle(Triangle.A, Triangle.B, Triangle.C, 0);
			}

			DMData->Initialize(UE::Geometry::FDynamicMesh3(DM));

			PCGDA->Data.AddData(TaggedData, TaggedData.ComputeCrc(false));
		}
#endif
	}

	for (UPCGDataAsset* PCGDA : PCGDAs)
	{
		PCGDA->Modify();
		FHoudiniEngineUtils::NotifyAssetChanged(PCGDA);  // Notify all HDAs' PCGDataAsset input this asset has been modified
	}

	// After all static mesh outputs finished
	AsyncTask(ENamedThreads::GameThread, [PCGDAs]
		{
			FStaticMeshCompilingManager::Get().FinishAllCompilation();
			for (UPCGDataAsset* PCGDA : PCGDAs)
				PCGDA->PostEditChange();
		});

	return true;
}
