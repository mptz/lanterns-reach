// Shared helpers. Included via GL_GOOGLE_include_directive.

// Octahedral normal encoding into [0,1]^2 (stored in R16G16_UNORM).
vec2 signNotZero(vec2 v) { return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }

vec2 octEncode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * signNotZero(n.xy);
    return e * 0.5 + 0.5;
}

vec3 octDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy -= t * signNotZero(n.xy);
    return normalize(n);
}

// A well-mixed integer hash, so neighbouring ids get unrelated colors
// (display values in [0.2, 1]).
vec3 idColor(uint id) {
    uint h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return vec3(uvec3(h, h >> 8u, h >> 16u) & 0xFFu) / 255.0 * 0.8 + 0.2;
}
