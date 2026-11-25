#include "Core.slangh"
#include "gltfData.slangh"
#include "BRDF.slangh"

#include "NRD.hlsli"


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
    float4x4 invProjectionMatrix;
    float4x4 invViewMatrix;
    float4x4 worldToClip;
    float4x4 preWorldToClip;
    float4x4 worldToView;
    float4x4 preWorldToView;
    uint randSeed;
    uint frameIndex;
    float2 cameraJitter;
    float tanSunAngularRadius;
    float cosSunAngularRadius;
};

// Path flags carried in the payload
static const uint PATH_FLAG_DIFFUSE      = 1u << 0; // lobe picked at the primary hit was diffuse -> rest of the path feeds the diffuse signal
static const uint PATH_FLAG_WRITE_GUIDES = 1u << 1; // this path writes the NRD guide buffers at the primary hit
static const uint PATH_INDEX_SHIFT       = 8u;      // bits 8-15: path index within the pixel
static const uint PATH_INDEX_MASK        = 0xFFu;
static const uint PATH_RR_SCALE_SHIFT    = 16u;     // bits 16-31: half, max component of the primary demodulation factor (Russian roulette)

// NRD material IDs (2 bits with R10G10B10A2 normal encoding).
// Host side: relaxSettings.minMaterialForDiffuse = 0, relaxSettings.minMaterialForSpecular = 1
static const float MATERIAL_ID_DEFAULT = 0.0f;
static const float MATERIAL_ID_METAL   = 1.0f;

struct [raypayload] RayPayload
{
    float3 diffRadiance : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 specRadiance : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit);
    float3 newOrigin : read(caller) : write(caller, closesthit);
    float3 newDirection : read(caller) : write(caller, closesthit);
    float hitT : read(caller) : write(caller, closesthit, miss);
    uint randSeed : read(caller, closesthit) : write(caller, closesthit);
    uint depth : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    uint flags : read(caller, closesthit, miss) : write(caller, closesthit);
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
    int sigmaLightIndex;
};

[[push_constant]] ConstantBuffer<PushConstantData> PC;


// ---- NRD inputs, written at the primary hit ----
[[vk::binding(0, 0)]] [[vk::image_format("r32f")]]
RWTexture2D<float> gOutViewZ;                   // IN_VIEWZ                 VK_FORMAT_R32_SFLOAT
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> gOutMotion;                 // IN_MV                    VK_FORMAT_R16G16B16A16_SFLOAT
[[vk::binding(2, 0)]] [[vk::image_format("rgb10_a2")]]
RWTexture2D<float4> gOutNormalRoughness;        // IN_NORMAL_ROUGHNESS      VK_FORMAT_A2B10G10R10_UNORM_PACK32
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> gOutDiffRadianceHitDist;    // IN_DIFF_RADIANCE_HITDIST VK_FORMAT_R16G16B16A16_SFLOAT
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> gOutSpecRadianceHitDist;    // IN_SPEC_RADIANCE_HITDIST VK_FORMAT_R16G16B16A16_SFLOAT
[[vk::binding(5, 0)]] [[vk::image_format("r16f")]]
RWTexture2D<float> gOutPenumbra;                // IN_PENUMBRA

