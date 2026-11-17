// Oversampling 1x / 2x / 4x attorno al motore: l'intero motore gira a fs*f con FFT scalata di f
// (stessa risoluzione in Hz, stessa latenza di finestra). Filtri FIR a fase lineare (Kaiser, 64f+1 tap).
#pragma once
#include "SpectralLimiterEngine.h"
#include <memory>
#include <vector>

namespace sl
{
class OsEngine
{
public:
    // factor: 1, 2 o 4
    void prepare (double baseFs, int baseFft, int factor, int maxBlock = 4096)
    {
        f = factor;
        baseRate = baseFs;
        blockMax = maxBlock;
        eng.prepare (baseFs * f, baseFft * f, baseFs);
        if (f == 1) { latBase = eng.latencySamples(); return; }

        M = 64 * f + 1;
        const int D = (M - 1) / 2;
        h.assign ((size_t) M, 0.0);
        const double beta = 10.0, fc = 0.5 / f; // taglio a fs_base/2, in unita' di fs_os
        const double i0b = bessel (beta);
        double sum = 0.0;
        for (int i = 0; i < M; ++i)
        {
            const double x = (double) (i - D);
            const double sinc = x == 0.0 ? 2.0 * fc : std::sin (2.0 * kPi * fc * x) / (kPi * x);
            const double r = x / (double) D;
            h[(size_t) i] = sinc * bessel (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            sum += h[(size_t) i];
        }
        for (auto& v : h) v /= sum;

        const int dTot = 2 * D + eng.latencySamples();
        latBase = dTot / f;            // latenza in campioni di base (esatta grazie all'offset di decimazione)
        off = dTot - f * latBase;      // fase di decimazione, 0..f-1

        histL.assign (65, 0.0); histR.assign (65, 0.0);
        upL.assign ((size_t) (blockMax * f), 0.f); upR.assign ((size_t) (blockMax * f), 0.f);
        zL.assign ((size_t) (M - 1 + blockMax * f), 0.f); zR.assign (zL.size(), 0.f);
        reset();
    }

    void reset()
    {
        eng.reset();
        if (f == 1) return;
        std::fill (histL.begin(), histL.end(), 0.0); std::fill (histR.begin(), histR.end(), 0.0);
        std::fill (zL.begin(), zL.end(), 0.f); std::fill (zR.begin(), zR.end(), 0.f);
        hpos = 0;
    }

    void setParams (const Engine::Params& p)
    {
        eng.setParams (p);
        ceilLin = std::pow (10.0, p.ceilingDb / 20.0);
    }

    int latencySamples() const { return latBase; }
    int factor() const { return f; }
    double baseSampleRate() const { return baseRate; }
    const Engine& core() const { return eng; }

    void process (float* l, float* r, int n)
    {
        if (f == 1) { eng.process (l, r, n); return; }
        int pos = 0;
        while (pos < n)
        {
            const int c = std::min (blockMax, n - pos);
            chunk (l + pos, r + pos, c);
            pos += c;
        }
    }

private:
    static double bessel (double x)
    {
        double s = 1.0, t = 1.0;
        for (int i = 1; i < 60; ++i) { t *= (x / (2.0 * i)) * (x / (2.0 * i)); s += t; if (t < 1e-18 * s) break; }
        return s;
    }

    void chunk (float* l, float* r, int c)
    {
        // interpolazione polifase (guadagno f per compensare lo zero-stuffing)
        for (int i = 0; i < c; ++i)
        {
            double xl = l[i], xr = r[i];
            if (! (std::abs (xl) < 1.0e6)) xl = 0.0;
            if (! (std::abs (xr) < 1.0e6)) xr = 0.0;
            histL[(size_t) hpos] = xl; histR[(size_t) hpos] = xr;
            for (int p = 0; p < f; ++p)
            {
                double sl = 0.0, sr = 0.0;
                int hp = hpos;
                for (int k = p; k < M; k += f)
                {
                    sl += h[(size_t) k] * histL[(size_t) hp];
                    sr += h[(size_t) k] * histR[(size_t) hp];
                    if (--hp < 0) hp = 64;
                }
                upL[(size_t) (i * f + p)] = (float) (sl * f);
                upR[(size_t) (i * f + p)] = (float) (sr * f);
            }
            if (++hpos == 65) hpos = 0;
        }

        eng.process (upL.data(), upR.data(), c * f);

        // decimazione: z = [coda precedente | nuovo blocco]
        const size_t keep = (size_t) (M - 1);
        std::copy (upL.begin(), upL.begin() + (long) (c * f), zL.begin() + (long) keep);
        std::copy (upR.begin(), upR.begin() + (long) (c * f), zR.begin() + (long) keep);
        for (int i = 0; i < c; ++i)
        {
            const size_t q = keep + (size_t) (i * f + off);
            double sl = 0.0, sr = 0.0;
            for (int k = 0; k < M; ++k)
            {
                sl += h[(size_t) k] * zL[q - (size_t) k];
                sr += h[(size_t) k] * zR[q - (size_t) k];
            }
            l[i] = (float) std::max (-ceilLin, std::min (ceilLin, sl));
            r[i] = (float) std::max (-ceilLin, std::min (ceilLin, sr));
        }
        // conserva la coda per il prossimo blocco (ultimi M-1 campioni a livello OS)
        const size_t total = keep + (size_t) (c * f);
        std::copy (zL.begin() + (long) (total - keep), zL.begin() + (long) total, zL.begin());
        std::copy (zR.begin() + (long) (total - keep), zR.begin() + (long) total, zR.begin());
    }

    Engine eng;
    int f = 1, M = 1, off = 0, latBase = 0, blockMax = 4096, hpos = 0;
    double baseRate = 48000.0, ceilLin = 1.0;
    std::vector<double> h, histL, histR;
    std::vector<float> upL, upR, zL, zR;
};
} // namespace sl
