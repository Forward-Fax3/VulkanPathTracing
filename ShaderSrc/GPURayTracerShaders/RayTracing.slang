#include "../GeneralHelperShaders/Core.slang"
#include "../GeneralHelperShaders/gltfData.h.slang"
#include "../GeneralHelperShaders/BRDF.slang"

struct GetLightIndexReturn
{
    int lightIndex;
    float pdf;
}

struct LightSample
{
    float3 direction;
    float distance;
    float3 radiance;
    float pdf;
    float cosineLight;
}

struct GeneralData
{
    float4x4 InvProjectionMatrix;
    float4x4 InvViewMatrix;
    uint randSeed;
    uint stratifiedGridPosition;
    uint imageReset; // bool
};

struct [raypayload] RayPayload
{
    float3 radiance : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit);
    float3 newOrigin : read(caller) : write(caller, closesthit);
    float3 newDirection : read(caller) : write(caller, closesthit);
    uint randSeed : read(caller, closesthit) : write(caller, closesthit);
    uint depth : read(caller, closesthit) : write(caller, closesthit, miss);
};

struct PushConstantData
{
    uint64_t mainBufferData;
    GeometryDescriptor* geometryData;
    MaterialData* materialData;
    LightData* lightsBuffer;
    uint numOfLights;
    uint numberOfBounces;
    uint numberOfSamples;
    uint stratifiedGridSize;
};

[[push_constant]] ConstantBuffer<PushConstantData> PC;

[[vk::binding(0, 0)]]
RWTexture2D<float4> resultTexture;
[[vk::binding(1, 0)]]
RaytracingAccelerationStructure sceneBVH;
[[vk::binding(2, 0)]]
ConstantBuffer<GeneralData> PerFrameData;
[[vk::binding(3, 0)]]
SamplerState textureSampler;
[[vk::binding(4, 0)]]
Texture2DArray<float4> colourTextures;
[[vk::binding(5, 0)]]
Texture2DArray<float4> normalTextures;
[[vk::binding(6, 0)]]
Texture2DArray<float2> metallicRoughnessTextures;
[[vk::binding(7, 0)]]
Texture2DArray<float3> emissiveTextures;

float Rand(inout uint rngState)
{
    // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
    rngState = rngState * 747796405 + 1;
    uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
    word = (word >> 22) ^ word;
    return float(word >> 8) * (1.0f / 16777216.0f);
}

uint PCGHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float3 DiffuseReflection(float3 normal, float3 tangent, float3 bitangent, inout uint rngState)
{
    // Cosine-weighted hemisphere sampling: generate a random direction
    // centered around the normal with PDF = cos(theta) / pi.
    const float theta = 2.0 * pi * Rand(rngState); // Random in [0, 2pi]
    const float u = Rand(rngState);                 // Random in [0, 1]
    const float r = sqrt(u);
    const float x = r * cos(theta);
    const float y = r * sin(theta);
    const float z = sqrt(1.0 - u);

    return normalize(tangent * x + bitangent * y + normal * z);
}

__generic<T : IFloat> T GetTriangleAttributes(uint64_t bufferOffset, uint3 attributeIndex, float3 barycentricCoords)
{
    T* attr = reinterpret<T*>(PC.mainBufferData + bufferOffset);

    return T(barycentricCoords.x) * attr[attributeIndex.x] +
           T(barycentricCoords.y) * attr[attributeIndex.y] +
           T(barycentricCoords.z) * attr[attributeIndex.z];
}

VertexData InterpolateVerticesData(uint64_t vertexOffset, uint3 attributeIndex, float3 barycentricCoords)
{
    VertexData* vertices = reinterpret<VertexData*>(PC.mainBufferData + vertexOffset);
    VertexData vertexX = vertices[attributeIndex.x];
    VertexData vertexY = vertices[attributeIndex.y];
    VertexData vertexZ = vertices[attributeIndex.z];

    VertexData result;
    result.normal = float3(barycentricCoords.x) * vertexX.normal +
                    float3(barycentricCoords.y) * vertexY.normal +
                    float3(barycentricCoords.z) * vertexZ.normal;

    result.tangent = float4(barycentricCoords.x) * vertexX.tangent +
                     float4(barycentricCoords.y) * vertexY.tangent +
                     float4(barycentricCoords.z) * vertexZ.tangent;

    result.colour = float4(barycentricCoords.x) * vertexX.colour +
                    float4(barycentricCoords.y) * vertexY.colour +
                    float4(barycentricCoords.z) * vertexZ.colour;

    for (uint i = 0; i < 2; i++)
        result.UV[i] = float2(barycentricCoords.x) * vertexX.UV[i] +
                       float2(barycentricCoords.y) * vertexY.UV[i] +
                       float2(barycentricCoords.z) * vertexZ.UV[i];

    return result;
}