// ---- Not NRD inputs: needed to rebuild the image after denoising (NRDComposite.slang) ----
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> gOutDiffFactor;             // diffuse demodulation factor
[[vk::binding(7, 0)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> gOutSpecFactor;             // specular demodulation factor
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> gOutEmission;               // primary-surface emission + sky; never denoised
[[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]]
RWTexture2D<float4> gOutSigmaLight;             // skyLight

[[vk::binding(10, 0)]]
RaytracingAccelerationStructure sceneBVH;
[[vk::binding(11, 0)]]
ConstantBuffer<GeneralData> PerFrameData;
[[vk::binding(12, 0)]]
SamplerState textureSampler;
[[vk::binding(13, 0)]]
Texture2DArray<float4> colourTextures;
[[vk::binding(14, 0)]]
Texture2DArray<float4> normalTextures;
[[vk::binding(15, 0)]]
Texture2DArray<float2> metallicRoughnessTextures;
[[vk::binding(16, 0)]]
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

// 4x4 Bayer dither, animated per frame (same as NVIDIA MathLib's Sequence::Bayer4x4).
// Used for the primary lobe choice so every 3x3 block is guaranteed both a diffuse and a specular sample.
float Bayer4x4(uint2 p, uint frameIndex)
{
    p &= 3u;
    uint b = ((p.y & 1u) << 2) | ((p.x & 1u) << 3) | ((p.y & 2u) >> 1) | (p.x & 2u);
    return (float((b + frameIndex) & 0xFu) + 0.5f) / 16.0f;
}

// Screen-uniform 1D Weyl sequence (same as MathLib's Sequence::Weyl1D)
float Weyl1D(float p, uint n)
{
    return frac(p + float(n * 10368889u) / 16777216.0f);
}

float GetViewZ(float3 worldPosition)
{
    return mul(float4(worldPosition, 1.0f), PerFrameData.worldToView).z;
}

// After the primary hit, everything a path collects goes into the signal of the lobe it picked there
void AddPathRadiance(inout RayPayload payload, float3 radiance)
{
    if ((payload.flags & PATH_FLAG_DIFFUSE) != 0)
        payload.diffRadiance += radiance;
    else
        payload.specRadiance += radiance;
}

// Russian roulette scale, stored as a half in the upper bits of the path flags (keeps the payload size unchanged)
void SetPathRRScale(inout RayPayload payload, float scale)
{
    payload.flags = (payload.flags & ((1u << PATH_RR_SCALE_SHIFT) - 1u)) | (f32tof16(scale) << PATH_RR_SCALE_SHIFT);
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
        const float dOverR = distance / light.range;
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

GetLightIndexReturn GetLightIndex(float3 position, float3 normal, int excludeIndex, inout uint rngState)
{
    GetLightIndexReturn result;
    float total = 0.0f;
    float chosenWeight = 0.0f;
    result.lightIndex = -1;
    float uniformRandNum = Rand(rngState);
    for (uint i = 0; i < PC.numOfLights; i++)
    {
        if (int(i) == excludeIndex)
            continue;

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

float TraceShadowHitDist(float3 origin, float3 dir, float tMax, inout uint rngState)
{
    RayQuery<RAY_FLAG_NONE> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.0f;
    ray.TMax = tMax;

    q.TraceRayInline(sceneBVH, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            GeometryDescriptor g = PC.geometryData[q.CandidateInstanceID() + q.CandidateRayGeometryIndex()];
            float a = EvalAlpha(g, q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics());
            if (a >= 1.0f || Rand(rngState) < a)
                q.CommitNonOpaqueTriangleHit();
        }
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : NRD_FP16_MAX;
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
    static_assert(NRD_NORMAL_ENCODING == 2, "NRD Normal not expected value");

    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 intScreenSize = DispatchRaysDimensions().xy;
    const float2 screenSize = (float2)intScreenSize;

    const uint pixelIndex = pixel.x + intScreenSize.x * pixel.y;
    uint randVal = PCGHash(pixelIndex + PerFrameData.randSeed);

    // NRD: every pixel must sample the SAME sub-pixel offset this frame (and NRD must be told what it is),
    // otherwise viewZ / normals / motion become noisy guides. Anti-aliasing then comes from varying
    // cameraJitter over frames (e.g. Halton 2,3), not from per-pixel random jitter.
    const float2 samplePos = float2(pixel) + 0.5f + PerFrameData.cameraJitter;
    const float2 screenCoords = samplePos / screenSize * 2.0 - 1.0;
    const float4 viewCoords = mul(float4(screenCoords, 1.0, 1.0), PerFrameData.invProjectionMatrix);
    const float3 cameraOrigin = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), PerFrameData.invViewMatrix).xyz;
    const float3 cameraDirection = mul(float4(normalize(viewCoords.xyz), 0.0f), PerFrameData.invViewMatrix).xyz;

    float3 diffRadiance = 0.0f;
    float3 specRadiance = 0.0f;
    float diffHitDist = 0.0f;
    uint diffHitDistNum = 0;
    float specHitDist = NRD_FrontEnd_SpecHitDistAveraging_Begin();

    for (uint path = 0; path < PC.numberOfSamples; path++)
    {
        RayDesc ray;
        ray.TMin = 0.001;
        ray.TMax = 10000.0;
        RayPayload payload;
        payload.diffRadiance = float3(0.0f);
        payload.specRadiance = float3(0.0f);
        payload.throughput = float3(1.0f);
        payload.hitT = 0.0f;
        payload.randSeed = randVal;
        payload.depth = PC.numberOfBounces;
        payload.flags = ((path & PATH_INDEX_MASK) << PATH_INDEX_SHIFT) | (path == 0 ? PATH_FLAG_WRITE_GUIDES : 0u) | (f32tof16(1.0f) << PATH_RR_SCALE_SHIFT);
        payload.newOrigin = cameraOrigin;
        payload.newDirection = cameraDirection;

        // NRD wants the length of the 1st bounce only (primary hit -> next hit), never the summed path length
        float firstBounceHitT = 0.0f; // 0 = "no data" for NRD
        uint bounce = 0;

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

            if (bounce == 1)
                firstBounceHitT = payload.hitT;
            bounce++;

            if (!NRD_IsValidRadiance(payload.diffRadiance) || !NRD_IsValidRadiance(payload.specRadiance))
                break;
        }

        randVal = payload.randSeed;

        // Radiance already includes 1 / lobe probability, so it is averaged over ALL paths below
        if (NRD_IsValidRadiance(payload.diffRadiance) && NRD_IsValidRadiance(payload.specRadiance))
        {
            diffRadiance += payload.diffRadiance;
            specRadiance += payload.specRadiance;
        }

        // Hit distances are not probability-weighted: average them only over paths of that lobe
        if ((payload.flags & PATH_FLAG_DIFFUSE) != 0)
        {
            if (firstBounceHitT > 0.0f)
            {
                diffHitDist += firstBounceHitT;
                diffHitDistNum++;
            }
        }
        else
            NRD_FrontEnd_SpecHitDistAveraging_Add(specHitDist, firstBounceHitT); // ignores 0
    }

    const float invPathNum = 1.0f / float(max(PC.numberOfSamples, 1u));
    diffRadiance *= invPathNum;
    specRadiance *= invPathNum;
    diffHitDist = diffHitDistNum != 0 ? diffHitDist / float(diffHitDistNum) : 0.0f;
    NRD_FrontEnd_SpecHitDistAveraging_End(specHitDist);

    // RELAX takes the raw hit distance. For REBLUR later: normalise each path's hitT with
    // REBLUR_FrontEnd_GetNormHitDist(...) before accumulating and pack with REBLUR_FrontEnd_PackRadianceAndNormHitDist.
    gOutDiffRadianceHitDist[pixel] = RELAX_FrontEnd_PackRadianceAndHitDist(diffRadiance, diffHitDist, true);
    gOutSpecRadianceHitDist[pixel] = RELAX_FrontEnd_PackRadianceAndHitDist(specRadiance, specHitDist, true);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    float3 rayDirection = WorldRayDirection();

    const float scale = 1.0f;
    float t = 0.5f * (rayDirection.y + 1.0f);
    const float3 sky = (1.0f - t) + t * float3(0.5f, 0.7f, 1.0f) * scale;

    // A bounce ray that escapes counts as "very far away" for NRD
    payload.hitT = NRD_FP16_MAX;

    if (payload.depth == PC.numberOfBounces)
    {
        // Primary miss: nothing to denoise. viewZ beyond CommonSettings::denoisingRange makes NRD skip the
        // pixel, and the sky goes straight into the emission buffer.
        if ((payload.flags & PATH_FLAG_WRITE_GUIDES) != 0)
        {
            const uint2 pixel = DispatchRaysIndex().xy;
            const float viewDirZ = mul(float4(rayDirection, 0.0f), PerFrameData.worldToView).z;

            gOutViewZ[pixel] = (viewDirZ < 0.0f ? -1.0f : 1.0f) * NRD_INF; // guides must not contain INF/NaN
            gOutMotion[pixel] = float4(0.0f);
            gOutNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(-rayDirection, 1.0f, MATERIAL_ID_DEFAULT);
            gOutDiffFactor[pixel] = float4(1.0f);
            gOutSpecFactor[pixel] = float4(1.0f);
            gOutEmission[pixel] = float4(sky, 0.0f);
        }
    }
    else
        AddPathRadiance(payload, payload.throughput * sky);

    payload.depth = 0;
}

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    if (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE)
    {
        IgnoreHit();
        return;
    }

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
    const bool isPrimary = payload.depth == PC.numberOfBounces;
    const uint2 pixel = DispatchRaysIndex().xy;

    // For the ray that left the primary hit, this is NRD's "1st bounce hitT"
    payload.hitT = RayTCurrent();

    float3 barycentricCoords = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    uint instanceID = InstanceID();
    uint geometryIndex = GeometryIndex();
    uint primitiveIndex = PrimitiveIndex();

    GeometryDescriptor geometry = PC.geometryData[instanceID + geometryIndex];

    uint3 attributeIndex = GetTriangleIndices(geometry, primitiveIndex);

    VertexData hitData = InterpolateVerticesData(geometry.vertexOffset, attributeIndex, barycentricCoords);

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

    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

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

    if (dot(hitNormal, geometricNormal) < 0.0f)
    {
        hitNormal = -hitNormal; tangent = -tangent; bitangent = -bitangent;
    }

    if (mat.normalTextureIndex != 0xFFFFFFFFUL)
    {
        float3 normalTexXYZ = normalTextures.SampleLevel(textureSampler, float3(hitData.UV[mat.normalTextureCoords], mat.normalTextureIndex), 0.0f).xyz * 2.0f - 1.0f;
        float3x3 TBN = float3x3(tangent, normalize(bitangent), hitNormal);
        hitNormal = normalize(mul(normalTexXYZ, TBN));
    }

    float3 negRayDir = -WorldRayDirection();
    float nv = dot(hitNormal, negRayDir);
    if (nv < 1e-3f)
        hitNormal = normalize(hitNormal + negRayDir * (1e-3f - nv));

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

    float metallicness = mat.metallicFactor;
    float roughness = mat.roughnessFactor; // glTF perceptual roughness == NRD "linear roughness"
    if (mat.metallicRoughnessTextureIndex != 0xFFFFFFFFUL)
    {
        float2 metallicRoughness = metallicRoughnessTextures.SampleLevel(textureSampler, float3(hitData.UV[mat.metallicRoughnessTextureCoords], mat.metallicRoughnessTextureIndex), 0.0f).xy;
        metallicness *= metallicRoughness.x;
        roughness *= metallicRoughness.y;
    }

    roughness = max(roughness, 0.03f); // avoid singularities in GGX

    float3 emissive = mat.emissiveFactor;
    if (mat.emissiveTextureIndex != 0xFFFFFFFFUL && (emissive.x > 0.0f || emissive.y > 0.0f || emissive.z > 0.0f))
        emissive *= emissiveTextures.SampleLevel(textureSampler, float3(hitData.UV[mat.emissiveTextureCoords], mat.emissiveTextureIndex), 0.0f).xyz;

    payload.newOrigin = hitLocation + geometricNormal * 1e-4;

    float3 albedo = colour4.xyz;
    float3 F0 = lerp(float3(0.04f), colour4.xyz, metallicness);

    float NdotV = max(dot(hitNormal, -WorldRayDirection()), 0.0f);
    float3 Fview = FresnelSchlick(NdotV, F0);
    float3 kD = (1.0f - Fview) * (1.0f - metallicness);

    float3 diffFactor = float3(1.0f);
    float3 specFactor = float3(1.0f);
    if (isPrimary)
    {
        NRD_MaterialFactors(hitNormal, negRayDir, albedo * (1.0f - metallicness), F0, roughness, diffFactor, specFactor);

        if ((payload.flags & PATH_FLAG_WRITE_GUIDES) != 0)
        {
            // Static geometry: previous position == current position. For moving instances, pass the hit
            // point transformed by last frame's object-to-world matrix as the second argument.
            const float3 motion = GetMotion(hitLocation, hitLocation, PerFrameData.worldToView, PerFrameData.preWorldToView, PerFrameData.worldToClip, PerFrameData.preWorldToClip);
            const float materialID = metallicness > 0.5f ? MATERIAL_ID_METAL : MATERIAL_ID_DEFAULT;

            gOutViewZ[pixel] = GetViewZ(hitLocation);
            gOutMotion[pixel] = float4(motion, 0.0f);
            gOutNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(hitNormal, roughness, materialID);
            gOutDiffFactor[pixel] = float4(diffFactor, 0.0f);
            gOutSpecFactor[pixel] = float4(specFactor, 0.0f);
            gOutEmission[pixel] = float4(emissive, 0.0f); // primary emission stays sharp: it bypasses NRD
        }
    }
    else
        AddPathRadiance(payload, payload.throughput * emissive);

    if (PC.numOfLights > 0)
    {
        GetLightIndexReturn lightIndex = GetLightIndex(payload.newOrigin, hitNormal, isPrimary ? PC.sigmaLightIndex : -1, payload.randSeed);
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
                    const GGXResult specLight = EvalGGXSpecular(hitNormal, negRayDir, light.direction, roughness, F0);

                    const float3 lightScale = payload.throughput * NdotL_light * light.radiance / (light.pdf * lightIndex.pdf);
                    const float3 diffContribution = lightScale * kD * albedo * oneOverPi;
                    const float3 specContribution = lightScale * specLight.value;
                    const float3 contribution = diffContribution + specContribution;

                    const float lum = dot(contribution, float3(0.2126f, 0.7152f, 0.0722f));
                    const float shadowRayThreshold = 5e-3;
                    const float p = lum / shadowRayThreshold;

                    if (p > 0.0f && (p >= 1.0f || Rand(payload.randSeed) < p) && IsVisible(payload.newOrigin, light.direction, light.distance, payload.randSeed))
                    {
                        const float invP = 1.0f / min(p, 1.0f);
                        if (isPrimary)
                        {
                            payload.diffRadiance += diffContribution * invP / diffFactor;
                            payload.specRadiance += specContribution * invP / specFactor;
                        }
                        else
                            AddPathRadiance(payload, contribution * invP);
                    }
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
    float3 newRayDirection;
    float NDotL;

    if (isPrimary)
    {
        float shadowHitDist = NRD_FP16_MAX;
        float tanRadius     = 0.0f;
        float3 unshadowed    = 0.0f;

        if (PC.sigmaLightIndex >= 0)
        {
            shadowHitDist = 0.0f; // occluded unless proven otherwise (back-facing / out of range / outside cone)

            const LightData sl = PC.lightsBuffer[PC.sigmaLightIndex];

            float3 L;           // direction to the light's centre
            float lightDist;
            float3 lightRadiance;

            if (sl.type == directionalLight)
            {
                L = -sl.direction;
                lightDist = 1e6f;
                tanRadius = PerFrameData.tanSunAngularRadius;
                lightRadiance = sl.colour * sl.intensity;
            }
            else
            {
                LightSample c = SampleLight(sl, payload.newOrigin);  // centre: attenuation + spot cone
                L = c.direction;
                lightDist = c.distance;
                tanRadius = sl.radius / max(c.distance, 1e-4f);
                lightRadiance = c.radiance;
            }

            if (dot(geometricNormal, L) > 0.0f && any(lightRadiance > 0.0f))
            {
                float3 Ls = L;
                float  tMax = lightDist;
                if (sl.type == directionalLight)
                    Ls = SampleSunDisk(L, PerFrameData.cosSunAngularRadius, payload.randSeed);
                else if (sl.radius > 0.0f)
                {
                    LightSample s = SampleSphereLight(sl, payload.newOrigin, payload.randSeed);
                    if (s.pdf > 0.0f)
                    {
                        Ls = s.direction;
                        tMax = s.distance;
                    }
                }

                shadowHitDist = TraceShadowHitDist(payload.newOrigin, Ls, tMax - 1e-3f, payload.randSeed);

                const float NdotL = dot(hitNormal, L);
                if (NdotL > 0.0f)
                {
                    const GGXResult spec = EvalGGXSpecular(hitNormal, negRayDir, L, roughness, F0);
                    unshadowed = lightRadiance * NdotL * (kD * albedo * oneOverPi + spec.value);
                }
            }
        }

        gOutPenumbra[pixel] = SIGMA_FrontEnd_PackPenumbra(shadowHitDist, tanRadius);
        gOutSigmaLight[pixel] = float4(unshadowed, 0.0f);

        // NRD: choose ONE lobe here; the rest of the path then feeds that lobe's signal only.
        // Probability clamped to [1/4, 3/4] + Bayer dithering guarantees both lobes inside every 3x3 block,
        // which RELAX's HitDistanceReconstructionMode::AREA_3X3 relies on. Exact 0 (pure metal) is allowed.
        float diffuseProbability = diffuseLum / max(diffuseLum + specularLum, 1e-4f);
        diffuseProbability = diffuseProbability > 0.0f ? clamp(diffuseProbability, 0.25f, 0.75f) : 0.0f;

        const uint pathIndex = (payload.flags >> PATH_INDEX_SHIFT) & PATH_INDEX_MASK;
        const float lobeRand = frac(Bayer4x4(pixel, PerFrameData.frameIndex)  + Weyl1D(rsqrt(7.0f), PerFrameData.frameIndex) + float(pathIndex) * 0.6180339887f);

        float3 lobeWeight; // (BRDF * cos / pdf) of the chosen lobe / selection probability / demodulation factor
        if (lobeRand < diffuseProbability)
        {
            newRayDirection = DiffuseReflection(hitNormal, tangent, bitangent, payload.randSeed);
            NDotL = max(dot(hitNormal, newRayDirection), 0.0f);

            // cosine sampling: (kD * albedo / pi) * cos / (cos / pi) = kD * albedo
            lobeWeight = kD * albedo / (diffFactor * diffuseProbability);
            payload.flags |= PATH_FLAG_DIFFUSE;
            SetPathRRScale(payload, max(max(diffFactor.x, diffFactor.y), diffFactor.z));
        }
        else
        {
            BRDFSample gs = SampleGGX(hitNormal, negRayDir, roughness, F0, float2(Rand(payload.randSeed), Rand(payload.randSeed)), tangent, bitangent);
            newRayDirection = gs.direction;
            NDotL = max(dot(hitNormal, newRayDirection), 0.0f);

            lobeWeight = gs.ggx.pdf > 0.0f ? gs.ggx.value * NDotL / (gs.ggx.pdf * specFactor * (1.0f - diffuseProbability)) : float3(0.0f);
            SetPathRRScale(payload, max(max(specFactor.x, specFactor.y), specFactor.z));
        }

        payload.throughput *= lobeWeight;

        if (NDotL <= 0.0f || dot(geometricNormal, newRayDirection) <= 0.0f || all(lobeWeight <= 0.0f))
        {
            payload.depth = 0; // this lobe's hitT stays 0 = "no data", NRD reconstructs it from neighbours
            return;
        }

        // No Russian roulette on the first bounce: a killed path would leave the pixel without a hit distance
    }
    else
    {
        float diffuseWeight = clamp(diffuseLum / max(diffuseLum + specularLum, 1e-4f), 0.05f, 0.95f);

        GGXResult evalResult;
        if (Rand(payload.randSeed) < diffuseWeight)
        {
            newRayDirection = DiffuseReflection(hitNormal, tangent, bitangent, payload.randSeed);
            evalResult = EvalGGXSpecular(hitNormal, negRayDir, newRayDirection, roughness, F0);
        }
        else
        {
            BRDFSample gs = SampleGGX(hitNormal, negRayDir, roughness, F0, float2(Rand(payload.randSeed), Rand(payload.randSeed)), tangent, bitangent);
            newRayDirection = gs.direction;
            evalResult = gs.ggx;
        }

        float3 diffuseValue = kD * albedo * oneOverPi;

        const float3 brdfValue = diffuseValue + evalResult.value;
        NDotL = max(dot(hitNormal, newRayDirection), 0.0f);
        float cosinePdf = NDotL * oneOverPi;
        const float brdfpdf = diffuseWeight * cosinePdf + (1.0f - diffuseWeight) * evalResult.pdf;

        payload.throughput *= (brdfpdf > 0.0f) ? (brdfValue * NDotL / brdfpdf) : float3(0.0f);

        if (brdfpdf <= 0.0f || NDotL <= 0.0f || dot(geometricNormal, newRayDirection) <= 0.0f)
        {
            payload.depth = 0;
            return;
        }

        // Roulette on what the path is worth in the FINAL image. The demodulated throughput alone is ~1 / lobe probability
        // (>= 1.33) after the primary hit, which pins rrP at 0.95 and lets almost every path run to numberOfBounces.
        const float rrScale = f16tof32(payload.flags >> PATH_RR_SCALE_SHIFT);
        float rrP = clamp(max(max(payload.throughput.x, payload.throughput.y), payload.throughput.z) * rrScale, 0.05f, 0.95f);
        if (Rand(payload.randSeed) > rrP)
        {
            payload.depth = 0;
            return;
        }

        payload.throughput /= rrP;
    }

    payload.newDirection = newRayDirection;
    payload.depth--;
}
