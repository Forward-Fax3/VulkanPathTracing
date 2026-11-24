#include "Core.slangh"
#include "HDR.slangh"

#include "NRD.hlsli"

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
	float denoisingRange;
	uint  validationEnabled;
	uint  sigmaEnabled;
	uint  fullscreenPanel;   // 0xFFFFFFFF = show the 4x4 grid
};

static const uint kNoPanel = 0xFFFFFFFF;

[[vk::binding(0, 0)]] ConstantBuffer<ImageInfo> imageInfo;
[[vk::binding(1, 0)]] ConstantBuffer<DynamicRangeData> rangeData;

[[vk::binding(2, 0)]] Sampler2D<float4> diffFactorTexture;
[[vk::binding(3, 0)]] Sampler2D<float4> specFactorTexture;
[[vk::binding(4, 0)]] Sampler2D<float4> emissionTexture;
[[vk::binding(5, 0)]] Sampler2D<float4> shadow;
[[vk::binding(6, 0)]] Sampler2D<float>  viewZTexture;
[[vk::binding(7, 0)]] Sampler2D<float4> diffuseTexture;
[[vk::binding(8, 0)]] Sampler2D<float4> specularTexture;
[[vk::binding(9, 0)]] Sampler2D<float4> sigmaLight;

// Debugging
[[vk::binding(10, 0)]] Sampler2D<float4> validationTexture;
[[vk::binding(11, 0)]]  Sampler2D<float4> noisyDiffuseTexture;
[[vk::binding(12, 0)]] Sampler2D<float4> noisySpecularTexture;

struct VertexStageOutput
{
	float4 sv_position : SV_Position;
	float2 uv : UV;
};

float3 NoisyDiffuse(float2 uv)
{
	return InDenoisingRange(uv) ? noisyDiffuseTexture.Sample(uv).rgb : 0.0;
}

float3 NoisySpecular(float2 uv)
{
	return InDenoisingRange(uv) ? noisySpecularTexture.Sample(uv).rgb : 0.0;
}

bool InDenoisingRange(float2 uv)
{
	return abs(viewZTexture.Sample(uv)) < imageInfo.denoisingRange;
}

float3 DenoisedDiffuse(float2 uv)
{
	return InDenoisingRange(uv) ? RELAX_BackEnd_UnpackRadiance(diffuseTexture.Sample(uv)).rgb : 0.0;
}

float3 DenoisedSpecular(float2 uv)
{
	return InDenoisingRange(uv) ? RELAX_BackEnd_UnpackRadiance(specularTexture.Sample(uv)).rgb : 0.0;
}

float3 CompositeNRD(float2 uv)
{
	float3 colour = emissionTexture.Sample(uv).rgb;

	colour += DenoisedDiffuse(uv)  * diffFactorTexture.Sample(uv).rgb;
	colour += DenoisedSpecular(uv) * specFactorTexture.Sample(uv).rgb;

	if (imageInfo.sigmaEnabled == 1)
		colour += sigmaLight.Sample(uv).rgb * SIGMA_BackEnd_UnpackShadow(shadow.Sample(uv)).x;

	return colour;
}

float3 SceneToDisplay(float3 hdrColour)
{
	hdrColour *= rangeData.exposure;

	if (rangeData.hasHDR == 1)
		return ApplySaturation(TonemapToHDR10(hdrColour, rangeData), rangeData.saturation);

	return ApplySaturation(pow(hdrColour, rangeData.invGamma), rangeData.saturation);
}

float3 SrgbToLinear(float3 c)
{
	return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

float3 OverlayToDisplay(float3 c)
{
	if (rangeData.hasHDR == 1)
	{
		const float sdrWhiteNits = 203.0;
		return linearToPQ(mul(REC709_TO_REC2020, SrgbToLinear(c)) * sdrWhiteNits);
	}

	return c;
}

float3 NrdPanel(uint idx, float2 panelUv)
{
	uint w, h;
	validationTexture.GetDimensions(w, h);
	const float2 halfTexel = 0.5 / float2(w, h);

	const float2 cellMin = float2(idx % 4, idx / 4) * 0.25;
	const float2 vUv = clamp(cellMin + panelUv * 0.25, cellMin + halfTexel, cellMin + 0.25 - halfTexel);

	return OverlayToDisplay(validationTexture.Sample(vUv).rgb);
}

float3 PanelToDisplay(uint idx, float2 panelUv)
{
	switch (idx)
	{
		case 5:
		case 6:
		case 9:
		case 10:
			return SceneToDisplay(CompositeNRD(panelUv));
		case 7:  return SceneToDisplay(diffFactorTexture.Sample(panelUv).rgb);
		case 11: return SceneToDisplay(specFactorTexture.Sample(panelUv).rgb);
		case 12: return SceneToDisplay(NoisyDiffuse(panelUv));
		case 13: return SceneToDisplay(DenoisedDiffuse(panelUv));
		case 14: return SceneToDisplay(DenoisedSpecular(panelUv));
		case 15: return SceneToDisplay(NoisySpecular(panelUv));
		default: return NrdPanel(idx, panelUv);
	}
}

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
	if (imageInfo.validationEnabled == 0)
		return float4(SceneToDisplay(CompositeNRD(uv)), 1.0f);

	if (imageInfo.fullscreenPanel != kNoPanel)
		return float4(PanelToDisplay(imageInfo.fullscreenPanel, uv), 1.0f);

	const float2 cell = min(floor(uv * 4.0), 3.0);
	const uint   idx  = uint(cell.y) * 4 + uint(cell.x);

	// 5, 6, 9, 10 share one 2x2 block, so they use the block's UV, not the cell's
	const bool   inBlock = all(cell >= 1.0) && all(cell <= 2.0);
	const float2 panelUv = inBlock ? (uv - 0.25) * 2.0 : frac(uv * 4.0);

	return float4(PanelToDisplay(idx, panelUv), 1.0f);
}