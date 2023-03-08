#version 330

#define PI 3.14

uniform sampler2D texture0;
uniform int shadow_resolution;

out vec4 finalColor;

const float THRESHOLD = 0.75;

void main(void) {
    float distance = 1.0;

    float theta = PI*1.5 + (2.0*gl_FragCoord.s/float(shadow_resolution) - 1.0) * PI;
    for (float y = 0.0; y < float(shadow_resolution); y += 1.0) {
        float r = y/float(shadow_resolution);

        vec2 coord = vec2(0.5,0.5) + (-r/2.0)*vec2(sin(theta), cos(theta));
        //vec2 coord = vec2(0.5,0.5) + (r/2.0)*vec2(cos(theta), sin(theta));

        float occlusion = texture(texture0, coord).r;

        if (occlusion > THRESHOLD) {
            distance = min(distance, r);
        }
    }

    finalColor = vec4(vec3(distance), 1.0);
}
