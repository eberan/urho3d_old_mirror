#include "Uniforms.frag"
#include "Samplers.frag"
#include "Lighting.frag"
#include "Fog.frag"

varying vec4 vTexCoord;
#ifdef VERTEXCOLOR
    varying vec4 vColor;
#endif
varying vec3 vLightVec;
#ifdef SPECULAR
    varying vec3 vEyeVec;
#endif
#ifndef NORMALMAP
    varying vec3 vNormal;
#endif
#ifdef SHADOW
    #if defined(DIRLIGHT)
        varying vec4 vShadowPos[4];
    #elif defined(SPOTLIGHT)
        varying vec4 vShadowPos;
    #else
        varying vec3 vShadowPos;
    #endif
#endif
#ifdef SPOTLIGHT
    varying vec4 vSpotPos;
#endif
#ifdef POINTLIGHT
    varying vec3 vCubeMaskVec;
#endif

void main()
{
    #ifdef DIFFMAP
        vec4 diffColor = cMatDiffColor * texture2D(sDiffMap, vTexCoord.xy);
    #else
        vec4 diffColor = cMatDiffColor;
    #endif

    #ifdef VERTEXCOLOR
        diffColor *= vColor;
    #endif

    vec3 lightColor;
    vec3 lightDir;
    vec3 finalColor;
    float diff;

    #ifdef NORMALMAP
        vec3 normal = DecodeNormal(texture2D(sNormalMap, vTexCoord.xy));
    #else
        vec3 normal = normalize(vNormal);
    #endif

    #ifdef DIRLIGHT
        #ifdef NORMALMAP
            lightDir = normalize(vLightVec);
        #else
            lightDir = vLightVec;
        #endif
        diff = GetDiffuseDir(normal, lightDir);
    #else
        diff = GetDiffusePointOrSpot(normal, vLightVec, lightDir);
    #endif

    #ifdef SHADOW
        #if defined(DIRLIGHT)
            vec4 shadowPos = GetDirShadowPos(vShadowPos, vTexCoord.w);
            diff *= min(GetShadow(shadowPos) + GetShadowFade(vTexCoord.w), 1.0);
        #elif defined(SPOTLIGHT)
            diff *= GetShadow(vShadowPos);
        #else
            diff *= GetCubeShadow(vShadowPos);
        #endif
    #endif

    #if defined(SPOTLIGHT)
        lightColor = vSpotPos.w > 0.0 ? texture2DProj(sLightSpotMap, vSpotPos).rgb * cLightColor.rgb : vec3(0.0, 0.0, 0.0);
    #elif defined(CUBEMASK)
        lightColor = textureCube(sLightCubeMap, vCubeMaskVec).rgb * cLightColor.rgb;
    #else
        lightColor = cLightColor.rgb;
    #endif

    #ifdef SPECULAR
        #ifdef SPECMAP
            float specStrength = cMatSpecProperties.x * texture2D(sSpecMap, vTexCoord.xy).g;
        #else
            float specStrength = cMatSpecProperties.x;
        #endif
        float spec = GetSpecular(normal, vEyeVec, lightDir, cMatSpecProperties.y);
        finalColor = diff * lightColor * (diffColor.rgb + spec * specStrength * cLightColor.a);
    #else
        finalColor = diff * lightColor * diffColor.rgb;
    #endif

    #ifdef AMBIENT
        finalColor += GetAmbient(vTexCoord.z) * diffColor.rgb;
        gl_FragColor = vec4(GetFog(finalColor, vTexCoord.w), diffColor.a);
    #else
        gl_FragColor = vec4(GetLitFog(finalColor, vTexCoord.w), diffColor.a);
    #endif
}