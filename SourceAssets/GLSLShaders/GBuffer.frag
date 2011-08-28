#include "Uniforms.frag"
#include "Samplers.frag"
#include "Fog.frag"

varying vec2 vTexCoord;
#ifndef HWDEPTH
varying float vDepth;
#endif
varying vec3 vNormal;
#ifdef NORMALMAP
varying vec3 vTangent;
varying vec3 vBitangent;
#endif

void main()
{
    #ifdef ALPHAMASK
        vec4 diffInput = texture2D(sDiffMap, iTexCoord);
        if (diffInput.a < 0.5)
            discard;
    #endif

    #ifdef NORMALMAP
        mat3 tbn = mat3(vTangent, vBitangent, vNormal);
        vec3 normal = normalize(tbn * UnpackNormal(texture2D(sNormalMap, vTexCoord)));
    #else
        vec3 normal = normalize(vNormal);
    #endif

    #ifdef SPECMAP
        float specStrength = texture2D(sSpecMap, vTexCoord).r * cMatSpecProperties.x;
    #else
        float specStrength = cMatSpecProperties.x;
    #endif
    float specPower = cMatSpecProperties.y / 255.0;

    #ifndef HWDEPTH
        gl_FragData[0] = vec4(PackDepth(vDepth), 0.0);
        gl_FragData[1] = vec4(normal * 0.5 + 0.5, specPower);
    #else
        gl_FragColor = vec4(normal * 0.5 + 0.5, specPower);
    #endif
}
