// lipsync_auto.h - RE3 3DD Mod: lip-sync track (R3LIP1 frame layout) computed from voice PCM at clip load time.
//
// Header only, C++17, standard library + <cmath> only, no Windows calls, no global state (all tables live in the
// call), safe to call from any thread. Namespace re3lipauto.
//
// Faithful port of the offline analysis that baked the installed tracks:
//   models/characters/face-rig/tools/lipsync_analyze.py   decode() + analyse() + mouth_track()
//   voice-ru/tools/lipsync_ru.py                          (verbatim copy of the analysis for PCM WAVs)
// FPS 100, SR 22050, 40 ms lead, formants divided by 1.16 for the female speakers (0 Jill, 7 other female).
// The Python decoded every clip with ffmpeg (-f f32le -ac 1 -ar 22050); that step is emulated here too:
//   s16 -> float x / 32768; stereo -> mono 0.70710677f * L + 0.70710677f * R in float (swresample rematrix,
//   done after resampling as swresample orders it); other rates -> libswresample's default resampler
//   (Kaiser-windowed sinc, filter_size 32, phase_shift 10, cutoff 0.97, beta 9, exact rational phase count,
//   8-lane FMA accumulation as its x86 FMA3 kernel, mirrored start, reflected end at the flush), so the sample
//   count at 22050 Hz - and with it frameCount and duration - follows the same rules as the Python.
//
// Output (same as lipsync_analyze.py writes into lipsync.r3lip):
//   frames      4 bytes per 10 ms frame: jaw, wide, round, loudness, each u8 0..255 (np.round half-even, clipped)
//   frameCount  n = 1 + max(0, (len - 882) // 220), len = sample count at 22050 Hz
//   duration    float(round(len / 22050, 4))   -> LipClip.duration; LipClip {name, speaker, frameCount, firstFrame,
//               duration} is filled by the caller (speaker = character id, female = speaker 0 or 7).
// Differences from the Python, all outside what it can produce: clips shorter than 4 frames (< 1542 samples at
// 22050 Hz, ~70 ms) crash the Python (lead shift); here they give all-zero mouth channels. An empty clip is an error.
// A fully silent clip opens the mouth (jaw ~0.78) exactly as the Python would: loudness is relative to the clip's
// own 95th percentile. Callers that care can test for silence before calling.
//
// API:
//   bool re3lipauto::Analyze(const int16_t* pcm, size_t sampleFrames, unsigned channels, unsigned rate, bool female,
//                            Track& out, std::string* error = nullptr);
//   bool re3lipauto::ToAnalysisRate(pcm, sampleFrames, channels, rate, std::vector<float>& mono22050, error);
//   bool re3lipauto::AnalyzeMono(const float* mono22050, size_t len, bool female, Track& out, error);
// The last two split Analyze in its decode and analysis halves (e.g. to pad the 22050 Hz signal first, as
// lipsync_ru.py pads the Russian PCM to the original's decoded length).
//
// Verified offline (qa/voice-packs/lipsync-auto, REPORT.ru.txt): all 411 installed English tracks and all 411
// Russian tracks byte-identical (frame counts, bytes; English durations too - lipsync_ru.py stored the unrounded
// len / 22050, up to 5e-5 s apart); ToAnalysisRate bit-identical to ffmpeg 8.0.1 for 8000..48000 Hz mono/stereo;
// the same results with the x87 precision control at 24 bits (Direct3D 9's default, see Mem() below).
// Cost (x86 /O2, this PC): ~1.3 ms per audio second at 22050 Hz mono (5 s clip ~7 ms, 60 s ~70 ms); other rates
// add the resampler, ~1 ms (mono) / ~2 ms (stereo) per audio second (5 s at 48 kHz stereo ~17 ms). Memory: ~4 bytes
// per 22050 Hz output sample plus ~100 bytes per 10 ms frame; the PCM is read in place. Better on a loader / worker
// thread (or cached) than inside a frame.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace re3lipauto {

struct Track {
    std::vector<std::uint8_t> frames;   // 4 * frameCount bytes, R3LIP1 layout (jaw, wide, round, loudness)
    std::uint32_t frameCount = 0;
    float duration = 0;
};

