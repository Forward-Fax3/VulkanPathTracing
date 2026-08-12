#pragma once


static constexpr uint spotLight = 0;
static constexpr uint pointLight = 1;
static constexpr uint directionalLight = 2;

static constexpr float pi = 3.14159265f;
static constexpr float pi2 = pi * pi;
static constexpr float oneOverPi = 1.0f / pi;
static constexpr float oneOverPi2 = 1.0f / pi2;

static constexpr uint CANDIDATE_AABB = 1;
static constexpr uint CANDIDATE_TRIANGLE = 2;

void BuildOrthonormalBasis(float3 n, out float3 tangent, out float3 bitangent)
{
    float sign = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + n.z);
    float b = n.x * n.y * a;

    tangent   = float3(1.0 + sign * n.x * n.x * a, sign * b, -sign * n.x);
    bitangent = float3(b, sign + n.y * n.y * a, -n.y);
}