uint3 GetTriangleIndices(GeometryDescriptor geometry, uint primitiveIndex)
{
    if (geometry.is32BitIndices == 1)
        return reinterpret<uint3*>(PC.mainBufferData + geometry.indicesOffset)[primitiveIndex];

    uint16_t* indices16 = reinterpret<uint16_t*>(PC.mainBufferData + geometry.indicesOffset);
    return uint3(indices16[primitiveIndex * 3 + 0], indices16[primitiveIndex * 3 + 1], indices16[primitiveIndex * 3 + 2]);
}

// Alpha-only evaluation for any-hit / shadow-ray alpha testing.
// Only fetches what alpha needs: colour.w and a single UV set, not the full VertexData/MaterialData.
float EvalAlpha(GeometryDescriptor geometry, uint primitiveIndex, float2 barycentrics)
{
    if (geometry.materialIndex == 0xFFFFFFFFUL)
        return 1.0f; // default material is opaque

    MaterialData* m = PC.materialData + geometry.materialIndex;
    uint3 idx = GetTriangleIndices(geometry, primitiveIndex);

    VertexData* v = reinterpret<VertexData*>(PC.mainBufferData + geometry.vertexOffset);
    float3 b = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

    float alpha = m->baseColourFactor.w * (b.x * v[idx.x].colour.w + b.y * v[idx.y].colour.w + b.z * v[idx.z].colour.w);

    uint texIndex = m->baseColourTextureIndex;
    if (texIndex != 0xFFFFFFFFUL && alpha > 0.0f)
    {
        uint set = m->baseColourTextureCoords;
        float2 uv = b.x * v[idx.x].UV[set] + b.y * v[idx.y].UV[set] + b.z * v[idx.z].UV[set];
        alpha *= colourTextures.SampleLevel(textureSampler, float3(uv, texIndex), 0.0f).w;
    }

    return alpha;
}

float LightWeight(LightData light, float3 position, float3 normal)
{
    //const float3 BT709Weights = { 0.2126f, 0.7152f, 0.0722 };
    //float luminosity = dot(light.colour, BT709Weights) * light.intensity;
    float luminosity = light.luminosity;

    if (light.type == directionalLight)
    {
        float normalDotNegLightDirection = max(dot(normal, -light.direction), 0.0f);
        return luminosity * normalDotNegLightDirection;
    }

    float3 positionDelta = light.position - position;
    float distance2 = dot(positionDelta, positionDelta);
    float invDist = rsqrt(distance2);
    float normalDotDelta = dot(normal, positionDelta) * invDist;

    if (normalDotDelta <= 0.0f)
        return 0.0f;

    float distance = distance2 * invDist; // sqrt(distance2)
    if (light.range > 0.0f && distance > light.range)
        return 0.0f;

    float attenuation = 1.0f;
    if (light.range > 0.0f)
    {
        const float dOverR =distance / light.range;
        float k = saturate(1.0f - dOverR * dOverR * dOverR * dOverR);
        attenuation = k * k;
    }

    float spot = 1.0f;
    if (light.type == spotLight)
        spot = GetSpotAttenuation(light, positionDelta * invDist);
    if (spot <= 0.0f)
        return 0.0f;

    return luminosity * normalDotDelta * attenuation * spot / max(distance2, 1e-4);
}

