#version 450

/*
 * High precision, unlike the SGSR shader beside it, and not by preference.
 *
 * mediump is fp16 on this GPU, whose smallest normal value is about 6.1e-5.
 * The direction test below compares against 1/32768 — 3.05e-5 — which is
 * *below* that and flushes to zero in fp16. A flat region has a gradient of
 * exactly zero, so the guard `dir2 < 1/32768` became `dir2 < 0`, which is
 * never true, and the inversesqrt it was guarding returned infinity. The
 * direction then became NaN across every flat area of the frame — sky, walls,
 * most of a typical image.
 *
 * The alternative was to raise the threshold into fp16's range, but the whole
 * kernel is built from small differences of luminance and fp16 is short of
 * mantissa for that. Precision is what this shader is for.
 */
precision highp float;
precision highp int;

/*
 * FSR 1.0 upscaling — AMD FidelityFX Super Resolution, EASU stage.
 *
 * A second upscaler alongside SGSR, not a replacement. The two fail
 * differently and which one looks better is a per-game answer: SGSR is
 * cheaper and holds text and UI edges very cleanly; EASU reconstructs
 * diagonals and fine geometry better and costs more doing it. Having both
 * means the choice is the user's rather than the fork's.
 *
 * Spatial only, which is the reason this is usable here at all. FSR 2 and
 * every temporal upscaler need motion vectors and a depth buffer from the
 * engine, and this compositor has neither — it sees a finished image and
 * nothing about how it was made. FSR 1 asks for exactly what we have.
 *
 * <h2>What EASU actually does</h2>
 *
 * A bilinear upscale blends four texels by distance and knows nothing about
 * what it is blending, so an edge crossing the sample at an angle comes out
 * as a staircase softened into mush. EASU instead looks at twelve texels,
 * works out which way the local feature runs and how strongly, and then
 * shapes its filter kernel into an ellipse aligned to that direction —
 * long along the edge, narrow across it. The result keeps the edge sharp
 * while still resolving between pixels.
 *
 * <h2>Sharpening is the next shader, not this one</h2>
 *
 * Upstream FSR is two passes: EASU upscales, RCAS sharpens. RCAS needs the
 * neighbours of the *upscaled* image, which do not exist while this shader is
 * still producing them — so it runs afterwards, over this shader's output, in
 * `window_rcas.frag`. That is why the renderer gives FSR its own offscreen
 * target when the sharpness dial is above zero: not for EASU's benefit, but so
 * there is something for RCAS to read.
 *
 * Nothing here sharpens. An earlier version did — a contrast-adaptive pass
 * against the *source* neighbourhood, which was the best available while this
 * was one pass — and leaving it in would now sharpen every frame twice.
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
    float resW;
    float sharpness;
} pc;

layout(binding = 0) uniform mediump sampler2D texSampler;
layout(location = 0) in  highp vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

/*
 * Brightness, used only to decide direction.
 *
 * The kernel is built from luminance and then applied to colour, because an
 * edge is a change in brightness — two different hues at the same brightness
 * are not an edge to sharpen, and treating them as one produces coloured
 * fringes along them.
 */
float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

/** One source texel by index, clamped to the edge. */
vec3 easuTexel(ivec2 p, ivec2 lim) {
    return texelFetch(texSampler, clamp(p, ivec2(0), lim), 0).rgb;
}

/*
 * One quadrant's contribution to the local edge direction and strength.
 *
 * Called four times, once per corner of the texel the output pixel lands in,
 * each weighted by how close the pixel is to that corner. That bilinear
 * weighting is what stops the kernel snapping between orientations as a pixel
 * crosses a texel boundary, which would shimmer as the camera moved.
 *
 * `len` accumulates how confident the direction is: a hard edge gives a large
 * gradient relative to the local variation and pushes the kernel towards a
 * long thin ellipse, while flat or noisy areas leave it near-circular, which
 * is bilinear and is the right answer there.
 */
