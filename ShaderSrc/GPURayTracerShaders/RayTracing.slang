#include "gltfData.h.slang"

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
}

struct GeneralData
{
    float4x4 InvProjectionMatrix;
    float4x4 InvViewMatrix;
    uint randSeed;
    uint imageReset; // bool
};

struct [raypayload] RayPayload
{
    float3 radiance : read(caller) : write(caller, closesthit);
    float3 throughput : read(closesthit, miss) : write(caller, closesthit);
    float3 newOrigin : read(caller) : write(closesthit);
    float3 newDirection : read(caller) : write(closesthit);
    uint randSeed : read(closesthit) : write(caller, closesthit);
    uint depth : read(closesthit) : write(caller, closesthit, miss);
};

struct PushConstantData
{
    uint64_t mainBufferData;
    GeometryDescriptor* geomertyData;
    MaterialData* materialData;
    LightData* lightsBuffer;
    uint numOfLights;
    uint numberOfBounces;
};

[[push_constant]] ConstantBuffer<PushConstantData> PC;

[[vk::binding(0, 0)]]
RWTexture2D<float4> resultTexture;
[[vk::binding(1, 0)]]
RaytracingAccelerationStructure sceneBVH;
[[vk::binding(2, 0)]]
ConstantBuffer<GeneralData> PerFrameData;

static constexpr uint spotLight = 0;
static constexpr uint pointLinght = 1;
static constexpr uint directionalLight = 2;

static constexpr float pi = 3.14159265f;
static constexpr float oneOverPi = 1.0f / pi;

static constexpr uint CANDIDATE_AABB = 1;
static constexpr uint CANDIDATE_TRIANGLE = 2;

float Rand(inout uint rngState)
{
    // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
    rngState = rngState * 747796405 + 1;
    uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
    word = (word >> 22) ^ word;
    return float(word) / 4294967295.0f;
}

float3 DiffuseReflection(float3 inRay, float3 normal, inout uint rngState)
{
    // For a random diffuse bounce direction, we follow the approach of
    // Ray Tracing in One Weekend, and generate a random point on a sphere
    // of radius 1 centered at the normal. This uses the random_unit_vector
    // function from chapter 8.5:

    const float theta = 2.0 * pi * Rand(rngState); // Random in [0, 2pi]
    const float u = 2.0 * Rand(rngState) - 1.0;    // Random in [-1, 1]
    const float r = sqrt(1.0 - u * u);
    const float3 direction = reflect(inRay, normal) + float3(r * cos(theta), r * sin(theta), u);

    if (dot(direction, direction) < 1e-8)
        return normal;

    return normalize(direction);
}

__generic<T : IFloat> T GetTriangleAttributes(uint64_t bufferOffset, uint3 attributeIndex, float3 barycentricCoords)
{
    T* attr = reinterpret<T*>(PC.mainBufferData + bufferOffset);

    return T(barycentricCoords.x) * attr[attributeIndex.x] +
           T(barycentricCoords.y) * attr[attributeIndex.y] +
           T(barycentricCoords.z) * attr[attributeIndex.z];
}

float LightWeight(LightData light, float3 position, float3 normal)
{
    constexpr float3 BT709Weights = { 0.2126f, 0.7152f, 0.0722 };
    float luminosity = dot(light.colour, BT709Weights) * light.intensity;

    if (light.type == directionalLight)
    {
        float normalDotNegLightDirection = max(dot(normal, -light.direction), 0.0f);
        return luminosity * normalDotNegLightDirection;
    }

    float3 positionDelta = light.position - position;
    float distance2 = dot(positionDelta, positionDelta);
    float normalDotDelta = dot(normal, normalize(positionDelta));

    if (normalDotDelta <= 0.0f)
        return 0.0f;

    float distance = sqrt(distance2);
    if (light.range > 0.0f && distance > light.range)
        return 0.0f;

    float attenuation = light.range > 0.0 ? saturate(1.0f - pow(distance / light.range, 4.0)) : 1.0f;

    return luminosity * normalDotDelta * attenuation / max(distance2, 1e-4);
}

GetLightIndexReturn GetLightIndex(float3 position, float3 normal, float randVal)
{
    float weights[64]; // max number of light is 64, hardcoded for now possibly, in feuture implement something like ReSTIR
    float total = 0.0f;

    for (uint i = 0; i < PC.numOfLights; i++)
    {
        float iWeight = LightWeight(PC.lightsBuffer[i], position, normal);
        weights[i] = iWeight;
        total += iWeight;
    }

    if (total < 1e-6)
        return { 0, 1.0f / PC.numOfLights };

    float t = randVal * total;
    float accumulation = 0.0f;

    for (uint i = 0; i < PC.numOfLights - 1; i++)
    {
        accumulation += weights[i];
        if (t <= accumulation)
            return { i, weights[i] / total };
    }

    return { PC.numOfLights - 1, weights[PC.numOfLights - 1] / total };
}

float GetRangeAttenuation(float range, float distance)
{
    if (range <= 0.0f)
        return 1.0f / (distance * distance);

    float num = clamp(1.0f - pow(distance / range, 4.0f), 0.0f, 1.0f);
    return (num * num) / max(distance * distance, 1e-4);
}

float GetSpotAttenuation(LightData light, float3 toLight)
{
    float cd = dot(-toLight, light.direction);
    float cosLightOuterConeAngle = cos(light.outerConeAngle);
    float agularAttenuation = clamp(
        (cd - cosLightOuterConeAngle) / (cos(light.innerConeAngle) - cosLightOuterConeAngle),
        0.0f, 1.0f
    );
    return agularAttenuation * agularAttenuation;
}