namespace detail {

constexpr int kRate = 22050;            // SR
constexpr int kFps = 100;               // FPS
constexpr int kHop = kRate / kFps;      // 220
constexpr int kWin = 882;               // int(0.04 * SR)
constexpr int kHalf = kWin / 2;         // 441: YIN window W and the complex FFT size
constexpr int kBins = kHalf + 1;        // 442 rfft bins, 25 Hz apart
constexpr int kRate2 = kRate / 2;       // x[::2]
constexpr int kWin2 = 330;              // int(0.03 * 11025)
constexpr int kHop2 = kRate2 / kFps;    // 110
constexpr int kOrder = 12;              // LPC order
constexpr int kYinMin = 52;             // int(SR / 420)
constexpr int kYinMax = 315;            // min(int(SR / 70), W - 1); lags kYinMin .. kYinMax-1 are searched
constexpr int kLead = 4;                // int(round(0.04 * FPS))
constexpr int kSyllK = 18;              // int(0.18 * FPS)
constexpr double kPi = 3.141592653589793;
constexpr float kS16 = 1.0f / 32768.0f;
constexpr float kSqrtHalf = 0.70710678118654752440f;   // float(M_SQRT1_2), swresample's stereo -> mono weight

inline bool Fail(std::string* error, const char* text) {
    if (error) *error = text;
    return false;
}

// x86 FPU state. A 32-bit build returns a double from a call in the x87 st(0) register, and MSVC may then do the
// next + - * / with that value on the x87 unit, whose precision control Direct3D 9 sets to 24 bits (no
// D3DCREATE_FPU_PRESERVE in the proxy) - that changed a few results. So: small helpers are force-inlined, recursion
// returns through memory, and every CRT result that feeds arithmetic goes through Mem() (a volatile store / load),
// which keeps all arithmetic on SSE2 and the output independent of the x87 control word (tested with _PC_24).
#if defined(_MSC_VER)
#define RE3LIPAUTO_INLINE __forceinline
#else
#define RE3LIPAUTO_INLINE inline
#endif

RE3LIPAUTO_INLINE double Mem(double v) {
    volatile double t = v;
    return t;
}

RE3LIPAUTO_INLINE double Clip01(double v) { return (std::min)((std::max)(v, 0.0), 1.0); }

// numpy's pairwise summation (add.reduce over a contiguous axis), bit for bit.
inline void PairwiseSum(const double* a, std::size_t n, double* out) {
    if (n < 8) {
        double r = -0.0;
        for (std::size_t i = 0; i < n; ++i) r += a[i];
        *out = r;
        return;
    }
    if (n <= 128) {
        double r0 = a[0], r1 = a[1], r2 = a[2], r3 = a[3], r4 = a[4], r5 = a[5], r6 = a[6], r7 = a[7];
        std::size_t i = 8;
        const std::size_t end = n - (n % 8);
        for (; i < end; i += 8) {
            r0 += a[i]; r1 += a[i + 1]; r2 += a[i + 2]; r3 += a[i + 3];
            r4 += a[i + 4]; r5 += a[i + 5]; r6 += a[i + 6]; r7 += a[i + 7];
        }
        double res = ((r0 + r1) + (r2 + r3)) + ((r4 + r5) + (r6 + r7));
        for (; i < n; ++i) res += a[i];
        *out = res;
        return;
    }
    std::size_t n2 = n / 2;
    n2 -= n2 % 8;
    double lo, hi;
    PairwiseSum(a, n2, &lo);
    PairwiseSum(a + n2, n - n2, &hi);
    *out = lo + hi;
}

// np.percentile(v, 95), method 'linear' (numpy >= 1.22 _lerp)
inline void Percentile95(std::vector<double> v, double* out) {
    const std::size_t n = v.size();
    const double vi = double(n - 1) * (95.0 / 100.0);
    if (vi >= double(n - 1)) {
        *out = *std::max_element(v.begin(), v.end());
        return;
    }
    const std::size_t lo = std::size_t(Mem(std::floor(vi)));
    const double gamma = vi - double(lo);
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(lo), v.end());
    const double a = v[lo];
    const double b = *std::min_element(v.begin() + std::ptrdiff_t(lo) + 1, v.end());
    const double diff = b - a;
    double r = a + diff * gamma;
    if (gamma >= 0.5) r = b - diff * (1.0 - gamma);
    *out = r;
}

// ------------------------------------------------------------------ spectrum: |rfft(882)|^2 via a 441-point FFT
struct Cx {
    double re, im;
};

