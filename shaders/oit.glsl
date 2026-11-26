// Moment-based OIT math (Münstermann et al. 2018) with VOLUMETRIC absorbance
// from boundaries (render/transparency). Valid GLSL and valid C++ with glm:
// sandbox_tests includes this file and tests these exact functions. Plain
// float / vec4 math only: no swizzles, no arrays, no `inout`, f-suffixed
// literals.
//
// Depth is r, the DISTANCE FROM THE EYE along the pixel's ray (not view-space
// depth: Σ ±σ·r over a ray is σ × path length only in ray distance). The
// paper's absorbance is the cumulative distribution of a non-negative measure
// along the ray, stored as its moments b_k = E_Z(w^k) in log-warped depth w.
// Our measure is σ·dr per medium, so a medium entered at r_in and left at
// r_out contributes σ·∫ w(r)^k dr = σ·(F_k(r_out) − F_k(r_in)): a face adds
// −σ·F_k(r) when front-facing (entering) and +σ·F_k(r) when back-facing
// (leaving), with F_k the closed-form antiderivative of w^k. k = 0 is the
// total absorbance σ × path length.

const float kOitNear = 0.05f;   // translucent depth range (fixed harness constants);
const float kOitFar = 128.0f;   // F_k is measured from kOitNear (limits fp32 cancellation)

// Logarithmic warp of [kOitNear, kOitFar] to [−1, 1] (paper §3.3, applied to r).
float oitWarpScale() { return 2.0f / (log(kOitFar) - log(kOitNear)); }
float oitWarp(float r) { return (log(r) - log(kOitNear)) * oitWarpScale() - 1.0f; }

struct OitMoments {
    float b0;  // ∫ dZ: total absorbance
    vec4 b;    // ∫ w^k dZ, k = 1..4
};

// G_k(r) = ∫ w(r)^k dr. With u = w(r) = a·ln r + c, repeated integration by
// parts gives G_k = r · Σ_j (−1)^j · k!/(k−j)! · a^j · u^(k−j).
OitMoments oitAntiderivativeRaw(float r) {
    float a = oitWarpScale();
    float u = oitWarp(r);
    float a2 = a * a;
    float a3 = a2 * a;
    float u2 = u * u;
    float u3 = u2 * u;
    OitMoments g;
    g.b0 = r;
    g.b = vec4(r * (u - a),
               r * (u2 - 2.0f * a * u + 2.0f * a2),
               r * (u3 - 3.0f * a * u2 + 6.0f * a2 * u - 6.0f * a3),
               r * (u2 * u2 - 4.0f * a * u3 + 12.0f * a2 * u2 - 24.0f * a3 * u + 24.0f * a2 * a2));
    return g;
}

// F_k(r) = G_k(r) − G_k(kOitNear), with r clamped into the warp range (an eye
// inside a medium starts its path at the near bound; beyond the far bound
// nothing more accumulates).
OitMoments oitAntiderivative(float r) {
    OitMoments g = oitAntiderivativeRaw(clamp(r, kOitNear, kOitFar));
    OitMoments g0 = oitAntiderivativeRaw(kOitNear);
    g.b0 = g.b0 - g0.b0;
    g.b = g.b - g0.b;
    return g;
}

// The boundary term of one face fragment: sign · σ · F(min(r, rOpaque)), with
// sign −1 entering (front-facing) and +1 leaving (back-facing). Clamping to
// the opaque surface ends a path at the wall; volumes wholly behind it cancel.
OitMoments oitBoundaryTerm(float sigma, float r, float rOpaque, bool frontFacing) {
    OitMoments f = oitAntiderivative(min(r, rOpaque));
    float s = frontFacing ? -sigma : sigma;
    f.b0 = f.b0 * s;
    f.b = f.b * s;
    return f;
}

// Paper Algorithm 1 for 4 power moments, from the authors' code
// (computeTransmittanceAtDepthFrom4PowerMoments), returning the NORMALIZED
// absorbance in front of warped depth `depth` (a fraction in [0, 1] of the
// total; multiply by a channel's total absorbance). `b` holds b1..b4 already
// divided by b0. biasVector = (0, 0.375, 0, 0.375) for single precision.
float oitNormalizedAbsorbance(vec4 bNormalized, float depth, float bias, float overestimation) {
    vec4 b = mix(bNormalized, vec4(0.0f, 0.375f, 0.0f, 0.375f), bias);
    float z0 = depth;
    // Cholesky factorization of the Hankel matrix (non-trivial entries only).
    float L21D11 = b.z - b.x * b.y;
    float D11 = b.y - b.x * b.x;
    float InvD11 = 1.0f / D11;
    float L21 = L21D11 * InvD11;
    float SquaredDepthVariance = b.w - b.y * b.y;
    float D22 = SquaredDepthVariance - L21D11 * L21;
    // Scaled inverse image of (1, z0, z0²): forward substitution, scaling,
    // backward substitution.
    float c0 = 1.0f;
    float c1 = z0;
    float c2 = z0 * z0;
    c1 = c1 - b.x;
    c2 = c2 - (b.y + L21 * c1);
    c1 = c1 * InvD11;
    c2 = c2 / D22;
    c1 = c1 - L21 * c2;
    c0 = c0 - (c1 * b.x + c2 * b.y);
    // Roots z1, z2 of c0 + c1·z + c2·z².
    float InvC2 = 1.0f / c2;
    float p = c1 * InvC2;
    float q = c0 * InvC2;
    float D = p * p * 0.25f - q;
    float rt = sqrt(D);
    float z1 = -p * 0.5f - rt;
    float z2 = -p * 0.5f + rt;
    // Sum the weights of the canonical representation in front of z0
    // (interpolating toward the upper bound by `overestimation`).
    float f0 = overestimation;
    float f1 = z1 < z0 ? 1.0f : 0.0f;
    float f2 = z2 < z0 ? 1.0f : 0.0f;
    float f01 = (f1 - f0) / (z1 - z0);
    float f12 = (f2 - f1) / (z2 - z1);
    float f012 = (f12 - f01) / (z2 - z0);
    float poly0 = f012;
    float poly1 = poly0;
    poly0 = f01 - poly0 * z1;
    float poly2 = poly1;
    poly1 = poly0 - poly1 * z0;
    poly0 = f0 - poly0 * z0;
    return clamp(poly0 + b.x * poly1 + b.y * poly2, 0.0f, 1.0f);
}
