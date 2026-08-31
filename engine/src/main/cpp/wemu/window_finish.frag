#version 450

/*
 * highp for the same reason the EASU shader beside it is highp.
 *
 * Every limiter below is a reciprocal of a difference of two nearly equal
 * values, and the whole point of the algorithm is that those reciprocals are
 * accurate — upstream's comment on them reads "these need to be high precision
 * RCPs". fp16 has ten bits of mantissa and would quantise the sharpening
 * strength into visible bands.
 */
precision highp float;
precision highp int;

/*
 * The pass that turns the composited image into the one that is presented.
 *
 * Two jobs, deliberately in one shader: FSR's RCAS sharpening, and whichever
 * screen effect the container asked for.
 *
 * <h2>Why they had to be merged</h2>
 *
 * Both are full-screen passes over a finished frame, and the renderer had room
 * for exactly one pipeline per window draw:
 *
 *     activePipeline = useFsr ? fsr : useNis ? nis : useSgsr ? sgsr
 *                    : usePostFX ? postfx : ...
 *
 * An upscaler always won that chain, so choosing one silently turned the screen
 * effects off — with no message, and with the effect still shown as enabled in
 * the console. The interface had grown a rule to match ("sharpness only appears
 * under Screen effects when no upscaler is set") which documented the
 * limitation rather than removing it.
 *
 * They are not really alternatives, though. An upscaler decides how the frame
 * is reconstructed on its way to the panel; an effect decides what colour comes
 * out at the end. Running both is the ordinary case, and the order matters: the
 * effect belongs *after* the upscale, on the pixels that will actually be lit,
 * which is exactly where this pass sits.
 *
 * <h2>Why one pass and not two</h2>
 *
 * A strict chain would sharpen into a second offscreen image and grade that.
 * The effects here are a colour transform plus a small neighbourhood, and the
 * neighbourhood taps are a blur (DLS), a bloom (HDR) or a sub-pixel offset
 * (CRT) — none of which can resolve the difference between a sharpened and an
 * unsharpened neighbour. So the centre pixel is taken from RCAS and the taps
 * from the image RCAS is reading, which costs one render target and one pass
 * less than being pedantic about it would.
 *
 * DLS is the exception and is handled where it is defined: it is itself an
 * unsharp mask, so it drops its own sharpening term when RCAS has already run.
 */

layout(push_constant) uniform PC {
    float ndcX0;
    float ndcY0;
    float ndcX1;
    float ndcY1;
    int   useTexAlpha;
    float invSrcW;
    float invSrcH;
    float srcW;
    float srcH;
    int   effectId;
    /*
     * The effect's own strength, which is not the same number as `sharpness`.
     *
     * Both come from the one dial in the console, but RCAS only runs under FSR
     * — it is the second half of that algorithm and means nothing after SGSR or
     * NIS. So `sharpness` arrives at zero on those paths while this still
     * carries what the user set, and an effect that reads a strength (DLS) is
     * not silently flattened by the upscaler it was combined with.
     */
    float effectAmount;
    float sharpness;
} pc;

layout(binding = 0) uniform mediump sampler2D texSampler;
layout(location = 0) in  highp vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

/*
 * Twice the usual luminance, which is upstream's weighting and is deliberate.
 *
 * It feeds only the noise ratio below, where it appears in both numerator and
 * denominator and the factor of two cancels. Keeping upstream's form means the
 * constant folds the same way and the shader stays comparable to the reference.
 */
float rcasLuma(vec3 c) {
    return c.b * 0.5 + (c.r * 0.5 + c.g);
}

/*
 * Exact texels, by index, clamped at the edges.
 *
 * RCAS is defined on the pixels of the upscaled image, not on a filtered
 * neighbourhood of it. This sampled with `texture()` at coordinate offsets
 * first, which gives the same answer only while the quad maps one-for-one onto
 * the render target and the sampler's linear filtering therefore lands exactly
 * on texel centres. That is true today and is the kind of invariant nobody
 * remembers: a viewport change, or a scissor that scales rather than crops,
 * turns a sharpening pass into a slightly blurring one with nothing to show
 * for it. texelFetch cannot drift.
 */
vec3 rcasTap(ivec2 p, ivec2 lim) {
    return texelFetch(texSampler, clamp(p, ivec2(0), lim), 0).rgb;
}

