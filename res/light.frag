#version 330

#define PI 3.14

uniform sampler2D texture0;
uniform vec2 light_pos;
uniform vec2 resolution;
uniform int shadow_resolution;
uniform int light_resolution;

uniform int light_mode;
uniform float cone_angle;
uniform float cone_width;
uniform float cone_length;

in vec4 fragColor;

out vec4 finalColor;

float constrain_angle(float theta, float rot) {
    float angle = mod(theta - rot, 2.0*PI);
    if (angle < 0)
        angle += 2.0*PI;
    return angle + rot;
}

float sample_cone(float angle, float r) {
    angle = 2.0*PI - angle;

    float lower = constrain_angle(cone_angle - cone_width/2.0, PI + cone_angle);
    float upper = constrain_angle(cone_angle + cone_width/2.0, PI + cone_angle);
    float a = constrain_angle(angle, PI + cone_angle);

    if (a >= lower && a <= upper) {
        return step(r, texture(texture0, vec2(angle/(2.0*PI), 0.0)).r);
    } else {
        return 0.0;
    }
}

float sample_point(float angle, float r) {
    angle = 2.0*PI - angle;
    return step(r, texture(texture0, vec2(angle/(2.0*PI), 0.0)).r);
}

void main(void) {
    vec2 normalized_light_pos = vec2(light_pos.x/resolution.x, 1.0 - light_pos.y/resolution.y);
    vec2 normalized_frag_pos = gl_FragCoord.st/resolution;
    vec2 light_texture_pos = 2.0*(normalized_frag_pos - normalized_light_pos) / (vec2(light_resolution)/resolution);

    float radius = length(light_texture_pos);

    float theta = atan(light_texture_pos.y, light_texture_pos.x);

    // [0,2pi]
    float shadowmap_angle = theta + PI;

    float blur = (1.0/float(shadow_resolution)) * smoothstep(0.0, 1.0, radius);

    float sum = 0.0;

    float center = 0.0;

    if (light_mode == 0) {
        center = sample_point(shadowmap_angle, radius);

        sum += sample_point(shadowmap_angle - 2.0*PI*4.0*blur, radius) * 0.05;
        sum += sample_point(shadowmap_angle - 2.0*PI*3.0*blur, radius) * 0.09;
        sum += sample_point(shadowmap_angle - 2.0*PI*2.0*blur, radius) * 0.12;
        sum += sample_point(shadowmap_angle - 2.0*PI*1.0*blur, radius) * 0.15;

        sum += center * 0.16;

        sum += sample_point(shadowmap_angle + 2.0*PI*1.0*blur, radius) * 0.15;
        sum += sample_point(shadowmap_angle + 2.0*PI*2.0*blur, radius) * 0.12;
        sum += sample_point(shadowmap_angle + 2.0*PI*3.0*blur, radius) * 0.09;
        sum += sample_point(shadowmap_angle + 2.0*PI*4.0*blur, radius) * 0.05;
    } else if (light_mode == 1) {
        center = sample_cone(shadowmap_angle, radius);

        sum += sample_cone(shadowmap_angle - 2.0*PI*4.0*blur, radius) * 0.05;
        sum += sample_cone(shadowmap_angle - 2.0*PI*3.0*blur, radius) * 0.09;
        sum += sample_cone(shadowmap_angle - 2.0*PI*2.0*blur, radius) * 0.12;
        sum += sample_cone(shadowmap_angle - 2.0*PI*1.0*blur, radius) * 0.15;

        sum += center * 0.16;

        sum += sample_cone(shadowmap_angle + 2.0*PI*1.0*blur, radius) * 0.15;
        sum += sample_cone(shadowmap_angle + 2.0*PI*2.0*blur, radius) * 0.12;
        sum += sample_cone(shadowmap_angle + 2.0*PI*3.0*blur, radius) * 0.09;
        sum += sample_cone(shadowmap_angle + 2.0*PI*4.0*blur, radius) * 0.05;
    }

    finalColor = fragColor * vec4(vec3(1.0), sum * smoothstep(cone_length, 0.0, radius));
    //finalColor = fragColor * vec4(vec3(1.0), center);
    //finalColor = vec4(vec3(radius),1);
}
