#include "../GeneralHelperShaders/Core.slang"
#include "../GeneralHelperShaders/HDR.slang"


static const float middleGrey = 0.18f;

struct VertexData
{
	static float2[] data = {
		{ 1.0,  1.0},
        { 1.0, -1.0},
		{-1.0,  1.0},
		{-1.0, -1.0}
	};

    static int[] indices = {
        0, 1, 2,
        1, 3, 2
    };
};

struct ImageInfo
{
    float invNumberOfSamples;
};

[[vk::binding(0, 0)]]
ConstantBuffer<ImageInfo> imageInfo;

[[vk::binding(1, 0)]]
ConstantBuffer<DynamicRangeData> rangeData;

[[vk::binding(2, 0)]]
Sampler2D<float4> texture;

struct VertexStageOutput
{
    float4 sv_position : SV_Position;
    float2 uv : UV;
};

[shader("vertex")]
VertexStageOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VertexStageOutput output;
    float2 vetData = VertexData.data[VertexData.indices[vertexIndex]];
    output.sv_position = float4(vetData, 0.0, 1.0);
    output.uv = (vetData + 1.0f) * 0.5f;

    return output;
}

[shader("fragment")]
float4 fragmentMain(float2 uv: UV) : SV_Target
{
    uint width, height;
    texture.GetDimensions(width, height);

    //uint2 pixelCoord = uint2(uv * float2(width, height));

    if (rangeData.hasHDR == 1)
    {
        float4 HDRInput = texture.Sample(uv) * imageInfo.invNumberOfSamples * rangeData.exposure;
        float3 pqOutput = ApplySaturation(TonemapToHDR10(HDRInput.xyz, rangeData), rangeData.saturation);
        return float4(pqOutput, 1.0f);
    }

    float3 output = ApplySaturation(pow(texture.Sample(uv).xyz * imageInfo.invNumberOfSamples * rangeData.exposure, rangeData.invGamma), rangeData.saturation);
    return float4(output, 1.0f);
}