GetLightIndexReturn GetLightIndex(float3 position, float3 normal, inout uint rngState)
{
    GetLightIndexReturn result;
    float total = 0.0f;
    float chosenWeight = 0.0f;
    result.lightIndex = -1;
    float uniformRandNum = Rand(rngState);
    for (uint i = 0; i < PC.numOfLights; i++)
    {
        float currentWeight = LightWeight(PC.lightsBuffer[i], position, normal);
        if (currentWeight <= 0.0f)
            continue;
        total += currentWeight;
        float probability = currentWeight / total;
        if (uniformRandNum < probability)
        {
            result.lightIndex = i;
            chosenWeight = currentWeight;
            uniformRandNum = uniformRandNum / probability;
        }
        else
            uniformRandNum = (uniformRandNum - probability) / (1.0f - probability);
    }

    result.pdf = (result.lightIndex < 0) ? 0.0f : chosenWeight / total;
    return result;
}

float GetRangeAttenuation(float range, float distance)
{
    if (range <= 0.0f)
        return 1.0f / (distance * distance);

    const float dOverR = distance / range;
    float num = saturate(1.0f - dOverR * dOverR * dOverR * dOverR);
    return (num * num) / max(distance * distance, 1e-4);
}

float GetSpotAttenuation(LightData light, float3 toLight)
{
    float cd = dot(-toLight, light.direction);
    float angularAttenuation = saturate((cd - light.cosOuterConeAngle) / max(1e-8, light.cosInnerConeAngle - light.cosOuterConeAngle));
    return angularAttenuation * angularAttenuation;
}

LightSample SampleLight(LightData light, float3 position)
{
    LightSample sample;
    sample.direction = float3(0.0f);
    sample.distance = 0.0f;
    sample.radiance = float3(0.0f);
    sample.cosineLight = 0.0f;
    sample.pdf = 1.0f;

    if (light.type == directionalLight)
    {
        sample.direction = -light.direction;
        sample.distance = 1e6;
        sample.radiance = light.colour * light.intensity;
        return sample;
    }

    float3 positionDelta = light.position - position;
    float distance = length(positionDelta);
    float3 direction = positionDelta / distance;
    float3 radiance = light.colour * light.intensity * GetRangeAttenuation(light.range, distance);
    if (light.type == pointLight)
    {
        sample.direction = direction;
        sample.distance = distance;
        sample.radiance = radiance;
        return sample;
    }

    if (light.type == spotLight)
    {
        float spotAttenuation = GetSpotAttenuation(light, direction);
        sample.direction = direction;
        sample.distance = distance;
        sample.radiance = radiance * spotAttenuation;
    }

    return sample;
}

LightSample SampleSphereLight(LightData light, float3 position, inout uint rngState)
{
    LightSample sample;
    sample.direction = float3(0.0f);
    sample.distance = 0.0f;
    sample.radiance = float3(0.0f);
    sample.pdf = 0.0f;
    sample.cosineLight = 0.0f;

    float3 toCentre = light.position - position;
    float d2 = dot(toCentre, toCentre);
    float d = sqrt(d2);
    float r = light.radius;
    if (d <= r)
        return sample;

    float cosThetaMax = sqrt(max(0.0f, 1.0f - (r * r) / d2));
    float3 w = toCentre / d;
    float3 u, v;
    BuildOrthonormalBasis(w, u, v);

    float cosTheta = 1.0f - Rand(rngState) * (1.0f - cosThetaMax);
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    float phi = 2.0f * pi * Rand(rngState);
    float3 dir = normalize(u * (cos(phi) * sinTheta) + v * (sin(phi) * sinTheta) + w * cosTheta);

    float t = d * cosTheta - sqrt(max(0.0f, r * r - d2 * sinTheta * sinTheta));

    float atten = 1.0f;
    if (light.range > 0.0f)
    {
        const float tOverR = t / light.range;
        float k = saturate(1.0f - tOverR * tOverR * tOverR * tOverR);
        atten = k * k;
    }

    sample.direction = dir;
    sample.distance = t;
    sample.radiance = light.colour * light.intensity / (pi * r * r) * atten;
    sample.pdf = 1.0f / (2.0f * pi * (1.0f - cosThetaMax));
    sample.cosineLight = 1.0f;
    return sample;
}

