#include "Uniforms.frag"
#include "Samplers.frag"
#include "Lighting.frag"

#ifdef DIRLIGHT
varying vec2 vScreenPos;
#else
varying vec4 vScreenPos;
#endif
varying vec3 vFarRay;
#ifdef ORTHO
varying vec3 vNearRay;
#endif

void main()
{
    // If rendering a directional light quad, optimize out the w divide
    #ifdef DIRLIGHT
        #ifdef ORTHO
            float depth = UnpackDepth(texture2D(sDepthBuffer, vScreenPos).rgb);
            vec3 worldPos = mix(vNearRay, vFarRay, depth);
        #else
            #ifdef LINEAR
                float depth = UnpackDepth(texture2D(sDepthBuffer, vScreenPos).rgb);
            #else
                float depth = ReconstructDepth(texture2D(sDepthBuffer, vScreenPos).r);
            #endif
            vec3 worldPos = vFarRay * depth;
        #endif
        vec4 normalInput = texture2D(sNormalBuffer, vScreenPos);
    #else
        #ifdef ORTHO
            float depth = UnpackDepth(texture2DProj(sDepthBuffer, vScreenPos).rgb);
            vec3 worldPos = mix(vNearRay, vFarRay, depth) / vScreenPos.w;
        #else
            #ifdef LINEAR
                float depth = UnpackDepth(texture2DProj(sDepthBuffer, vScreenPos).rgb);
            #else
                float depth = ReconstructDepth(texture2DProj(sDepthBuffer, vScreenPos).r);
            #endif
            vec3 worldPos = vFarRay * depth / vScreenPos.w;
        #endif
        vec4 normalInput = texture2DProj(sNormalBuffer, vScreenPos);
    #endif

    // With specular, normalization greatly improves stability of reflections,
    // considering input is only 8 bits per axis
    #ifdef SPECULAR
        vec3 normal = normalize(normalInput.rgb * 2.0 - 1.0);
    #else
        vec3 normal = normalInput.rgb * 2.0 - 1.0;
    #endif

    vec3 lightColor;
    vec3 lightDir;
    float diff;

    // Accumulate light at half intensity to allow 2x "overburn"
    #ifdef DIRLIGHT
        diff = 0.5 * GetDiffuseDir(normal, lightDir) * GetSplitFade(depth);
    #else
        vec3 lightVec;
        diff = 0.5 * GetDiffusePointOrSpot(normal, worldPos, lightDir, lightVec);
    #endif

    #ifdef SHADOW
        vec4 shadowPos = cShadowProjPS * vec4(worldPos, 1.0);
        diff *= GetShadow(shadowPos);
    #endif

    #ifdef SPOTLIGHT
        vec4 spotPos = cSpotProjPS * vec4(worldPos, 1.0);
        lightColor = spotPos.w > 0.0 ? texture2DProj(sLightSpotMap, spotPos).rgb * cLightColor.rgb : vec3(0.0, 0.0, 0.0);
    #else
        #ifdef CUBEMASK
            lightColor = textureCube(sLightCubeMap, cLightVecRot * lightVec).rgb * cLightColor.rgb;
        #else
            lightColor = cLightColor.rgb;
        #endif
    #endif

    #ifdef SPECULAR
        float spec = lightColor.g * GetSpecular(normal, worldPos, lightDir, normalInput.a * 255.0);
        gl_FragColor = diff * vec4(lightColor, spec * cLightColor.a);
    #else
        gl_FragColor = diff * vec4(lightColor, 0.0);
    #endif
}
