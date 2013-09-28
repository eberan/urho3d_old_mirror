#include "Uniforms.frag"
#include "Samplers.frag"
#include "Fog.frag"

varying vec2 vTexCoord;
#ifdef VERTEXCOLOR
    varying vec4 vColor;
#endif
varying float vDepth;

void main()
{
    #ifdef DIFFMAP
        vec4 diffColor = cMatDiffColor * texture2D(sDiffMap, vTexCoord);
        #ifdef ALPHAMASK
            if (diffColor.a < 0.5)
                discard;
        #endif
    #else
        vec4 diffColor = cMatDiffColor;
    #endif

    #ifdef VERTEXCOLOR
        diffColor *= vColor;
    #endif

    gl_FragColor = vec4(GetFog(diffColor.rgb, vDepth), diffColor.a);
}