LightSample SampleLight(LightData light, float3 position, float randVal)
{
    if (light.type == directionalLight)
        return { -light.direction, 1e6, light.colour * light.intensity, 1.0f };

    float3 positionDelta = light.position - position;
    float distance = length(positionDelta);
    float3 direction = positionDelta / distance;
    float3 radiance = light.colour * light.intensity * GetRangeAttenuation(light.range, distance);
    if (light.type == pointLinght)
        return { direction, distance, radiance, 1.0f };

    if (light.type == spotLight)
        return { direction, distance, radiance * GetSpotAttenuation(light, direction), 1.0f };

    return { { 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f };
}

bool IsVisible(float3 position, float3 normal, float3 lightDirection, float lightDistance)
{
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> rayQuery;

    RayDesc ray;
    ray.Origin = position + normal * 1e-3;
    ray.Direction = lightDirection;
    ray.TMin = 0.0f;
    ray.TMax = lightDistance - 1e-3;

    rayQuery.TraceRayInline(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, ray);
    while (rayQuery.Proceed())
    {
        uint candType = rayQuery.CandidateType();
        if (candType == CANDIDATE_TRIANGLE)
        {
            if (candType == CANDIDATE_NON_OPAQUE_TRIANGLE)
                return false;
            else // for now just continue
                continue;
        }
    }

    return rayQuery.CommittedStatus() == COMMITTED_NOTHING;
}

[shader("raygeneration")]
void RayGenShader()
{
    const uint2 intThreadIdx = DispatchRaysIndex().xy;
    const uint2 intScreanSize = DispatchRaysDimensions().xy;

    uint randVal = PerFrameData.randSeed + intThreadIdx.x + intScreanSize.x * intThreadIdx.y;

    const float2 threadIdx = (float2)intThreadIdx;
    const float2 screenSize = (float2)intScreanSize;

    const float2 screenCoords = (threadIdx + float2(Rand(randVal), Rand(randVal)) - 0.5f) / screenSize * 2.0 - 1.0;

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
        ray.Origin = payload.newOrigin;
        ray.Direction = payload.newDirection;
        TraceRay(sceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    }

    if (PerFrameData.imageReset == 1)
        resultTexture[intThreadIdx.xy] = float4(payload.radiance, 1.0f);
    else
        resultTexture[intThreadIdx.xy] += float4(payload.radiance, 1.0f);
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

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentricCoords = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    uint instanceID = InstanceID();
    uint geometryIndex = GeometryIndex();
    uint primitiveIndex = PrimitiveIndex();

    GeometryDescriptor geometry = PC.geomertyData[instanceID + geometryIndex];

    uint3 attributeIndex;
    if (geometry.is16BitIndices == 1)
    {
        uint16_t* idxBuf = reinterpret<uint16_t*>(PC.mainBufferData + geometry.indicesOffset);
        attributeIndex = uint3(
            (uint)idxBuf[primitiveIndex * 3 + 0],
            (uint)idxBuf[primitiveIndex * 3 + 1],
            (uint)idxBuf[primitiveIndex * 3 + 2]
        );
    }
    else // 32 bit indecies
        attributeIndex = reinterpret<uint3*>(PC.mainBufferData + geometry.indicesOffset)[primitiveIndex];

    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 hitNormal = float3(0.0f, 1.0f, 0.0f);
    if (geometry.normalsOffset != 0xFFFFFFFFFFFFFFFFULL)
        hitNormal = normalize(GetTriangleAttributes<float3>(geometry.normalsOffset, attributeIndex, barycentricCoords));
    //bool frontFace = dot(WorldRayDirection(), hitNormal) <= 0.0f;
    //hitNormal = frontFace ? hitNormal : -hitNormal;

    // TODO: add materials
    // if (payload.depth == PC.numberOfBounces)
    //     payload.radiance += payload.throughput * mat.emissive;

    float4 colour4 = float4(1.0f);
    if (geometry.coloursOffset != 0xFFFFFFFFFFFFFFFFULL)
        colour4 = GetTriangleAttributes<float4>(geometry.coloursOffset, attributeIndex, barycentricCoords);

    // TODO: dont assume a material
    //MaterialData mat = PC.materialData[geometry.materialIndex];

    if (Rand(payload.randSeed) > colour4.w)
    {
        payload.newOrigin = hitLocation + 0.0001f * WorldRayDirection();
    }
    else
    {
        float epsilon = max(1e-4f, RayTCurrent() * 1e-4f);
        float3 newRayDirection = DiffuseReflection(WorldRayDirection(), hitNormal, payload.randSeed);
        payload.newOrigin = hitLocation + hitNormal * epsilon;
        payload.newDirection = newRayDirection;

        float3 colour = colour4.xyz * oneOverPi;

        payload.throughput *= colour;

        GetLightIndexReturn lightIndex = GetLightIndex(payload.newOrigin, hitNormal, Rand(payload.randSeed));
        LightSample light = SampleLight(PC.lightsBuffer[lightIndex.lightIndex], payload.newOrigin, Rand(payload.randSeed));

        if (light.pdf > 1e-6 && IsVisible(payload.newOrigin, hitNormal, light.direction, light.distance))
        {
            float normalDotLightDirection = max(dot(hitNormal, light.direction), 0.0f);
            payload.radiance += payload.throughput * light.radiance * normalDotLightDirection / (light.pdf * lightIndex.pdf);
        }

        float rrP = min(max(max(payload.throughput.x, payload.throughput.y), payload.throughput.z), 0.95f);
        if (Rand(payload.randSeed) > rrP)
        {
            payload.depth = 0;
            return;
        }
    }

    payload.depth--;
}