// 441 = 3 * 3 * 7 * 7, decimation in time; stages written out (radix 7 leaves on stride-63 input, then radix-7,
// radix-3, radix-3 combines with twiddles W_n^(q k)). Tables on the heap (keeps the caller's stack small).
class Fft441 {
public:
    Fft441() : tw0_(147 * 2), tw1_(49 * 2), tw2_(7 * 6), twr_(kBins), z_(kHalf), Z_(kHalf) {
        std::vector<Cx> tw(kHalf);   // W_441^t
        for (int t = 0; t < kHalf; ++t) {
            const double a = -2.0 * kPi * double(t) / double(kHalf);
            tw[std::size_t(t)] = {std::cos(a), std::sin(a)};
        }
        for (int k = 0; k < 147; ++k)
            for (int q = 1; q < 3; ++q) tw0_[std::size_t(k * 2 + q - 1)] = tw[std::size_t(q * k)];        // n = 441
        for (int k = 0; k < 49; ++k)
            for (int q = 1; q < 3; ++q) tw1_[std::size_t(k * 2 + q - 1)] = tw[std::size_t(q * k * 3)];    // n = 147
        for (int k = 0; k < 7; ++k)
            for (int q = 1; q < 7; ++q) tw2_[std::size_t(k * 6 + q - 1)] = tw[std::size_t(q * k * 9)];    // n = 49
        for (int k = 0; k <= kHalf; ++k) {
            const double a = -2.0 * kPi * double(k) / double(kWin);
            twr_[std::size_t(k)] = {std::cos(a), std::sin(a)};
        }
        for (int t = 0; t < 7; ++t) {
            c7_[t] = std::cos(2.0 * kPi * double(t) / 7.0);
            s7_[t] = std::sin(2.0 * kPi * double(t) / 7.0);
        }
        s3_ = std::sin(2.0 * kPi / 3.0);
    }
    // power[k] = |rfft(x)[k]|^2, x real of length 882, k = 0..441
    void Power(const double* x, double* power) {
        Cx* z = z_.data();
        Cx* Z = Z_.data();
        for (int j = 0; j < kHalf; ++j) z[j] = {x[2 * j], x[2 * j + 1]};
        Transform(z, Z);
        for (int k = 0; k <= kHalf; ++k) {
            const Cx a = Z[k == kHalf ? 0 : k], b = Z[k == 0 ? 0 : kHalf - k];
            const double er = 0.5 * (a.re + b.re), ei = 0.5 * (a.im - b.im);   // E = (Z_k + conj Z_{M-k}) / 2
            const double orr = 0.5 * (a.im + b.im), oi = -0.5 * (a.re - b.re);  // O = (Z_k - conj Z_{M-k}) / 2i
            const Cx w = twr_[std::size_t(k)];
            const double xr = er + (w.re * orr - w.im * oi), xi = ei + (w.re * oi + w.im * orr);
            power[k] = xr * xr + xi * xi;
        }
    }

private:
    static Cx Mul(Cx a, Cx b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
    void R3(Cx& a0, Cx& a1, Cx& a2) const {
        const Cx t1 = {a1.re + a2.re, a1.im + a2.im};
        const Cx t2 = {a0.re - 0.5 * t1.re, a0.im - 0.5 * t1.im};
        const Cx t3 = {s3_ * (a1.re - a2.re), s3_ * (a1.im - a2.im)};
        a0 = {a0.re + t1.re, a0.im + t1.im};
        a1 = {t2.re + t3.im, t2.im - t3.re};
        a2 = {t2.re - t3.im, t2.im + t3.re};
    }
    void R7(const Cx* a, Cx* b) const {   // a, b may alias
        const Cx a0 = a[0];
        const Cx s1 = {a[1].re + a[6].re, a[1].im + a[6].im}, s2 = {a[2].re + a[5].re, a[2].im + a[5].im},
                 s3 = {a[3].re + a[4].re, a[3].im + a[4].im};
        const Cx d1 = {a[1].re - a[6].re, a[1].im - a[6].im}, d2 = {a[2].re - a[5].re, a[2].im - a[5].im},
                 d3 = {a[3].re - a[4].re, a[3].im - a[4].im};
        b[0] = {a0.re + s1.re + s2.re + s3.re, a0.im + s1.im + s2.im + s3.im};
        static constexpr int kQr[4][4] = {{0, 0, 0, 0}, {0, 1, 2, 3}, {0, 2, 4, 6}, {0, 3, 6, 2}};   // (q * r) % 7
        for (int r = 1; r <= 3; ++r) {
            const double c1 = c7_[kQr[r][1]], c2 = c7_[kQr[r][2]], c3 = c7_[kQr[r][3]];
            const double n1 = s7_[kQr[r][1]], n2 = s7_[kQr[r][2]], n3 = s7_[kQr[r][3]];
            Cx re = a0, im = {0, 0};
            re.re += s1.re * c1; re.im += s1.im * c1; im.re += d1.re * n1; im.im += d1.im * n1;
            re.re += s2.re * c2; re.im += s2.im * c2; im.re += d2.re * n2; im.im += d2.im * n2;
            re.re += s3.re * c3; re.im += s3.im * c3; im.re += d3.re * n3; im.im += d3.im * n3;
            b[r] = {re.re + im.im, re.im - im.re};
            b[7 - r] = {re.re - im.im, re.im + im.re};
        }
    }
    void Transform(const Cx* in, Cx* out) const {
        // leaves: out[q0*147 + q1*49 + q2*7 + r] = DFT7 of in[q0 + 3 q1 + 9 q2 + 63 q3]
        for (int q0 = 0; q0 < 3; ++q0)
            for (int q1 = 0; q1 < 3; ++q1)
                for (int q2 = 0; q2 < 7; ++q2) {
                    const Cx* src = in + q0 + 3 * q1 + 9 * q2;
                    Cx a[7];
                    for (int q3 = 0; q3 < 7; ++q3) a[q3] = src[63 * q3];
                    R7(a, out + q0 * 147 + q1 * 49 + q2 * 7);
                }
        // n = 49 blocks (p = 7, m = 7)
        for (int blk = 0; blk < 9; ++blk) {
            Cx* o = out + blk * 49;
            for (int k = 0; k < 7; ++k) {
                Cx a[7];
                a[0] = o[k];
                for (int q = 1; q < 7; ++q) a[q] = Mul(o[q * 7 + k], tw2_[std::size_t(k * 6 + q - 1)]);
                Cx b[7];
                R7(a, b);
                for (int r = 0; r < 7; ++r) o[k + r * 7] = b[r];
            }
        }
        // n = 147 blocks (p = 3, m = 49)
        for (int blk = 0; blk < 3; ++blk) {
            Cx* o = out + blk * 147;
            for (int k = 0; k < 49; ++k) {
                Cx a0 = o[k], a1 = Mul(o[49 + k], tw1_[std::size_t(k * 2)]), a2 = Mul(o[98 + k], tw1_[std::size_t(k * 2 + 1)]);
                R3(a0, a1, a2);
                o[k] = a0;
                o[49 + k] = a1;
                o[98 + k] = a2;
            }
        }
        // n = 441 (p = 3, m = 147)
        for (int k = 0; k < 147; ++k) {
            Cx a0 = out[k], a1 = Mul(out[147 + k], tw0_[std::size_t(k * 2)]), a2 = Mul(out[294 + k], tw0_[std::size_t(k * 2 + 1)]);
            R3(a0, a1, a2);
            out[k] = a0;
            out[147 + k] = a1;
            out[294 + k] = a2;
        }
    }
    std::vector<Cx> tw0_, tw1_, tw2_, twr_;
    double c7_[7], s7_[7], s3_;
    std::vector<Cx> z_, Z_;
};

// ------------------------------------------------------------------ formants: LPC(12) roots of a 330-sample frame
// The Python takes np.roots (companion-matrix eigenvalues); here Laguerre's method with deflation and polishing on
// the full polynomial gives the same roots to rounding. A small complex type: std::complex division / abs / sqrt
// are several times slower with MSVC.
struct Cz {
    double re, im;
};
inline Cz operator+(Cz a, Cz b) { return {a.re + b.re, a.im + b.im}; }
inline Cz operator-(Cz a, Cz b) { return {a.re - b.re, a.im - b.im}; }
inline Cz operator*(Cz a, Cz b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
inline Cz operator*(double s, Cz a) { return {s * a.re, s * a.im}; }
RE3LIPAUTO_INLINE double Abs(Cz a) { return std::sqrt(a.re * a.re + a.im * a.im); }
inline Cz Inv(Cz a) {
    const double n = a.re * a.re + a.im * a.im;
    return {a.re / n, -a.im / n};
}
inline Cz Sqrt(Cz z) {
    if (z.re == 0 && z.im == 0) return {0, 0};
    const double r = Abs(z);
    if (z.re >= 0) {
        const double t = std::sqrt(0.5 * (r + z.re));
        return {t, z.im / (2 * t)};
    }
    const double t = std::sqrt(0.5 * (r - z.re));
    return {std::fabs(z.im) / (2 * t), z.im >= 0 ? t : -t};
}

// Laguerre's method on the real polynomial c[0..m] (ascending powers); x = start / result.
inline bool Laguerre(const double* c, int m, Cz& x) {
    const double kFrac[9] = {0.0, 0.5, 0.25, 0.75, 0.13, 0.38, 0.62, 0.88, 1.0};
    const int kMt = 10, kMaxIt = 80;
    for (int it = 1; it <= kMaxIt; ++it) {
        Cz b = {c[m], 0}, d = {0, 0}, f = {0, 0};
        double err = std::fabs(c[m]);
        const double abx = Abs(x);
        for (int j = m - 1; j >= 0; --j) {
            f = x * f + d;
            d = x * d + b;
            b = x * b;
            b.re += c[j];
            err = Abs(b) + abx * err;
        }
        err *= std::numeric_limits<double>::epsilon();
        if (Abs(b) <= err) return true;
        const Cz ib = Inv(b);
        const Cz g = d * ib, g2 = g * g, h = g2 - 2.0 * (f * ib);
        const Cz sq = Sqrt(double(m - 1) * (double(m) * h - g2));
        Cz gp = g + sq;
        const Cz gm = g - sq;
        const double abp = Abs(gp), abm = Abs(gm);
        if (abp < abm) gp = gm;
        Cz dx;
        if ((std::max)(abp, abm) > 0.0) dx = double(m) * Inv(gp);
        else dx = {(1.0 + abx) * Mem(std::cos(double(it))), (1.0 + abx) * Mem(std::sin(double(it)))};
        const Cz x1 = x - dx;
        if (x1.re == x.re && x1.im == x.im) return true;
        if (it % kMt) x = x1;
        else x = x - kFrac[it / kMt] * dx;
    }
    return false;
}

// Roots of the real polynomial a[0..deg] (a[0] the highest power) in the upper half plane (imag > 0), each polished
// on the undeflated polynomial. Zero roots (trailing zero coefficients) and real roots are dropped: the formant
// filter keeps only imag > 0.01. Complex roots are deflated with their conjugate (real quadratic factor).
inline int UpperRoots(const double* a, int deg, Cz* up) {
    int m = deg;
    while (m > 0 && a[m] == 0.0) --m;
    double c[kOrder + 1], ad[kOrder + 1], q[kOrder + 1];
    for (int j = 0; j <= m; ++j) c[j] = ad[j] = a[m - j];   // ascending
    int nup = 0, j = m;
    while (j >= 1) {
        Cz x = {0, 0};
        Laguerre(ad, j, x);
        if (j >= 2 && std::fabs(x.im) > 1e-12 * (1.0 + Abs(x))) {
            // divide by z^2 + p1 z + p0 (descending synthetic division)
            const double p1 = -2 * x.re, p0 = x.re * x.re + x.im * x.im;
            q[0] = ad[j];
            if (j - 2 >= 1) q[1] = ad[j - 1] - p1 * q[0];
            for (int k = 2; k <= j - 2; ++k) q[k] = ad[j - k] - p1 * q[k - 1] - p0 * q[k - 2];
            for (int i = 0; i <= j - 2; ++i) ad[i] = q[j - 2 - i];
            j -= 2;
            up[nup++] = x.im > 0 ? x : Cz{x.re, -x.im};
        } else {
            const double r = x.re;
            double b = ad[j];
            for (int jj = j - 1; jj >= 0; --jj) {
                const double t = ad[jj];
                ad[jj] = b;
                b = r * b + t;
            }
            j -= 1;
        }
    }
    for (int k = 0; k < nup; ++k) {   // polish; keep a root that would jump to a neighbour
        Cz x = up[k];
        Laguerre(c, m, x);
        if (Abs(x - up[k]) <= 1e-6 * (1.0 + Abs(up[k]))) up[k] = x;
    }
    return nup;
}

// formants(frame, 11025): F1..F3 (Hz) from the LPC roots, NaN when not found
inline void Formants(const double* samples, const double* ham, double* out3) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    out3[0] = out3[1] = out3[2] = nan;
    double y[kWin2], p[kWin2];
    for (int j = 0; j < kWin2; ++j) y[j] = samples[j] * ham[j];
    p[0] = y[0];
    for (int j = 1; j < kWin2; ++j) p[j] = y[j] - 0.63 * y[j - 1];
    double r[kOrder + 1];
    for (int m = 0; m <= kOrder; ++m) {
        double s = 0.0;
        for (int j = 0; j + m < kWin2; ++j) s += p[j + m] * p[j];
        r[m] = s;
    }
    if (r[0] <= 1e-12) return;
    double a[kOrder + 1] = {1.0};
    double e = r[0];
    for (int i = 1; i <= kOrder; ++i) {
        double dot = 0.0;
        for (int j = 1; j < i; ++j) dot += a[j] * r[i - j];
        const double acc = r[i] + dot;
        const double k = -acc / e;
        double na[kOrder + 1];
        for (int j = 0; j <= kOrder; ++j) na[j] = a[j];
        for (int j = 1; j < i; ++j) na[j] = a[j] + k * a[i - j];
        na[i] = a[i] + k * 1.0;
        for (int j = 0; j <= kOrder; ++j) a[j] = na[j];
        e *= (1 - k * k);
        if (e <= 0) return;
    }
    Cz roots[kOrder];
    const int nr = UpperRoots(a, kOrder, roots);
    double cand[kOrder];
    int nc = 0;
    for (int i = 0; i < nr; ++i) {
        if (!(roots[i].im > 0.01)) continue;
        const double ang = Mem(std::atan2(roots[i].im, roots[i].re)) * double(kRate2) / (2 * kPi);
        const double bw = -0.5 * double(kRate2) / kPi * Mem(std::log(Mem(std::hypot(roots[i].re, roots[i].im)) + 1e-12));
        if (200 < ang && ang < 5000 && bw < 500) cand[nc++] = ang;
    }
    std::sort(cand, cand + nc);
    for (int i = 0; i < nc && i < 3; ++i) out3[i] = cand[i];
}

// ------------------------------------------------------------------ libswresample emulation (ffmpeg -ar 22050)
inline std::int64_t Gcd(std::int64_t a, std::int64_t b) {
    while (b) {
        const std::int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

RE3LIPAUTO_INLINE double BesselI0(double x) {   // power series, as swresample's bessel() / av_bessel_i0 (to rounding)
    double lastv = 0, t, v;
    x = x * x / 4;
    t = x;
    v = 1 + x;
    for (int k = 2; v != lastv && k < 500; ++k) {
        lastv = v;
        t *= x * (1.0 / (double(k) * double(k)));
        v += t;
    }
    return v;
}

class Resampler {
public:
    // swresample resample_init with the defaults: filter_size 32, phase_shift 10, linear_interp 1, cutoff 0.97,
    // Kaiser beta 9, exact_rational 1, FLTP filter bank.
    void Init(int inRate, int outRate) {
        const double cutoff = 0.97;
        factor_ = (std::min)(outRate * cutoff / inRate, 1.0);
        int phaseCount = 1 << 10;
        len_ = (std::max)(int(Mem(std::ceil(32 / factor_))), 1);
        if (len_ > 1) len_ = (len_ + 1) & ~1;
        const std::int64_t g = Gcd(outRate, inRate);
        const std::int64_t exactNum = outRate / g;
        if (exactNum <= phaseCount) phaseCount = int(exactNum);
        pc_ = phaseCount;
        alloc_ = (len_ + 7) & ~7;
        center_ = (len_ - 1) / 2;
        std::vector<float> bank(std::size_t(alloc_) * std::size_t(pc_ + 1), 0.0f);
        BuildFilter(bank.data());
        for (int j = 0; j < alloc_ - 1; ++j) bank[std::size_t(alloc_) * pc_ + 1 + j] = bank[std::size_t(j)];
        bank[std::size_t(alloc_) * pc_] = bank[std::size_t(alloc_ - 1)];
        // s16 -> float (x * 2^-15, exact) folded into the taps; s16 * (tap * 2^-15) is exact in double
        bank_.resize(bank.size());
        for (std::size_t i = 0; i < bank.size(); ++i) bank_[i] = double(bank[i]) * (1.0 / 32768.0);
        const std::int64_t g2 = Gcd(outRate, std::int64_t(inRate) * pc_);
        srcIncr_ = outRate / g2;
        dstIncr_ = std::int64_t(inRate) * pc_ / g2;
        while (dstIncr_ < (1 << 20) && srcIncr_ < (1 << 20)) {
            dstIncr_ *= 2;
            srcIncr_ *= 2;
        }
        div_ = dstIncr_ / srcIncr_;
        mod_ = dstIncr_ % srcIncr_;
    }
    // Resamples the interleaved s16 channels and mixes stereo to mono (swresample: resample first, then rematrix).
    void Run(const std::int16_t* pcm, std::size_t frames, unsigned channels, std::vector<float>& out) const {
        const std::int64_t N = std::int64_t(frames);
        const int L = len_, c = center_;
        out.clear();
        out.reserve(std::size_t(double(N) * double(srcIncr_) / double(dstIncr_) * pc_) + kWin + 32);   // + analysis padding
        std::vector<double> edge(std::size_t(L) * 2);
        std::int64_t s = 0, index = 0, frac = 0, limit = N - 1, reflect = -1;
        const double invSrcIncr = 1.0 / double(srcIncr_);
        for (;;) {
            if (s - c + L - 1 > limit) {
                if (reflect >= 0) break;
                // flush: swresample appends (min(buffered, L) + 1) / 2 reflected samples, buffer starts at s - c
                const std::int64_t count = (std::max)(std::int64_t(0), N - (s - c));
                reflect = ((std::min)(count, std::int64_t(L)) + 1) / 2;
                limit = N - 1 + reflect;
                continue;
            }
            float v[2] = {0, 0};
            for (unsigned ch = 0; ch < channels; ++ch) Output(pcm, N, channels, ch, s - c, index, frac, invSrcIncr, edge, &v[ch]);
            if (channels == 2) {
                const float l = kSqrtHalf * v[0];
                const float r = kSqrtHalf * v[1];
                out.push_back(l + r);
            } else {
                out.push_back(v[0]);
            }
            frac += mod_;
            index += div_;
            if (frac >= srcIncr_) {
                frac -= srcIncr_;
                ++index;
            }
            while (index >= pc_) {
                ++s;
                index -= pc_;
            }
        }
    }

private:
    void BuildFilter(float* filter) const {
        const int tapCount = len_, phaseCount = pc_;
        const int phNb = phaseCount % 2 ? phaseCount : phaseCount / 2 + 1;
        const int center = (tapCount - 1) / 2;
        const double factor = factor_ > 1.0 ? 1.0 : factor_;
        std::vector<double> tab(std::size_t(tapCount) + 1), sinLut(std::size_t(phNb), 0.0);
        double norm = 0;
        if (factor == 1.0)
            for (int ph = 0; ph < phNb; ++ph) sinLut[std::size_t(ph)] = Mem(std::sin(kPi * ph / phaseCount)) * (center & 1 ? 1 : -1);
        for (int ph = 0; ph < phNb; ++ph) {
            double s = sinLut[std::size_t(ph)];
            for (int i = 0; i < tapCount; ++i) {
                double x = kPi * (double(i - center) - double(ph) / phaseCount) * factor;
                double y;
                if (x == 0) y = 1.0;
                else if (factor == 1.0) y = s / x;
                else y = Mem(std::sin(x)) / x;
                const double w = 2.0 * x / (factor * tapCount * kPi);
                y *= Mem(BesselI0(9.0 * std::sqrt((std::max)(1 - w * w, 0.0))));
                tab[std::size_t(i)] = y;
                s = -s;
                if (!ph) norm += y;
            }
            for (int i = 0; i < tapCount; ++i) filter[std::size_t(ph) * alloc_ + i] = float(tab[std::size_t(i)] * 1 / norm);
            if (phaseCount % 2) continue;
            for (int i = 0; i < tapCount; ++i)
                filter[std::size_t(phaseCount - ph) * alloc_ + tapCount - 1 - i] = filter[std::size_t(ph) * alloc_ + i];
        }
    }
    // one filter evaluation: 8 float lanes, lane = tap % 8, fused multiply-add per tap (ff_resample_*_float_fma3)
    static void Dot(const double* row, const double* src, int L, float* out) {
        float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        int i = 0;
        for (; i + 8 <= L; i += 8)
            for (int j = 0; j < 8; ++j) acc[j] = float(double(acc[j]) + src[i + j] * row[i + j]);
        for (int j = 0; i + j < L; ++j) acc[j] = float(double(acc[j]) + src[i + j] * row[i + j]);
        const float t0 = acc[0] + acc[4], t1 = acc[1] + acc[5], t2 = acc[2] + acc[6], t3 = acc[3] + acc[7];
        const float u0 = t0 + t2, u1 = t1 + t3;
        *out = u0 + u1;
    }
    void Output(const std::int16_t* pcm, std::int64_t N, unsigned channels, unsigned ch, std::int64_t t0, std::int64_t index,
                std::int64_t frac, double invSrcIncr, std::vector<double>& edge, float* out) const {
        const int L = len_;
        double* src = edge.data();
        if (t0 >= 0 && t0 + L <= N) {
            const std::int16_t* p = pcm + std::size_t(t0) * channels + ch;
            for (int i = 0; i < L; ++i) src[i] = double(p[std::size_t(i) * channels]);
        } else {
            for (int i = 0; i < L; ++i) {
                std::int64_t t = t0 + i;
                if (t < 0) t = -t;                   // mirrored start (swresample invert_initial_buffer)
                else if (t >= N) t = 2 * N - 1 - t;  // reflected end (resample_flush)
                src[i] = (t >= 0 && t < N) ? double(pcm[std::size_t(t) * channels + ch]) : 0.0;
            }
        }
        const double* row = &bank_[std::size_t(index) * alloc_];
        float val, v2;
        Dot(row, src, L, &val);
        if (mod_ != 0) {   // inexact ratio (phase count 1024): linear interpolation between neighbouring phases
            Dot(row + alloc_, src, L, &v2);
            val = float(val + double(v2 - val) * invSrcIncr * double(frac));
        }
        *out = val;
    }

    int pc_ = 1, len_ = 1, alloc_ = 8, center_ = 0;
    double factor_ = 1;
    std::int64_t srcIncr_ = 1, dstIncr_ = 1, div_ = 1, mod_ = 0;
    std::vector<double> bank_;
};

// ------------------------------------------------------------------ analysis (analyse + mouth_track)
struct Intermediates {   // optional per-frame values for tests
    std::vector<double> db, loud, syll, hf, F;   // F: 3 per frame, NaN = none
    std::vector<std::uint8_t> voiced;
    double stageMs[4] = {0, 0, 0, 0};   // spectrum + envelope, YIN, formants, mouth track
};

class StageClock {   // stage timings, only when the caller asked for intermediates
public:
    explicit StageClock(Intermediates* dbg) : dbg_(dbg) {
        if (dbg_) last_ = std::chrono::steady_clock::now();
    }
    void Mark(int stage) {
        if (!dbg_) return;
        const auto now = std::chrono::steady_clock::now();
        dbg_->stageMs[stage] += std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
    }

private:
    Intermediates* dbg_;
    std::chrono::steady_clock::time_point last_;
};

// sum over 220 samples of (a[j] - a[j + k])^2
RE3LIPAUTO_INLINE double YinBlock(const double* a, int k) {
    const double* b = a + k;
    double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (int j = 0; j < kHop; j += 4) {
        const double d0 = a[j] - b[j], d1 = a[j + 1] - b[j + 1], d2 = a[j + 2] - b[j + 2], d3 = a[j + 3] - b[j + 3];
        s0 += d0 * d0;
        s1 += d1 * d1;
        s2 += d2 * d2;
        s3 += d3 * d3;
    }
    return (s0 + s1) + (s2 + s3);
}

inline void Smooth(std::vector<double>& v, double up, double down) {
    const double au = 1 - Mem(std::exp(-1 / (kFps * up))), ad = 1 - Mem(std::exp(-1 / (kFps * down)));
    double s = 0.0;
    for (double& x : v) {
        s += (x > s ? au : ad) * (x - s);
        x = s;
    }
}

RE3LIPAUTO_INLINE std::uint8_t Quant(double v) {
    const double q = Mem(std::nearbyint(v * 255));
    return std::uint8_t(q <= 0 ? 0 : q >= 255 ? 255 : int(q));
}

// xs: mono float samples at 22050 Hz; zero-padded in place.
inline bool AnalyzeSignal(std::vector<float>& xs, bool female, Track& out, std::string* error, Intermediates* dbg) {
    out = Track{};
    const std::size_t len = xs.size();
    if (!len) return Fail(error, "empty clip");
    const std::size_t n = 1 + (len >= std::size_t(kWin) ? (len - kWin) / kHop : 0);
    xs.resize(len + kWin + 16, 0.0f);   // frames() pads with zeros
    const float* x = xs.data();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    std::vector<double> hann(kWin), ham(kWin2);   // np.hanning / np.hamming formulas
    for (int j = 0; j < kWin; ++j) hann[std::size_t(j)] = 0.5 + 0.5 * Mem(std::cos(kPi * double(1 - kWin + 2 * j) / double(kWin - 1)));
    for (int j = 0; j < kWin2; ++j) ham[std::size_t(j)] = 0.54 + 0.46 * Mem(std::cos(kPi * double(1 - kWin2 + 2 * j) / double(kWin2 - 1)));
    const double binHz = 1.0 / (double(kWin) * (1.0 / double(kRate)));   // np.fft.rfftfreq(882, 1 / 22050)
    int v0 = -1, v1 = -1, h0 = -1;
    for (int k = 0; k < kBins; ++k) {
        const double f = double(k) * binHz;
        if (f > 250 && f < 2800) {
            if (v0 < 0) v0 = k;
            v1 = k;
        }
        if (f > 3500 && h0 < 0) h0 = k;
    }

    // spectrum, level, vowel band, high band
    StageClock clock(dbg);
    std::vector<double> db(n), vdb(n), hf(n);
    {
        Fft441 fft;
        std::vector<double> w(kWin), power(kBins);
        for (std::size_t i = 0; i < n; ++i) {
            const float* f = x + i * kHop;
            for (int j = 0; j < kWin; ++j) w[std::size_t(j)] = double(f[j]) * hann[std::size_t(j)];
            fft.Power(w.data(), power.data());
            double total, vowel, high;
            PairwiseSum(power.data(), kBins, &total);
            PairwiseSum(power.data() + v0, std::size_t(v1 - v0 + 1), &vowel);
            PairwiseSum(power.data() + h0, std::size_t(kBins - h0), &high);
            total += 1e-12;
            db[i] = 10 * Mem(std::log10(total));
            vdb[i] = 10 * Mem(std::log10(vowel + 1e-12));
            hf[i] = high / total;
        }
    }
    double p95;
    Percentile95(db, &p95);
    std::vector<double> loud(n), syll(n);
    for (std::size_t i = 0; i < n; ++i) loud[i] = Clip01((db[i] - (p95 - 34)) / 30);
    for (std::size_t i = 0; i < n; ++i) {   // syllabic envelope: vowel level vs its +-0.18 s maximum (edge padded)
        double m = -std::numeric_limits<double>::infinity();
        const std::ptrdiff_t lo = std::ptrdiff_t(i) - kSyllK, hi = std::ptrdiff_t(i) + kSyllK;
        for (std::ptrdiff_t j = lo; j <= hi; ++j) {
            const std::ptrdiff_t jj = j < 0 ? 0 : j >= std::ptrdiff_t(n) ? std::ptrdiff_t(n) - 1 : j;
            m = (std::max)(m, vdb[std::size_t(jj)]);
        }
        syll[i] = Clip01((vdb[i] - (m - 16)) / 16);
    }

    clock.Mark(0);
    // YIN voicing (only where loud > 0.2: voiced = isfinite(f0) & loud > 0.2, f0 itself is not used).
    // d[k] over the 441-sample window = block(i) + block(i+1) + last sample; block(i+1) is reused by frame i+1.
    std::vector<std::uint8_t> voiced(n, 0);
    {
        std::vector<double> cache(kYinMax + 1), next(kYinMax + 1), fd(kWin);
        std::size_t cacheBlock = std::size_t(-1);
        int cacheK = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!(loud[i] > 0.2)) continue;
            const std::size_t S = i * kHop;
            for (int j = 0; j < kWin; ++j) fd[std::size_t(j)] = double(x[S + std::size_t(j)]);   // exact
            const double* f = fd.data();
            double cs = 0;
            int k = 1;
            bool found = false;
            for (; k < kYinMax; ++k) {
                const double b0 = (cacheBlock == i && k <= cacheK) ? cache[std::size_t(k)] : YinBlock(f, k);
                const double b1 = YinBlock(f + kHop, k);
                next[std::size_t(k)] = b1;
                const double e = f[2 * kHop] - f[2 * kHop + k];
                const double d = b0 + b1 + e * e;
                cs += d;
                if (k >= kYinMin && d * double(k) / (std::max)(cs, 1e-12) < 0.12) {
                    found = true;
                    break;
                }
            }
            std::swap(cache, next);
            cacheBlock = i + 1;
            cacheK = found ? k : kYinMax - 1;
            voiced[i] = found ? 1 : 0;
        }
    }

    clock.Mark(1);
    // formants on x[::2] (11025 Hz), 330-sample frames every 110 samples, voiced frames only
    std::vector<double> F(3 * n, nan);
    {
        const std::size_t len2 = (len + 1) / 2;
        const std::size_t n2 = 1 + (len2 >= std::size_t(kWin2) ? (len2 - kWin2) / kHop2 : 0);
        double fr2[kWin2];
        for (std::size_t i = 0; i < n && i < n2; ++i) {
            if (!voiced[i]) continue;
            const std::size_t s2 = i * kHop2;
            for (int j = 0; j < kWin2; ++j) fr2[j] = (s2 + j < len2) ? double(x[2 * (s2 + j)]) : 0.0;
            Formants(fr2, ham.data(), &F[3 * i]);
        }
    }

    clock.Mark(2);
    // mouth_track
    const double kf = female ? 1.16 : 1.0;
    std::vector<double> jaw(n), wide(n), rnd(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double f1n = F[3 * i] / kf, f2n = F[3 * i + 1] / kf;
        const double openness = std::isfinite(f1n) ? Clip01((f1n - 280) / 500) : 0.4;
        const double level = Clip01(loud[i] / 0.35);
        double j = level * (0.08 + 0.92 * Mem(std::pow(syll[i], 1.3))) * (0.5 + 0.7 * openness);
        const double fric = Clip01((hf[i] - 0.25) / 0.35) * (voiced[i] ? 0.0 : 1.0);
        j *= (1 - 0.65 * fric);
        const bool good = voiced[i] && std::isfinite(f2n);
        const double f1z = std::isnan(f1n) ? 500.0 : f1n;
        double wd = good ? Clip01((f2n - 1500) / 600) * Clip01((640 - f1z) / 260) : 0.0;
        wd = (std::max)(wd, 0.45 * fric * level);
        double rd = good ? Clip01((1250 - f2n) / 450) : 0.0;
        wd *= level;
        rd *= level;
        jaw[i] = Clip01(j);
        wide[i] = wd;
        rnd[i] = rd;
    }
    Smooth(jaw, 0.02, 0.045);
    Smooth(wide, 0.045, 0.08);
    Smooth(rnd, 0.045, 0.08);
    // 40 ms lead (shift left by 4 frames, zero fill), then an 8-frame fade-out ramp (np.linspace(1, 0, tail))
    std::vector<double> ramp;
    const std::size_t tail = (std::min)(n, std::size_t(8));
    ramp.resize(tail);
    if (tail == 1) {
        ramp[0] = 1.0;
    } else {
        const double step = (0.0 - 1.0) / double(tail - 1);
        for (std::size_t j = 0; j < tail; ++j) ramp[j] = double(j) * step + 1.0;
        ramp[tail - 1] = 0.0;
    }
    out.frames.resize(4 * n);
    for (std::size_t i = 0; i < n; ++i) {
        double a = i + kLead < n ? jaw[i + kLead] : 0.0;
        double b = i + kLead < n ? wide[i + kLead] : 0.0;
        double c = i + kLead < n ? rnd[i + kLead] : 0.0;
        if (i >= n - tail) {
            const double r = ramp[i - (n - tail)];
            a *= r;
            b *= r;
            c *= r;
        }
        std::uint8_t* q = &out.frames[4 * i];
        q[0] = Quant(a);
        q[1] = Quant(b);
        q[2] = Quant(c);
        q[3] = Quant(loud[i]);
    }
    out.frameCount = std::uint32_t(n);
    out.duration = float(Mem(std::round(double(len) / kRate * 10000.0)) / 10000.0);
    clock.Mark(3);
    if (dbg) {
        dbg->db = db;
        dbg->loud = loud;
        dbg->syll = syll;
        dbg->hf = hf;
        dbg->F = F;
        dbg->voiced = voiced;
    }
    xs.resize(len);
    if (error) error->clear();
    return true;
}

}  // namespace detail