void easuSet(inout vec2 dir, inout float len,
             vec2 pp, bool biS, bool biT, bool biU, bool biV,
             float lA, float lB, float lC, float lD, float lE)
{
    float w = 0.0;
    if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
    if (biT) w = pp.x * (1.0 - pp.y);
    if (biU) w = (1.0 - pp.x) * pp.y;
    if (biV) w = pp.x * pp.y;

    // Horizontal: gradient across C, and the variation either side of it.
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = 1.0 / max(lenX, 1.0 / 32768.0);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;

    // Vertical, the same shape rotated.
    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / max(lenY, 1.0 / 32768.0);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

/*
 * The windowed kernel weight for one tap.
 *
 * A Lanczos-like lobe, evaluated on the tap's distance *after* that distance
 * has been measured in the rotated, stretched space — which is what turns a
 * circular filter into the edge-aligned ellipse.
 */
float easuTap(inout vec3 acc, inout float accW,
              vec2 off, vec2 dir, vec2 len2, float lob, float clp, vec3 c)
{
    // Rotate the offset into edge space: x along the edge, y across it.
    vec2 v = vec2(dot(off, dir), dot(off, vec2(-dir.y, dir.x)));
    v *= len2;
    float d2 = min(dot(v, v), clp);

    float wB = (2.0 / 5.0) * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;

    acc  += c * w;
    accW += w;
    return w;
}

void main() {
    vec2 srcRes = vec2(max(pc.srcW, 1.0), max(pc.srcH, 1.0));

    /*
     * Where this output pixel lands in source pixels.
     *
     * The half-texel shift moves from texel centres to texel corners, so `fp`
     * is the top-left of the inner 2x2 and `f` is the position inside it.
     * Getting this offset wrong shifts the whole image by half a source pixel,
     * which reads as a soft picture rather than as a misalignment and is
     * correspondingly easy to miss.
     */
    vec2 pp = fragTexCoord * srcRes - 0.5;
    vec2 fp = floor(pp);
    vec2 f  = pp - fp;

    /*
     * Taps are fetched by index, not by coordinate.
     *
     * This built a normalised `base` and sampled twelve offsets from it with
     * the linear sampler, on the reasoning that `(fp + 0.5) * texel` lands on a
     * texel centre. It does not, quite: `texel` is `1.0/srcW`, and for a source
     * width of 960 that is not representable, so every tap sits a fraction off
     * centre and the sampler blends it with its neighbour.
     *
     * On a photograph that is invisible. On text it is not — strokes are one
     * source pixel wide with the largest contrast in the frame, so a fractional
     * misalignment mixes glyph and background before the kernel has decided
     * anything, and the direction it then finds is of an edge that is not
     * there. The reference implementation reads exact texels with
     * textureGather for the same reason.
     */
    ivec2 lim  = textureSize(texSampler, 0) - ivec2(1);
    ivec2 base = ivec2(fp);

    /*
     * Twelve taps: a 4x4 neighbourhood with the corners dropped.
     *
     *        b  c
     *     e  f  g  h
     *     i  j  k  l
     *        n  o
     *
     * The corners are the four furthest samples and contribute least, so
     * dropping them costs a quarter of the bandwidth and almost none of the
     * quality — which is the trade that makes FSR 1 affordable.
     */
    vec3 cB = easuTexel(base + ivec2( 0, -1), lim);
    vec3 cC = easuTexel(base + ivec2( 1, -1), lim);
    vec3 cE = easuTexel(base + ivec2(-1,  0), lim);
    vec3 cF = easuTexel(base                , lim);
    vec3 cG = easuTexel(base + ivec2( 1,  0), lim);
    vec3 cH = easuTexel(base + ivec2( 2,  0), lim);
    vec3 cI = easuTexel(base + ivec2(-1,  1), lim);
    vec3 cJ = easuTexel(base + ivec2( 0,  1), lim);
    vec3 cK = easuTexel(base + ivec2( 1,  1), lim);
    vec3 cL = easuTexel(base + ivec2( 2,  1), lim);
    vec3 cN = easuTexel(base + ivec2( 0,  2), lim);
    vec3 cO = easuTexel(base + ivec2( 1,  2), lim);

    float lB = luma(cB), lC = luma(cC);
    float lE = luma(cE), lF = luma(cF), lG = luma(cG), lH = luma(cH);
    float lI = luma(cI), lJ = luma(cJ), lK = luma(cK), lL = luma(cL);
    float lN = luma(cN), lO = luma(cO);

    vec2  dir = vec2(0.0);
    float len = 0.0;
    easuSet(dir, len, f, true,  false, false, false, lB, lE, lF, lG, lJ);
    easuSet(dir, len, f, false, true,  false, false, lC, lF, lG, lH, lK);
    easuSet(dir, len, f, false, false, true,  false, lF, lI, lJ, lK, lN);
    easuSet(dir, len, f, false, false, false, true,  lG, lJ, lK, lL, lO);

    /*
     * Turn the accumulated gradient into a kernel shape.
     *
     * A direction of zero means no edge was found — flat colour, or noise that
     * cancelled — and the fallback has to be a valid unit vector or the
     * rotation below produces NaNs that spread across the frame as black or
     * white pixels.
     */
    float dir2 = dot(dir, dir);
    // Written as a failed positive test rather than `dir2 < eps`, so that a
    // NaN arriving from anywhere upstream lands in the safe branch instead of
    // propagating: every comparison against NaN is false.
    bool  zero = !(dir2 > (1.0 / 32768.0));
    float dirR = zero ? 1.0 : inversesqrt(max(dir2, 1.0 / 32768.0));
    dir = zero ? vec2(1.0, 0.0) : dir * dirR;

    len = len * 0.5;
    len *= len;

    // Longer along the edge the more confident the direction is.
    float stretch = dir2 == 0.0 ? 1.0
                  : (dot(dir, dir) / max(abs(dir.x), abs(dir.y)));
    vec2  len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob  = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp  = 1.0 / lob;

    vec3  acc  = vec3(0.0);
    float accW = 0.0;
    easuTap(acc, accW, vec2( 0.0, -1.0) - f, dir, len2, lob, clp, cB);
    easuTap(acc, accW, vec2( 1.0, -1.0) - f, dir, len2, lob, clp, cC);
    easuTap(acc, accW, vec2(-1.0,  0.0) - f, dir, len2, lob, clp, cE);
    easuTap(acc, accW, vec2( 0.0,  0.0) - f, dir, len2, lob, clp, cF);
    easuTap(acc, accW, vec2( 1.0,  0.0) - f, dir, len2, lob, clp, cG);
    easuTap(acc, accW, vec2( 2.0,  0.0) - f, dir, len2, lob, clp, cH);
    easuTap(acc, accW, vec2(-1.0,  1.0) - f, dir, len2, lob, clp, cI);
    easuTap(acc, accW, vec2( 0.0,  1.0) - f, dir, len2, lob, clp, cJ);
    easuTap(acc, accW, vec2( 1.0,  1.0) - f, dir, len2, lob, clp, cK);
    easuTap(acc, accW, vec2( 2.0,  1.0) - f, dir, len2, lob, clp, cL);
    easuTap(acc, accW, vec2( 0.0,  2.0) - f, dir, len2, lob, clp, cN);
    easuTap(acc, accW, vec2( 1.0,  2.0) - f, dir, len2, lob, clp, cO);

    /*
     * Clamped to the four texels actually being interpolated between.
     *
     * The lobed kernel has negative weights, which is where its sharpness
     * comes from and also what lets it overshoot into values none of the
     * source texels held — bright haloes along dark edges. Clamping to the
     * inner 2x2 keeps the sharpening and discards the ringing.
     */
    vec3 mn = min(min(cF, cG), min(cJ, cK));
    vec3 mx = max(max(cF, cG), max(cJ, cK));
    vec3 col = clamp(acc / max(accW, 1.0 / 32768.0), mn, mx);

    outColor = vec4(col, pc.useTexAlpha != 0 ? texture(texSampler, fragTexCoord).a : 1.0);
}
