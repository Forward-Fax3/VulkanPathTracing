#pragma once


struct GeometryDescriptor
{
    uint64_t indicesOffset;
    uint64_t vertexOffset;
    uint materialIndex;
    uint is32BitIndices;
}

struct MaterialData
{
    float4 baseColourFactor;
    float metallicFactor;
    float roughnessFactor;
    uint baseColourTextureIndex;
    uint baseColourTextureCoords;
    uint metallicRoughnessTextureIndex;
    uint metallicRoughnessTextureCoords;
    float3 emissiveFactor;
    uint normalTextureIndex;
    uint normalTextureCoords;
    uint emissiveTextureIndex;
    uint emissiveTextureCoords;
    float alphaCutoff;
}

struct VertexData
{
    float3 normal;
    float4 tangent;
    float4 colour;
    float2 UV[2];
    uint pad[1]; // pad to 64 bytes
}

struct LightData
{
    float3 position;
    uint type;
    float3 direction;
    float intensity;
    float3 colour;
    float range;
    float cosInnerConeAngle;
    float cosOuterConeAngle;
    float radius;
    uint pad[1];
}
