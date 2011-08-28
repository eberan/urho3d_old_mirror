#include "Uniforms.frag"
#include "Samplers.frag"
#include "Fog.frag"

varying vec2 vTexCoord;
varying vec4 vScreenPos;
varying vec4 vWorldPos;

void main()
{
    #ifdef DIFFMAP
        vec4 diffInput = texture2D(sDiffMap, vTexCoord);
        #ifdef ALPHAMASK
            if (diffInput.a < 0.5)
                discard;
        #endif
        vec3 diffColor = cMatDiffColor.rgb * diffInput.rgb;
    #else
        vec3 diffColor = cMatDiffColor.rgb;
    #endif

    #ifdef SPECMAP
        float specIntensity = cMatSpecProperties.x * tex2D(sSpecMap, vTexCoord).g;
    #else
        float specIntensity = cMatSpecProperties.x;
    #endif

    // Lights are accumulated at half intensity. Bring back to full intensity now
    vec4 lightInput = 2.0 * texture2DProj(sLightBuffer, vScreenPos);
    vec3 lightDiffColor = cAmbientColor + lightInput.rgb;
    // Remove ambient color from the specular color to not get overbright highlights
    vec3 lightSpecColor = lightInput.a * clamp(lightInput.rgb - cAmbientColor, 0.0, 1.0);

    vec3 finalColor = lightInput.rgb * diffColor + lightSpecColor * specIntensity;
    gl_FragColor = vec4(GetFog(finalColor, vWorldPos.w), 1.0);
}
