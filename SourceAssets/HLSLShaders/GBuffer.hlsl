#include "Uniforms.hlsl"
#include "Samplers.hlsl"
#include "Transform.hlsl"
#include "Fog.hlsl"

void VS(float4 iPos : POSITION,
    float3 iNormal : NORMAL,
    #ifdef NORMALMAP
        float4 iTangent : TANGENT0,
    #endif
    float2 iTexCoord : TEXCOORD0,
    #ifdef SKINNED
        float4 iBlendWeights : BLENDWEIGHT,
        int4 iBlendIndices : BLENDINDICES,
    #endif
    #ifdef INSTANCED
        float4x3 iModelInstance : TEXCOORD2,
    #endif
    out float2 oTexCoord : TEXCOORD0,
    #ifndef HWDEPTH
        out float oDepth : TEXCOORD1,
    #endif
    #ifdef NORMALMAP
        out float3 oNormal : TEXCOORD2,
        out float3 oTangent : TEXCOORD3,
        out float3 oBitangent : TEXCOORD4,
    #else
        out float3 oNormal : TEXCOORD2,
    #endif
    out float4 oPos : POSITION)
{
    #if defined(SKINNED)
        #ifndef NORMALMAP
            GetPositionNormalSkinned(iPos, iNormal, iBlendWeights, iBlendIndices, oPos, oNormal);
        #else
            GetPositionNormalTangentSkinned(iPos, iNormal, iTangent, iBlendWeights, iBlendIndices, oPos, oNormal, oTangent);
        #endif
    #elif defined(INSTANCED)
        #ifndef NORMALMAP
            GetPositionNormalInstanced(iPos, iNormal, iModelInstance, oPos, oNormal);
        #else
            GetPositionNormalTangentInstanced(iPos, iNormal, iTangent, iModelInstance, oPos, oNormal, oTangent);
        #endif
    #else
        #ifndef NORMALMAP
            GetPositionNormal(iPos, iNormal, oPos, oNormal);
        #else
            GetPositionNormalTangent(iPos, iNormal, iTangent, oPos, oNormal, oTangent);
        #endif
    #endif

    #ifdef NORMALMAP
        oBitangent = cross(oTangent, oNormal) * iTangent.w;
    #endif

    oTexCoord = GetTexCoord(iTexCoord);
    #ifndef HWDEPTH
        oDepth = GetDepth(oPos);
    #endif
}

void PS(
    float2 iTexCoord : TEXCOORD0,
    #ifndef HWDEPTH
        float iDepth : TEXCOORD1,
    #endif
    #ifdef NORMALMAP
        float3 iNormal : TEXCOORD2,
        float3 iTangent : TEXCOORD3,
        float3 iBitangent : TEXCOORD4,
    #else
        float3 iNormal : TEXCOORD2,
    #endif
    #if !defined(HWDEPTH) && !defined(FALLBACK)
        out float4 oDepth : COLOR0,
        out float4 oNormal : COLOR1)
    #else
        out float4 oNormal : COLOR0)
    #endif
{
    #ifdef ALPHAMASK
        float4 diffInput = tex2D(sDiffMap, iTexCoord);
        if (diffInput.a < 0.5)
            discard;
    #endif

    #ifdef NORMALMAP
        float3x3 tbn = float3x3(iTangent, iBitangent, iNormal);
        float3 normal = normalize(mul(UnpackNormal(tex2D(sNormalMap, iTexCoord)), tbn));
    #else
        float3 normal = normalize(iNormal);
    #endif

    #ifdef FALLBACK
        // Fallback mode uses 15-bit linear depth
        oNormal = PackNormalDepth(normal, iDepth.x);
    #else
        #ifdef SPECMAP
            float specStrength = tex2D(sSpecMap, iTexCoord).r * cMatSpecProperties.x;
        #else
            float specStrength = cMatSpecProperties.x;
        #endif
        float specPower = cMatSpecProperties.y / 255.0;

        oNormal = float4(normal * 0.5 + 0.5, specPower);
        #ifndef HWDEPTH
            oDepth = iDepth;
        #endif
    #endif
}
