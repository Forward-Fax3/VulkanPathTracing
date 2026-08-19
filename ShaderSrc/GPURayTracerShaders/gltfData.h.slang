struct GeometryDescriptor
{
    uint64_t positionsOffset;
    uint64_t indicesOffset;
    uint64_t normalsOffset;
    uint64_t coloursOffset;
    uint64_t tangentOffset;
    uint64_t texCoordsOffset;
    uint materialIndex;
    uint is16BitIndices; // boolean
    uint pad[2]; // 64 bytes
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
    uint pad[2]; // pad to 80 bytes
}

struct LightData
{
    float3 position;
    uint type;
    float3 direction;
    float intensity;
    float3 colour;
    float range;
    float innerConeAngle;
    float outerConeAngle;
    float radius;
    uint pad[1];
}