/*
 * FSR 1.0 RCAS — Robust Contrast Adaptive Sharpening, the second of FSR's two
 * passes.
 *
 * EASU reconstructs the image at output resolution and deliberately clamps
 * every output pixel to the four source texels it sits between, because its
 * kernel has negative lobes that would otherwise ring. That clamp costs the
 * result some of its bite. RCAS puts it back, working on the *upscaled* image
 * where the pixels EASU produced are neighbours of each other — which is why
 * this cannot be folded into EASU and has to be its own pass over its own
 * render target.
 *
 * <h2>Why not an ordinary unsharp mask</h2>
 *
 * Sharpening is an overshoot: push a pixel further from its neighbours than it
 * was. Do that with a fixed strength and flat regions gain noise, near-clipped
 * highlights band, and one-pixel features grow haloes. RCAS instead computes,
 * per pixel and per channel, the largest lobe that still keeps the result
 * inside [0,1] given that pixel's own neighbourhood — the `hitMin`/`hitMax`
 * pair below — takes the most restrictive of the three channels, and clamps
 * that to a hard ceiling. The strength is therefore whatever the local
 * contrast can absorb, which is what the "robust" is doing in the name.
 *
 * <h2>Denoising</h2>
 *
 * `nz` measures how much the centre pixel looks like an isolated spike rather
 * than part of an edge, and halves the lobe where it does. Without it a
 * compressed or dithered source has its noise sharpened along with its detail,
 * which on this device — where the frame arrived over a socket from a Windows
 * guest and has been through a scaler already — is a real and visible effect.
 */
vec3 applyRcas(ivec2 ip, ivec2 lim, vec3 e, float amount) {
    /*
     * A cross, not a square.
     *
     *      b
     *    d e f
     *      h
     *
     * The diagonals are omitted on purpose: they sit further from the centre,
     * so including them would widen the effective sharpening radius and start
     * producing the haloes the limiter exists to prevent.
     */
    vec3 b = rcasTap(ip + ivec2( 0, -1), lim);
    vec3 d = rcasTap(ip + ivec2(-1,  0), lim);
    vec3 f = rcasTap(ip + ivec2( 1,  0), lim);
    vec3 h = rcasTap(ip + ivec2( 0,  1), lim);

    float bL = rcasLuma(b);
    float dL = rcasLuma(d);
    float eL = rcasLuma(e);
    float fL = rcasLuma(f);
    float hL = rcasLuma(h);

    /*
     * How much the centre disagrees with the average of its neighbours,
     * measured against how much they disagree among themselves.
     *
     * On an edge the ring is already spread out, the ratio is small, and
     * sharpening proceeds at full strength. On a lone bright pixel in a flat
     * field the ring agrees with itself and the ratio approaches one, halving
     * the lobe.
     */
    float ring = 0.25 * (bL + dL + fL + hL) - eL;
    float mxL  = max(max(max(bL, dL), max(eL, fL)), hL);
    float mnL  = min(min(min(bL, dL), min(eL, fL)), hL);
    float nz   = clamp(abs(ring) / max(mxL - mnL, 1.0 / 32768.0), 0.0, 1.0);
    nz = -0.5 * nz + 1.0;

    vec3 mn4 = min(min(b, d), min(f, h));
    vec3 mx4 = max(max(b, d), max(f, h));

    /*
     * The largest lobe that keeps the result inside the displayable range.
     *
     * Resolving a pixel costs `(lobe*(b+d+f+h) + e) / (4*lobe + 1)` with a
     * negative lobe, so the two ways to leave [0,1] are undershooting past
     * black and overshooting past white. `hitMin` is the limit set by the
     * former and `hitMax` by the latter, each solved directly rather than
     * found by clamping afterwards — clamping would flatten the overshoot into
     * a hard edge, which reads as a halo of its own.
     *
     * Both denominators are guarded. `4*mn4 - 4` is zero for a pure white
     * neighbourhood, where the numerator is also zero: 0/0 is NaN, and one NaN
     * in a fullscreen pass is a frame of garbage. The guards cost two
     * instructions and remove the case.
     */
    const float eps = 1.0 / 32768.0;
    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(eps));
    vec3 hitMax = (vec3(1.0) - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-eps));

    vec3  lobeRGB = max(-hitMin, hitMax);
    float lobe    = max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b);

    // Never sharper than upstream's ceiling, never a negative-of-negative that
    // would blur, and then scaled by the dial. At 1.0 this is stock RCAS at
    // maximum; at 0.5 it is one stop softer.
    lobe = max(-(0.25 - 1.0 / 16.0), min(lobe, 0.0)) * amount;
    lobe *= nz;

    // 4*lobe is at worst -0.75, so this reciprocal cannot blow up.
    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    return clamp((lobe * (b + d + f + h) + e) * rcpL, 0.0, 1.0);
}

/* ------------------------------------------------------------------------
 * Screen effects.
 *
 * Ported unchanged in arithmetic from `window_postfx.frag`, which still runs
 * them per window on the path where no upscaler is engaged. Two differences,
 * both forced by where this pass sits:
 *
 *   - the centre pixel arrives as an argument, because RCAS may already have
 *     produced it, rather than being sampled again here;
 *   - the resolution comes from `srcW`/`srcH`, this pass's own push constants,
 *     rather than the per-window `resW`/`resH` the other shader is given.
 * ------------------------------------------------------------------------ */