// PCM16 (interleaved, 1 or 2 channels, any rate 4000..192000; tested 8000..48000) -> mono float at 22050 Hz,
// sample for sample what ffmpeg -ac 1 -ar 22050 -f f32le gives (see the header comment).
inline bool ToAnalysisRate(const std::int16_t* pcm, std::size_t sampleFrames, unsigned channels, unsigned rate,
                           std::vector<float>& out, std::string* error = nullptr) {
    out.clear();
    if (!pcm || !sampleFrames) return detail::Fail(error, "empty clip");
    if (channels != 1 && channels != 2) return detail::Fail(error, "lip sync: only mono or stereo PCM");
    if (rate < 4000 || rate > 192000) return detail::Fail(error, "lip sync: sample rate outside 4000..192000 Hz");
    if (sampleFrames > std::size_t(rate) * 1800u) return detail::Fail(error, "lip sync: clip longer than 30 minutes");
    if (rate == unsigned(detail::kRate)) {
        out.reserve(sampleFrames + detail::kWin + 16);   // room for the analysis' zero padding
        out.resize(sampleFrames);
        if (channels == 1) {
            for (std::size_t i = 0; i < sampleFrames; ++i) out[i] = float(pcm[i]) * detail::kS16;
        } else {
            for (std::size_t i = 0; i < sampleFrames; ++i) {
                const float l = detail::kSqrtHalf * (float(pcm[2 * i]) * detail::kS16);
                const float r = detail::kSqrtHalf * (float(pcm[2 * i + 1]) * detail::kS16);
                out[i] = l + r;
            }
        }
    } else {
        detail::Resampler rs;
        rs.Init(int(rate), detail::kRate);
        rs.Run(pcm, sampleFrames, channels, out);
    }
    if (out.empty()) return detail::Fail(error, "lip sync: clip too short");
    if (error) error->clear();
    return true;
}

// Mono float samples at 22050 Hz (scale +-1, as the Python's float32 decode) -> track.
inline bool AnalyzeMono(const float* mono22050, std::size_t len, bool female, Track& out, std::string* error = nullptr) {
    out = Track{};
    if (!mono22050 || !len) return detail::Fail(error, "empty clip");
    std::vector<float> xs;
    xs.reserve(len + detail::kWin + 16);
    xs.assign(mono22050, mono22050 + len);
    return detail::AnalyzeSignal(xs, female, out, error, nullptr);
}

// The whole thing: PCM16 clip -> lip-sync track (female = speaker 0 or 7).
inline bool Analyze(const std::int16_t* pcm, std::size_t sampleFrames, unsigned channels, unsigned rate, bool female,
                    Track& out, std::string* error = nullptr) {
    out = Track{};
    std::vector<float> xs;
    if (!ToAnalysisRate(pcm, sampleFrames, channels, rate, xs, error)) return false;
    return detail::AnalyzeSignal(xs, female, out, error, nullptr);
}

}  // namespace re3lipauto