bool IsVisible(float3 position, float3 lightDirection, float lightDistance, inout uint rngState)
{
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;

    RayDesc ray;
    ray.Origin = position;
    ray.Direction = lightDirection;
    ray.TMin = 0.0f;

    float tMax = lightDistance - 1e-3;
    if (tMax < 1e-4)
        return true;
    ray.TMax = tMax;

    rayQuery.TraceRayInline(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, ray);
    while (rayQuery.Proceed())
    {
        uint candType = rayQuery.CandidateType();
        switch (candType)
        {
        case CANDIDATE_NON_OPAQUE_TRIANGLE:
        {
            GeometryDescriptor geometry = PC.geometryData[rayQuery.CandidateInstanceID() + rayQuery.CandidateRayGeometryIndex()];
            float alpha = EvalAlpha(geometry, rayQuery.CandidatePrimitiveIndex(), rayQuery.CandidateTriangleBarycentrics());

            if (alpha >= 1.0f || Rand(rngState) < alpha)
                rayQuery.CommitNonOpaqueTriangleHit();

            break;
        }
        default:
            continue;
        }
    }

    return rayQuery.CommittedStatus() == COMMITTED_NOTHING;
}

float3 SampleSunDisk(float3 sunDir, float cosAngularRadius, inout uint rngState)
{
    float r1 = Rand(rngState);
    float r2 = Rand(rngState);
    float cosTheta = 1.0f + r1 * (cosAngularRadius - 1.0f); // uniform in cos(theta)
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    float phi = 2.0f * pi * r2;

    // Build orthonormal basis around sunDir
    float3 w = sunDir;
    float3 u = (abs(w.x) > 0.1f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f));
    u = normalize(cross(u, w));
    float3 v = cross(w, u);

    return normalize(sinTheta * cos(phi) * u + sinTheta * sin(phi) * v + cosTheta * w);
}