/*
 * `alreadySharpened` is what keeps DLS from fighting RCAS.
 *
 * DLS is a saturation and contrast lift plus an unsharp mask. Run it after
 * RCAS with its mask intact and the frame is sharpened twice — once adaptively
 * and once not — which produces exactly the haloing RCAS's limiter exists to
 * avoid. The grade is what the effect is for and it is kept; the second
 * sharpener is dropped.
 */
vec3 applyDLS(vec2 uv, vec3 centre, float sharp, bool alreadySharpened) {
    vec2  texel = 1.0 / max(vec2(pc.srcW, pc.srcH), vec2(1.0));
    float SAT   = 1.0 + sharp * 0.20;
    float CON   = 1.0 + sharp * 0.12;
    float SHARP = alreadySharpened ? 0.0 : sharp * 1.2;
    vec3 c    = clamp((centre - 0.5) * CON + 0.5, 0.0, 1.0);
    float gray = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(gray), c, SAT);
    if (SHARP <= 0.0) return clamp(c, 0.0, 1.0);
    vec3 blur = (texture(texSampler, uv + vec2( 0.0,    -texel.y)).rgb
               + texture(texSampler, uv + vec2( 0.0,     texel.y)).rgb
               + texture(texSampler, uv + vec2(-texel.x,  0.0   )).rgb
               + texture(texSampler, uv + vec2( texel.x,  0.0   )).rgb) * 0.25;
    return clamp(c + (centre - blur) * SHARP, 0.0, 1.0);
}

vec3 applyCRT(vec2 uv, vec3 centre) {
    const float CA = 1.0025;
    vec3 fc = centre;
    fc.r = texture(texSampler, (uv - 0.5) * CA + 0.5).r;
    fc.b = texture(texSampler, (uv - 0.5) / CA + 0.5).b;
    float sx = abs(sin(uv.x * 1024.0) * 0.5 * 0.125);
    float sy = abs(sin(uv.y * 1024.0) * 0.5 * 0.375);
    return mix(fc, vec3(0.0), sx + sy);
}

vec3 applyHDR(vec2 uv, vec3 centre) {
    vec2 texel = 1.0 / max(vec2(pc.srcW, pc.srcH), vec2(1.0));
    vec3 c     = centre;
    const float r1 = 0.793, r2 = 0.870;
    vec3 b1 = vec3(0.0), b2 = vec3(0.0);
    vec2 offs[8] = vec2[](
        vec2( 1.5, -1.5), vec2(-1.5, -1.5), vec2( 1.5,  1.5), vec2(-1.5,  1.5),
        vec2( 0.0, -2.5), vec2( 0.0,  2.5), vec2(-2.5,  0.0), vec2( 2.5,  0.0)
    );
    for (int i = 0; i < 8; i++) {
        b1 += texture(texSampler, uv + offs[i] * r1 * texel).rgb;
        b2 += texture(texSampler, uv + offs[i] * r2 * texel).rgb;
    }
    b1 *= 0.005; b2 *= 0.010;
    vec3 hdr = (c + (b2 - b1)) * (r2 - r1);
    return clamp(pow(abs(hdr + c), vec3(1.30)) + hdr, 0.0, 1.0);
}

vec3 applyNatural(vec3 centre) {
    mat3 toYIQ = mat3( 0.299,  0.596,  0.212,
                       0.587, -0.275, -0.523,
                       0.114, -0.321,  0.311);
    mat3 toRGB = mat3( 1.0,         1.0,         1.0,
                       0.95568806, -0.27158179, -1.10817732,
                       0.61985809, -0.64687381,  1.70506455);
    vec3 t = centre * toYIQ;
    t = vec3(pow(t.r, 1.12), t.g * 1.2, t.b * 1.2);
    return clamp(t * toRGB, 0.0, 1.0);
}

void main() {
    ivec2 lim = textureSize(texSampler, 0) - ivec2(1);
    ivec2 ip  = ivec2(fragTexCoord * vec2(max(pc.srcW, 1.0), max(pc.srcH, 1.0)));
    vec3  col = rcasTap(ip, lim);

    /*
     * Zero is a real setting, not a rounding of one.
     *
     * The renderer skips this pass entirely when nothing here has work to do,
     * so a zero dial is only seen in the frame where the value changes, or
     * when an effect is running and sharpening is not. It exists so those
     * paths can never sharpen by a hair.
     */
    float amount    = clamp(pc.sharpness, 0.0, 1.0);
    bool  sharpened = amount > 0.0;
    if (sharpened) col = applyRcas(ip, lim, col, amount);

    vec2  uv     = fragTexCoord;
    float effect = clamp(pc.effectAmount, 0.0, 1.0);
    if      (pc.effectId == 1) col = applyDLS    (uv, col, effect, sharpened);
    else if (pc.effectId == 2) col = applyCRT    (uv, col);
    else if (pc.effectId == 3) col = applyHDR    (uv, col);
    else if (pc.effectId == 4) col = applyNatural(col);

    outColor = vec4(col, 1.0);
}
