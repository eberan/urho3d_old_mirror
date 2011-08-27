// Material map samplers
sampler2D sDiffMap : register(S0);
samplerCUBE sDiffCubeMap : register(S0);
sampler2D sNormalMap : register(S1);
sampler2D sSpecMap : register(S2);
sampler2D sDetailMap : register(S3);
sampler2D sEnvMap : register(S4);
samplerCUBE sEnvCubeMap : register(S4);
sampler2D sEmissiveMap : register(S5);

// Lighting samplers
sampler2D sShadowMap : register(S0);
sampler2D sNormalBuffer : register(S1);
sampler2D sDepthBuffer : register(S2);
sampler2D sLightBuffer : register(S6);
sampler1D sLightRampMap : register(S6);
sampler2D sLightSpotMap : register(S7);
samplerCUBE sLightCubeMap : register(S7);

float4 Sample(sampler2D map, float2 texCoord)
{
    // Use tex2Dlod if available to avoid divergence and allow branching
    #ifdef SM3
        return tex2Dlod(map, float4(texCoord, 0.0, 0.0));
    #else
        return tex2D(map, texCoord);
    #endif
}

float3 UnpackNormal(float4 normalInput)
{
    float3 normal;
    normal.xy = normalInput.ag * 2.0 - 1.0;
    normal.z = sqrt(1.0 - dot(normal.xy, normal.xy));
    return normal;
}

float4 PackNormalDepth(float3 normal, float depth)
{
    float4 ret;
    ret.xy = normal.xz * 0.5 + 0.5;
    ret.z = (floor(depth * 127.0) + (normal.y < 0.0) * 128.0) * (1.0 / 255.0);
    ret.w = frac(depth * 127.0);
    return ret;
}

void UnpackNormalDepth(float4 input, out float3 normal, out float depth)
{
    normal.xz = input.xy * 2.0 - 1.0;
    normal.y = sqrt(1.0 - dot(normal.xz, normal.xz));

    float hiDepth = input.z * 255.0;
    if (hiDepth > 127.0)
    {
        hiDepth -= 128.0;
        normal.y = -normal.y;
    }

    depth = (hiDepth + input.w) * (1.0 / 127.0);
}

float ReconstructDepth(float hwDepth)
{
    return cDepthReconstruct.y / (hwDepth - cDepthReconstruct.x);
}

float GetIntensity(float3 color)
{
    const float3 dotValues = 1.0 / 3.0;
    return dot(color, dotValues);
}
