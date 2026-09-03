#include "../GeneralHelperShaders/HDR.slang"


struct PushConstants
{
    float2 scale;
    float2 translate;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

[[vk::binding(0, 0)]]
Sampler2D<float4> uiTexture;
[[vk::binding(1, 0)]]
ConstantBuffer<DynamicRangeData> rangeData;

struct VertexInput
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct VertexOutput
{
    float4 clipPosition : SV_Position;
    float2 uv           : TEXCOORD0;
    float4 color        : COLOR0;
};

struct FragmentInput
{
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.uv = input.uv;
    output.color = input.color;

    float2 transformedPos = input.position * pc.scale + pc.translate;
    output.clipPosition = float4(transformedPos, 0.0f, 1.0f);

    return output;
}

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target
{
    uint width, height;
    uiTexture.GetDimensions(width, height);

    uint2 pixelCoord = uint2(input.uv * float2(width, height));

    float4 texel = uiTexture[pixelCoord];
    float4 colour = texel * input.color;

    if (rangeData.hasHDR == 1)
    {
        DynamicRangeData localRangeData = rangeData;
        localRangeData.toneMappingSelector = 1; // Force ACES tone mapping for UI elements
        localRangeData.paperWhiteNits = rangeData.paperWhiteUINits;
        float3 pqOutput = TonemapToHDR10(colour.rgb, localRangeData);

        return float4(pqOutput, colour.a);
    }

    return colour;
}
