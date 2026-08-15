#pragma once


static const uint spotLight = 0;
static const uint pointLight = 1;
static const uint directionalLight = 2;

static const float pi = 3.14159265f;
static const float pi2 = pi * pi;
static const float oneOverPi = 1.0f / pi;
static const float oneOverPi2 = 1.0f / pi2;

static const uint CANDIDATE_AABB = 1;
static const uint CANDIDATE_TRIANGLE = 2;

void BuildOrthonormalBasis(float3 n, out float3 tangent, out float3 bitangent)
{
    float sign = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + n.z);
    float b = n.x * n.y * a;

    tangent   = float3(1.0 + sign * n.x * n.x * a, sign * b, -sign * n.x);
    bitangent = float3(b, sign + n.y * n.y * a, -n.y);
}
