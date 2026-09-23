#pragma once
#include "RE/B/BSFaceGenBaseMorphExtraData.h"
#include "RE/M/MemoryManager.h"
#include "RE/N/NiGeometryData.h"

// CommonLibSSE-NG ships RE::BSFaceGenBaseMorphExtraData itself; this only adds
// the FOD factory the author's CommonLib fork had as a static member.
namespace RE::FODUtil
{
	inline BSFaceGenBaseMorphExtraData* Create(NiGeometryData* geometryData, bool copy = false)
	{
		auto* data = NiExtraData::Create<BSFaceGenBaseMorphExtraData>();
		if (!data) return nullptr;
		data->name = BSFixedString("FOD");
		data->modelVertexCount = 0;
		data->vertexCount = 0;
		data->vertexData = nullptr;

		if (geometryData) {
			data->vertexCount = geometryData->vertices;
			data->modelVertexCount = geometryData->vertices;

			data->vertexData = static_cast<NiPoint3*>(MemoryManager::GetSingleton()->Allocate(sizeof(NiPoint3) * data->vertexCount, 0, false));
			if (copy)
				std::memcpy(data->vertexData, geometryData->vertex, sizeof(NiPoint3) * data->vertexCount);
			else
				std::memset(data->vertexData, 0, sizeof(NiPoint3) * data->vertexCount);
		}
		return data;
	}
}
