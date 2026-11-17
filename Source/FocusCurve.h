// Curva di focus (stile "Soothe"): decide in quali zone dello spettro il limiter interviene.
// Peso w(f) = 10^(c(f)/20), con c(f) = somma in dB della risposta dei punti (max w = 2, cioe' +6 dB),
// moltiplicato per il "Balance" (inclinazione continua bassi <-> alti).
#pragma once
#include <algorithm>
#include <cmath>

namespace sl
{
constexpr int kMaxCurvePoints = 6;

enum CurveType { kOff = 0, kLowCut = 1, kLowShelf = 2, kPeak = 3, kHighShelf = 4, kHighCut = 5 };

struct CurvePoint
{
    int type = kOff;
    double freq = 1000.0;  // Hz
    double gainDb = 0.0;   // -24..+6 (ignorato dai cut)
    double q = 1.0;        // 0.3..8
    bool operator== (const CurvePoint& o) const { return type == o.type && freq == o.freq && gainDb == o.gainDb && q == o.q; }
};

// Risposta in ampiezza di un biquad RBJ (fs di riferimento 48 kHz) alla frequenza f, in dB.
inline double curvePointDb (const CurvePoint& p, double f)
{
    if (p.type == kOff) return 0.0;
    const double fsRef = 48000.0, pi = 3.14159265358979323846;
    f = std::max (1.0, std::min (23900.0, f));
    const double w0 = 2.0 * pi * std::min (std::max (p.freq, 10.0), 23000.0) / fsRef;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double Q = std::max (0.2, p.q);
    const double alpha = sw / (2.0 * Q);
    const double A = std::pow (10.0, p.gainDb / 40.0);
    const double sA = 2.0 * std::sqrt (A) * alpha;
    double b0, b1, b2, a0, a1, a2;
    switch (p.type)
    {
        case kPeak:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
        case kLowShelf:
            b0 = A * ((A + 1) - (A - 1) * cw + sA); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sA);
            a0 = (A + 1) + (A - 1) * cw + sA; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sA; break;
        case kHighShelf:
            b0 = A * ((A + 1) + (A - 1) * cw + sA); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sA);
            a0 = (A + 1) - (A - 1) * cw + sA; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sA; break;
        case kLowCut:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        default: // kHighCut
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
    }
    const double w = 2.0 * pi * f / fsRef;
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
    const double dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    const double m2 = (nr * nr + ni * ni) / std::max (1e-30, dr * dr + di * di);
    return std::max (-80.0, 10.0 * std::log10 (std::max (1e-30, m2)));
}

inline double curveTotalDb (const CurvePoint* pts, int n, double f)
{
    double c = 0.0;
    for (int i = 0; i < n; ++i) c += curvePointDb (pts[i], f);
    return c;
}

// Peso di intervento della curva (0..2).
inline double curveWeight (const CurvePoint* pts, int n, double f)
{
    return std::min (2.0, std::pow (10.0, curveTotalDb (pts, n, f) / 20.0));
}

// Balance: -1 = interviene solo sui bassi, +1 = solo sugli alti, 0 = uniforme; continuo.
// La zona "bassi" arriva a ~200 Hz, quella "alti" parte da ~8 kHz, in mezzo transizione liscia (log).
inline double tiltWeight (double balance, double f)
{
    const double b = std::max (-1.0, std::min (1.0, balance));
    const double x = std::log2 (std::max (1.0, f) / 200.0) / std::log2 (8000.0 / 200.0);
    const double t = std::max (0.0, std::min (1.0, x));
    const double ramp = t * t * (3.0 - 2.0 * t); // 0 = bassi, 1 = alti
    return b >= 0.0 ? (1.0 - b) + b * ramp : (1.0 + b) + (-b) * (1.0 - ramp);
}
} // namespace sl
