#include "Uniforms.frag"
#include "Samplers.frag"
#include "Fog.frag"

varying vec2 vScreenPos;

void main()
{
    float depth = texture2D(sDepthBuffer, vScreenPos).r;

    gl_FragColor = vec4(cFogColor, 1.0);
    // Copy the actual hardware depth value with slight bias to the destination depth buffer
    gl_FragDepth = min(depth + 0.0000001, 1.0);
}
