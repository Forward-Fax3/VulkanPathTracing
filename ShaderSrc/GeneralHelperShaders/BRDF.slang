#pragma once
#include "Core.slang"


struct GGXResult
{
    float3 value;
    float pdf;
}

struct BRDFSample
{
    float3 direction;
    float3 value;
    float pdf;
}


// Trowbridge-Reitz
float TR_GGX(float NDotH, float alpha2)
{
    float denom = NDotH * NDotH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (pi * denom * denom);
}

float V_SmithGGXCorrelated(float NDotV, float NDotL, float alpha2)
{
    float GGXV = NDotL * sqrt(NDotV * NDotV * (1.0f - alpha2) + alpha2);
    float GGXL = NDotV * sqrt(NDotL * NDotL * (1.0f - alpha2) + alpha2);
    return 0.5f / max(GGXV + GGXL, 1e-9f);
}

float3 FresnelSchlick(float VDotH, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - VDotH, 5.0f);
}


GGXResult EvalGGXSpecular(float3 n, float3 v, float3 l, float roughness, float3 F0)
{
    GGXResult result;
    result.value = 0.0f;
    result.pdf = 0.0f;

    float NDotL = saturate(dot(n, l));
    float NDotV = saturate(dot(n, v));
    if (NDotV <= 0.0f || NDotL <= 0.0f)
        return result;

    float3 h = normalize(v + l);
    float NDotH = saturate(dot(n, h));
    float VDotH = saturate(dot(v, h));

    float alpha2 = max(roughness * roughness, 1e-4);
    float D = TR_GGX(NDotH, alpha2);
    float V = V_SmithGGXCorrelated(NDotV, NDotL, alpha2);
    float3 F = FresnelSchlick(VDotH, F0);

    result.value = (D * V) * F;
    result.pdf = D * NDotH / max(4.0f * VDotH, 1e-4f);
    return result;
}

float3 SampleGGXVNDF(float3 vLocal, float alpha, float2 xi)
{
    float3 Vh = normalize(float3(alpha * vLocal.x, alpha * vLocal.y, vLocal.z));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0f ? float3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    float3 T2 = cross(Vh, T1);

    // Sample point with polar coordinates (r, phi)
    float r = sqrt(xi.x);
    float phi = 2.0f * pi * xi.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * sqrt(1.0f - t1 * t1) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;
    return normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0f, Nh.z)));
}


BRDFSample SampleGGX(float3 n, float3 v, float roughness, float3 F0, float2 xi, float3 tangent, float3 bitangent)
{
    BRDFSample result;
    result.direction = 0.0f;
    result.value = 0.0f;
    result.pdf = 0.0f;

    float alpha = max(roughness * roughness, 1e-4f);
    float3 vLocal = float3(dot(v, tangent), dot(v, bitangent), dot(v, n));
    if (vLocal.z <= 0.0f)
        return result;

    float3 hLocal = SampleGGXVNDF(vLocal, alpha, xi);
    float3 h = normalize(hLocal.x * tangent + hLocal.y * bitangent + hLocal.z * n);
    float3 l = reflect(-v, h);

    GGXResult evalResult = EvalGGXSpecular(n, v, l, roughness, F0);
    result.direction = l;
    result.value = evalResult.value;
    result.pdf = evalResult.pdf;

    return result;
}

float3 EvalBRDF(float3 n, float3 v, float3 l, float3 albedo, float metallic, float roughness)
{
    float NDotL = saturate(dot(n, l));
    if (NDotL <= 0.0f)
        return float3(0.0f);

    float3 F0 = lerp(float3(0.04f), albedo, metallic);
    GGXResult evalResult = EvalGGXSpecular(n, v, l, roughness, F0);

    float3 diffuse = (1.0f - metallic) * albedo * oneOverPi;

    return diffuse * NDotL + evalResult.value * NDotL;
}
