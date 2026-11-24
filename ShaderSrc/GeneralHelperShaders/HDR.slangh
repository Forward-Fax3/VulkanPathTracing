struct DynamicRangeData
{
    float exposure;
    float saturation;
    uint hasHDR;
    uint toneMappingSelector;
    float paperWhiteNits;
    float maxNits;
    float paperWhiteUINits;
    float invGamma;
};

static const float3x3 REC709_TO_REC2020 = float3x3(
    0.6274040f, 0.3292820f, 0.0433136f,
    0.0690970f, 0.9195400f, 0.0113612f,
    0.0163916f, 0.0880132f, 0.8955950f
);

// ST 2084 (PQ) constants
static const float PQ_M1 = 0.1593017578125f;
static const float PQ_M2 = 78.84375f;
static const float PQ_C1 = 0.8359375f;
static const float PQ_C2 = 18.8515625f;
static const float PQ_C3 = 18.6875f;

float3 linearToPQ(float3 linearNits)
{
    float3 Lp = pow(max(linearNits, 0.0f), PQ_M1);
    float3 num = PQ_C1 + PQ_C2 * Lp;
    float3 den = 1.0f + PQ_C3 * Lp;
    return pow(num / den, PQ_M2);
}


// ACES HDR tone map
float3 ACESFitted(float3 x)
{
    static const float a = 2.51f;
    static const float b = 0.03f;
    static const float c = 2.43f;
    static const float d = 0.59f;
    static const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ACESHDR(float3 colour, float maxWhite)
{
    float3 scaled = colour / maxWhite;
    float3 toned = ACESFitted(scaled);
    return toned * maxWhite;
}


// AGX HDR tone map
static const float3x3 AGX_MAT = float3x3(
    0.842479062253094,  0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772,  0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104
);

static const float3x3 AGX_MAT_INV = float3x3(
    1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
   -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
   -0.0990297440797205, -0.0989611768448433,  1.15107367264116
);

static const float AGX_GRAY_RESPONSE = 0.496723f;
static const float AGX_CEILING_RESPONSE = 0.998556f;

float3 AGXDefaultContrastApprox(float3 x)
{
    //return pow(x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f), 5.0f);
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

float3 AGXSimplified(float3 colour)
{
    static const float minEV = -12.47393f;
    static const float maxEV = 4.026069f;
    //static const float minEV = -16.0f;
    //static const float maxEV = 6.0f;
    static const float deltaEV = maxEV - minEV;

    //float3 v = mul(AGX_INSET, colour);
    //v = max(v, 1e-10f);
    //v = clamp(log2(v), minEV, maxEV);
    //v = (v - minEV) / deltaEV;
    //v = AGXDefaultContrastApprox(v);
    //v = mul(AGX_OUTSET, v);
    //return max(v , 0.0f);
    float3 v = mul(colour, AGX_MAT);
    v = clamp(log2(max(v, 1e-6f)), minEV, maxEV);
    v = (v - minEV) / deltaEV;
    v = AGXDefaultContrastApprox(v);
    return v;
}

float3 AGXHDR(float3 colour, DynamicRangeData rangeData)
{
    //float3 scaled = colour / maxWhite;
    //float3 toned = AGXSimplified(scaled);
    //float3 toned = pow(AGXSimplified(colour), 5.0f);
    float3 toned = AGXSimplified(colour);
    toned = mul(toned, AGX_MAT_INV);
    //return toned;
    //return toned * rangeData.curveModifiers.maxNits;

    float a = (rangeData.maxNits - rangeData.paperWhiteNits) / (AGX_CEILING_RESPONSE - AGX_GRAY_RESPONSE);
    float b = rangeData.paperWhiteNits - AGX_GRAY_RESPONSE * a;

    return max(toned * a + b, 0.0f);
}

float3 TonemapToHDR10(float3 linearColorRec709, DynamicRangeData rangeData)
{
     if (rangeData.toneMappingSelector == 1)
     {
        float3 toneMapped = ACESHDR(linearColorRec709, rangeData.maxNits / rangeData.paperWhiteNits);
        float3 rec2020 = mul(REC709_TO_REC2020, toneMapped);
        float3 nitsNormalized = rec2020 * (rangeData.paperWhiteNits / 10000.0f);
        return linearToPQ(nitsNormalized);
     }

    //float3 paperwhiteMidGrey = (linearColorRec709 / 0.18f) * AGXDefaultContrastApprox(params.paperWhiteNits / 203.0f).x;
    float3 toneMapped = AGXHDR(linearColorRec709, rangeData);
    float3 rec2020 = mul(REC709_TO_REC2020, toneMapped);
    return linearToPQ(rec2020 / 10000.0f);
    //return linearToPQ(rec2020);
}
