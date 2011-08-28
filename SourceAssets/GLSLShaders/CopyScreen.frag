#include "Uniforms.frag"
#include "Samplers.frag"
#include "Fog.frag"

varying vec2 vScreenPos;

void main()
{
    gl_FragColor = texture2D(sDiffMap, vScreenPos);
}
