#version 330

uniform sampler2D lightmap;
uniform vec2 resolution;

in vec4 fragColor;

out vec4 finalColor;

void main(void) {
    vec2 fragcoord = gl_FragCoord.st/resolution;
    float light = texture(lightmap, fragcoord).a;

    if (light > 0) {
        finalColor = vec4(1,1,1,smoothstep(0.0,0.3,light)) * fragColor;
    } else {
        finalColor = vec4(0,0,0,0);
    }
}