[shader("raygeneration")]
void RayGenShader()
{
    const uint2 intThreadIdx = DispatchRaysIndex().xy;
    const uint2 intScreenSize = DispatchRaysDimensions().xy;

    const float2 threadIdx = (float2)intThreadIdx;
    const float2 screenSize = (float2)intScreenSize;

    uint pixelX = intThreadIdx.x;
    uint pixelY = intThreadIdx.y;

    const uint pixelIndex = pixelX + intScreenSize.x * pixelY;
    uint randVal = PCGHash(pixelIndex + PerFrameData.randSeed);

    const uint pixelHash = PCGHash(pixelX + intScreenSize.x * pixelY);
    const uint rotX = pixelHash % PC.stratifiedGridSize;
    const uint rotY = PCGHash(pixelHash) % PC.stratifiedGridSize;

    uint stratifiedGridPosition = PerFrameData.stratifiedGridPosition;

    float3 sum = 0.0f;

    for (uint i = 0; i < PC.numberOfSamples; i++)
    {
        uint cell = stratifiedGridPosition % (PC.stratifiedGridSize * PC.stratifiedGridSize);
        uint stratumX = (cell % PC.stratifiedGridSize + rotX) % PC.stratifiedGridSize;
        uint stratumY = (cell / PC.stratifiedGridSize + rotY) % PC.stratifiedGridSize;

        float jitterX = (float(stratumX) + Rand(randVal)) / float(PC.stratifiedGridSize);
        float jitterY = (float(stratumY) + Rand(randVal)) / float(PC.stratifiedGridSize);

        const float2 screenCoords = (threadIdx + float2(jitterX, jitterY)) / screenSize * 2.0 - 1.0;
        const float4 viewCoords = mul(float4(screenCoords, 1.0, 1.0), PerFrameData.InvProjectionMatrix);

        RayDesc ray;
        ray.TMin = 0.001;
        ray.TMax = 10000.0;
        RayPayload payload;
        payload.radiance = float3(0.0f);
        payload.throughput = float3(1.0f);
        payload.randSeed = randVal;
        payload.depth = PC.numberOfBounces;
        payload.newOrigin = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), PerFrameData.InvViewMatrix).xyz;
        payload.newDirection = mul(float4(normalize(viewCoords.xyz), 0.0f), PerFrameData.InvViewMatrix).xyz;

        while (payload.depth != 0)
        {
            uint rayFlags = RAY_FLAG_NONE;
            ray.Origin = payload.newOrigin;
            ray.Direction = payload.newDirection;

#ifdef SER
            HitObject hit = HitObject::TraceRay(sceneBVH, rayFlags, 0xFF, 0, 0, 0, ray, payload);

            if (payload.depth != PC.numberOfBounces)
            {
                uint coherenceHint = 0xFFFFu;
                if (hit.IsHit())
                    coherenceHint = min(PC.geometryData[hit.GetInstanceID() + hit.GetGeometryIndex()].materialIndex, 0xFFFEu); // default material -> 0xFFFE
                ReorderThread(hit, coherenceHint, 16);
            }

            HitObject::Invoke(sceneBVH, hit, payload);
#else
            TraceRay(sceneBVH, rayFlags, 0xFF, 0, 0, 0, ray, payload);
#endif

            if (any(isnan(payload.radiance)) || any(isinf(payload.radiance)))
                break;
        }

        if (any(isnan(payload.radiance)) || any(isinf(payload.radiance)))
            payload.radiance = 0.0f;

        sum += payload.radiance;
        randVal = payload.randSeed;
        stratifiedGridPosition++;
    }

    if (PerFrameData.imageReset == 1)
        resultTexture[intThreadIdx.xy] = float4(sum, (float)PC.numberOfSamples);
    else
        resultTexture[intThreadIdx.xy] += float4(sum, (float)PC.numberOfSamples);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    float3 rayDirection = WorldRayDirection();

    const float scale = 1.0f;
    float t = 0.5f * (rayDirection.y + 1.0f);
    payload.radiance += payload.throughput * ((1.0f - t) + t * float3(0.5f, 0.7f, 1.0f) * scale);
    payload.depth = 0;
}

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    GeometryDescriptor geometry = PC.geometryData[InstanceID() + GeometryIndex()];
    float alpha = EvalAlpha(geometry, PrimitiveIndex(), attr.barycentrics);
    if (alpha >= 1.0f)
        return;

    uint seed = PCGHash(PerFrameData.randSeed ^ PCGHash(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x) ^ asuint(RayTCurrent()));
    if (Rand(seed) > alpha)
        IgnoreHit();
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentricCoords = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    uint instanceID = InstanceID();
    uint geometryIndex = GeometryIndex();
    uint primitiveIndex = PrimitiveIndex();

    GeometryDescriptor geometry = PC.geometryData[instanceID + geometryIndex];

    uint3 attributeIndex = GetTriangleIndices(geometry, primitiveIndex);

    VertexData hitData = InterpolateVerticesData(geometry.vertexOffset, attributeIndex, barycentricCoords);

    //payload.radiance = float3(hitData.UV[0], 0.0f);

    MaterialData mat;
    if (geometry.materialIndex != 0xFFFFFFFFUL)
    {
        mat = PC.materialData[geometry.materialIndex];
    }
    else
    {
        mat.baseColourTextureIndex = 0xFFFFFFFFUL;
        mat.baseColourFactor = float4(1.0f);
        mat.normalTextureIndex = 0xFFFFFFFFUL;
        mat.metallicRoughnessTextureIndex = 0xFFFFFFFFUL;
        mat.metallicFactor = 0.0f;
        mat.roughnessFactor = 0.5f;
        mat.emissiveTextureIndex = 0xFFFFFFFFUL;
        mat.emissiveFactor = float3(0.0f);
    }

    float4 colour4 = hitData.colour * mat.baseColourFactor;

    if (mat.baseColourTextureIndex != 0xFFFFFFFFUL)
        colour4 *= colourTextures.SampleLevel(textureSampler, float3(hitData.UV[mat.baseColourTextureCoords], mat.baseColourTextureIndex), 0.0f);

    //payload.radiance = colour4.xyz;
    //payload.depth = 0;
    //return;

    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    /*if (colour4.w < 1.0f && Rand(payload.randSeed) > colour4.w)
    {
        payload.newOrigin = hitLocation + 0.0001f * WorldRayDirection();
        return;
    }*/

    float3x3 normalMatrix = transpose((float3x3)WorldToObject3x4());
    float3 hitNormal = normalize(mul(normalMatrix, hitData.normal));

    float3 tangent = mul((float3x3)ObjectToWorld3x4(), hitData.tangent.xyz);
    tangent -= hitNormal * dot(hitNormal, tangent);
    float3 bitangent;
    if (dot(tangent, tangent) > 1e-8f)
    {
        tangent = normalize(tangent);

        const float mirrored = determinant((float3x3)ObjectToWorld3x4()) < 0.0f ? -1.0f : 1.0f;
        const float handedness = hitData.tangent.w < 0.0f ? -1.0f : 1.0f;
        bitangent = cross(hitNormal, tangent) * handedness * mirrored;
    }
    else
        BuildOrthonormalBasis(hitNormal, tangent, bitangent);

    const float3 p0 = HitTriangleVertexPosition(0);
    const float3 p1 = HitTriangleVertexPosition(1);
    const float3 p2 = HitTriangleVertexPosition(2);

    const float3 objectGeoNormal = cross(p1 - p0, p2 - p0);
    float3 geometricNormal;
    if (dot(objectGeoNormal, objectGeoNormal) > 1e-30f)
        geometricNormal = normalize(mul(normalMatrix, objectGeoNormal));
    else
        geometricNormal = hitNormal; // degenerate triangle: use the shading normal

    if (dot(WorldRayDirection(), geometricNormal) > 0.0f)
        geometricNormal = -geometricNormal;

    /*payload.radiance = (geometricNormal + 1.0f) / 2.0f;
    payload.depth = 0;
    return;*/

    if (dot(hitNormal, geometricNormal) < 0.0f)
    {
        hitNormal = -hitNormal; tangent = -tangent; bitangent = -bitangent;
    }

    if (mat.normalTextureIndex != 0xFFFFFFFFUL)
    {
        /*float2 normalTexXY = normalTextures.SampleLevel(textureSampler, float3(texCoords, mat.normalTextureIndex), 0.0f).xy * 2.0f - 1.0f;
        float normalZ = sqrt(max(0.0f, 1.0f - dot(normalTexXY, normalTexXY)));
        float3 normalTexXYZ = float3(normalTexXY, normalZ);*/

        float3 normalTexXYZ = normalTextures.SampleLevel(textureSampler, float3(hitData.UV[mat.normalTextureCoords], mat.normalTextureIndex), 0.0f).xyz * 2.0f - 1.0f;
        float3x3 TBN = float3x3(tangent, normalize(bitangent), hitNormal);
        hitNormal = normalize(mul(normalTexXYZ, TBN));
    }

    float3 V = -WorldRayDirection();
    float nv = dot(hitNormal, V);
    if (nv < 1e-3f)
        hitNormal = normalize(hitNormal + V * (1e-3f - nv));

    float3 t = tangent - hitNormal * dot(hitNormal, tangent);
    if (dot(t, t) > 1e-8f)
    {
        tangent = normalize(t);
        bitangent = cross(hitNormal, tangent);
    }
    else
    {
        BuildOrthonormalBasis(hitNormal, tangent, bitangent);
    }

    /*payload.radiance = (hitNormal + 1.0f) / 2.0f;
    payload.depth = 0;
    return;*/

    float metallicness = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    if (mat.metallicRoughnessTextureIndex != 0xFFFFFFFFUL)
    {
        float2 metallicRoughness = metallicRoughnessTextures.SampleLevel(textureSampler, float3(hitData.UV[mat.metallicRoughnessTextureCoords], mat.metallicRoughnessTextureIndex), 0.0f).xy;
        metallicness *= metallicRoughness.x;
        roughness *= metallicRoughness.y;
    }

    roughness = max(roughness, 0.03f); // avoid singularities in GGX

    /*payload.radiance = float3(0.0, metallicness, roughness);
    payload.depth = 0;
    return;*/

    float3 emissive = mat.emissiveFactor;
    if (mat.emissiveTextureIndex != 0xFFFFFFFFUL && (emissive.x > 0.0f || emissive.y > 0.0f || emissive.z > 0.0f))
        emissive *= emissiveTextures.SampleLevel(textureSampler, float3(hitData.UV[mat.emissiveTextureCoords], mat.emissiveTextureIndex), 0.0f).xyz;

    payload.radiance += payload.throughput * emissive;

    payload.newOrigin = hitLocation + geometricNormal * 1e-4;

    float3 albedo = colour4.xyz;
    float3 F0 = lerp(float3(0.04f), colour4.xyz, metallicness);

    float NdotV = max(dot(hitNormal, -WorldRayDirection()), 0.0f);
    float3 Fview = FresnelSchlick(NdotV, F0);
    float3 kD = (1.0f - Fview) * (1.0f - metallicness);

    if (PC.numOfLights > 0)
    {
        //GetLightIndexReturn lightIndex = GetLightIndex(payload.newOrigin, hitNormal, Rand(payload.randSeed));
        GetLightIndexReturn lightIndex = GetLightIndex(payload.newOrigin, hitNormal, payload.randSeed);
        if (lightIndex.lightIndex != -1 && lightIndex.pdf > 0.0f)
        {
            LightData lightData = PC.lightsBuffer[lightIndex.lightIndex];

            LightSample light;
            if (lightData.type == pointLight && lightData.radius > 0.0)
                light = SampleSphereLight(lightData, payload.newOrigin, payload.randSeed); // pdf = 0 if invalid
            else
                light = SampleLight(lightData, payload.newOrigin);

            if (light.pdf > 1e-6f && any(light.radiance > 0.0f) && dot(geometricNormal, light.direction) > 0.0f)
            {
                float NdotL_light = max(dot(hitNormal, light.direction), 0.0f);
                if (NdotL_light > 0.0f)
                {
                    const GGXResult specLight = EvalGGXSpecular(hitNormal, V, light.direction, roughness, F0);
                    const float3 brdfValueLight = kD * albedo * oneOverPi + specLight.value;

                    // everything except visibility
                    const float3 contribution = payload.throughput * brdfValueLight * NdotL_light * light.radiance / (light.pdf * lightIndex.pdf);

                    // Russian roulette on the shadow ray
                    const float lum = dot(contribution, float3(0.2126f, 0.7152f, 0.0722f));
                    const float shadowRayThreshold = 5e-3;
                    const float p = saturate(lum / shadowRayThreshold);   // lum == 0 -> never traced

                    if (p > 0.0f && (p >= 1.0f || Rand(payload.randSeed) < p) && IsVisible(payload.newOrigin, light.direction, light.distance, payload.randSeed))
                        payload.radiance += contribution / p;
                }
            }
        }
    }

    if (payload.depth <= 1)
    {
        payload.depth = 0;
        return;
    }

    float diffuseLum = dot(albedo * (1.0f - metallicness), float3(0.2126f, 0.7152f, 0.0722f));
    float specularLum = dot(F0, float3(0.2126f, 0.7152f, 0.0722f));
    float diffuseWeight = clamp(diffuseLum / max(diffuseLum + specularLum, 1e-4f), 0.05f, 0.95f);
    float3 newRayDirection;

    GGXResult evalResult;
    if (Rand(payload.randSeed) < diffuseWeight)
    {
        newRayDirection = DiffuseReflection(hitNormal, tangent, bitangent, payload.randSeed);
        evalResult = EvalGGXSpecular(hitNormal, -WorldRayDirection(), newRayDirection, roughness, F0);
    }
    else
    {
        BRDFSample gs = SampleGGX(hitNormal, -WorldRayDirection(), roughness, F0, float2(Rand(payload.randSeed), Rand(payload.randSeed)), tangent, bitangent);
        newRayDirection = gs.direction;
        evalResult = gs.ggx;
    }

    float3 diffuseValue = kD * albedo * oneOverPi;

    const float3 brdfValue = diffuseValue + evalResult.value;
    const float NDotL = max(dot(hitNormal, newRayDirection), 0.0f);
    float cosinePdf = NDotL * oneOverPi;
    const float brdfpdf = diffuseWeight * cosinePdf + (1.0f - diffuseWeight) * evalResult.pdf;

    payload.newDirection = newRayDirection;

    payload.throughput *= (brdfpdf > 0.0f) ? (brdfValue * NDotL / brdfpdf) : float3(0.0f);

    if (brdfpdf <= 0.0f || NDotL <= 0.0f || dot(geometricNormal, newRayDirection) <= 0.0f)
    {
        payload.depth = 0;
        return;
    }

    float rrP = clamp(max(max(payload.throughput.x, payload.throughput.y), payload.throughput.z), 0.05f, 0.95f);
    if (Rand(payload.randSeed) > rrP)
    {
        payload.depth = 0;
        return;
    }

    payload.throughput /= rrP;

    payload.depth--;
}
