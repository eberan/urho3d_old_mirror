#include "Uniforms.vert"
#include "Transform.vert"
#include "ScreenPos.vert"
#include "Lighting.vert"

varying vec2 vTexCoord;
#ifdef PERPIXEL
    varying vec4 vLightVec;
    #ifdef SPECULAR
        varying vec3 vEyeVec;
    #endif
    #ifndef NORMALMAP
        varying vec3 vNormal;
    #endif
    #ifdef SHADOW
        varying vec4 vShadowPos[NUMCASCADES];
    #endif
    #ifdef SPOTLIGHT
        varying vec4 vSpotPos;
    #endif
    #ifdef POINTLIGHT
        varying vec3 vCubeMaskVec;
    #endif
#else
    varying vec4 vVertexLight;
    varying vec3 vNormal;
    #ifdef NORMALMAP
        varying vec3 vTangent;
        varying vec3 vBitangent;
    #endif
    varying vec4 vScreenPos;
    #ifdef ENVCUBEMAP
        varying vec3 vReflectionVec;
    #endif
    #ifdef LIGHTMAP
        varying vec2 vTexCoord2;
    #endif
#endif

void main()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetTexCoord(iTexCoord);

    #if defined(PERPIXEL) && defined(NORMALMAP)
        vec3 vNormal;
        vec3 vTangent;
        vec3 vBitangent;
    #endif

    vNormal = GetWorldNormal(modelMatrix);
    #ifdef NORMALMAP
        vTangent = GetWorldTangent(modelMatrix);
        vBitangent = cross(vTangent, vNormal) * iTangent.w;
    #endif
    
    #ifdef PERPIXEL
        // Per-pixel forward lighting
        vec4 projWorldPos = vec4(worldPos, 1.0);

        #ifdef SHADOW
            // Shadow projection: transform from world space to shadow space
            for (int i = 0; i < NUMCASCADES; i++)
                vShadowPos[i] = GetShadowPos(i, projWorldPos);
        #endif

        #ifdef SPOTLIGHT
            // Spotlight projection: transform from world space to projector texture coordinates
            vSpotPos = cLightMatrices[0] * projWorldPos;
        #endif
    
        #ifdef POINTLIGHT
            vCubeMaskVec = mat3(cLightMatrices[0][0].xyz, cLightMatrices[0][1].xyz, cLightMatrices[0][2].xyz) * (cLightPos.xyz - worldPos);
        #endif
    
        #ifdef NORMALMAP
            mat3 tbn = mat3(vTangent, vBitangent, vNormal);
            #ifdef DIRLIGHT
                vLightVec = vec4(cLightDir * tbn, GetDepth(gl_Position));
            #else
                vLightVec = vec4((cLightPos.xyz - worldPos) * tbn * cLightPos.w, GetDepth(gl_Position));
            #endif
            #ifdef SPECULAR
                vEyeVec = (cCameraPos - worldPos) * tbn;
            #endif
        #else
            #ifdef DIRLIGHT
                vLightVec = vec4(cLightDir, GetDepth(gl_Position));
            #else
                vLightVec = vec4((cLightPos.xyz - worldPos) * cLightPos.w, GetDepth(gl_Position));
            #endif
            #ifdef SPECULAR
                vEyeVec = cCameraPos - worldPos;
            #endif
        #endif
    #else
        // Ambient & per-vertex lighting
        #ifndef LIGHTMAP
            vVertexLight = vec4(GetAmbient(GetZonePos(worldPos)), GetDepth(gl_Position));
        #else
            // If using lightmap, disregard zone ambient light
            vVertexLight = vec4(0.0, 0.0, 0.0, GetDepth(gl_Position));
            vTexCoord2 = iTexCoord2;
        #endif
        
        #ifdef NUMVERTEXLIGHTS
            for (int i = 0; i < NUMVERTEXLIGHTS; ++i)
                vVertexLight.rgb += GetVertexLight(i, worldPos, vNormal) * cVertexLights[i * 3].rgb;
        #endif
        
        vScreenPos = GetScreenPos(gl_Position);

        #ifdef ENVCUBEMAP
            vReflectionVec = worldPos - cCameraPos;
        #endif
    #endif
}
