// SpectralLimiterEngine.h
// Motore DSP del Limiter Spettrale. Header-only, C++17, nessuna dipendenza (niente JUCE):
// si compila e si collauda da solo (vedi tests/).
//
// Catena:   STFT spettrale (una banda di guadagno per bin) -> gain
//           -> brickwall lookahead con rilevazione true-peak 8x -> ceiling
//
//  * Stadio spettrale: FFT con finestra Hann in analisi e sintesi, overlap 87.5% (hop = N/8). Ogni bin ha il suo
//    guadagno. Il livello di banda di un bin e' l'ampiezza equivalente-sinusoide dell'energia nei
//    bin vicini (larghezza ~1/7 di ottava, minimo 5 bin). Sopra la soglia: riduzione con knee morbido,
//    attacco istantaneo (il frame "vede" gia' tutta la finestra) e rilascio esponenziale per bin.
//    I guadagni sono reali: la fase non viene toccata. Il canale sinistro e destro sono collegati
//    (si usa il massimo dei due) per non spostare l'immagine stereo.
//  * Stadio brickwall: lookahead ~2 ms, minimo scorrevole + doppio filtro a media mobile (garantisce
//    guadagno <= requisito su tutta la finestra), rilascio esponenziale, picco misurato su segnale
//    sovracampionato 8x (kernel Kaiser, 16 tap per fase), stereo collegato, clamp finale di sicurezza.
//
// Latenza (campioni) = fftSize + lookahead - 1 + 16. Va comunicata all'host.
#pragma once

#include <algorithm>
#include <atomic>
#include "FocusCurve.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace sl
{
constexpr double kPi = 3.14159265358979323846;

inline double dbToLin (double db) noexcept { return std::pow (10.0, db * 0.05); }
inline double linToDb (double x) noexcept { return 20.0 * std::log10 (x > 1e-12 ? x : 1e-12); }

//==============================================================================
// FFT radix-2 in place, parte reale e immaginaria separate, twiddle contigui per stadio.
class FFT
{
public:
    void init (int n)
    {
        N = n;
        int bits = 0;
        while ((1 << bits) < N) ++bits;
        rev.assign ((size_t) N, 0);
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        twr.assign ((size_t) N, 0.0);
        twi.assign ((size_t) N, 0.0);
        for (int h = 1; h < N; h <<= 1)
            for (int j = 0; j < h; ++j)
            {
                const double a = -kPi * j / h;
                twr[(size_t) (h - 1 + j)] = std::cos (a);
                twi[(size_t) (h - 1 + j)] = std::sin (a);
            }
    }

    void forward (double* re, double* im) const { run (re, im, 1.0); }
    void inverse (double* re, double* im) const { run (re, im, -1.0); } // non normalizzata

private:
    void run (double* re, double* im, double sgn) const
    {
        for (int i = 0; i < N; ++i)
        {
            const int r = rev[(size_t) i];
            if (i < r) { std::swap (re[i], re[r]); std::swap (im[i], im[r]); }
        }
        for (int h = 1; h < N; h <<= 1)
        {
            const double* wr = &twr[(size_t) (h - 1)];
            const double* wi = &twi[(size_t) (h - 1)];
            for (int i = 0; i < N; i += 2 * h)
                for (int j = 0; j < h; ++j)
                {
                    const double c = wr[j], s = sgn * wi[j];
                    const int a = i + j, b = a + h;
                    const double tr = re[b] * c - im[b] * s;
                    const double ti = re[b] * s + im[b] * c;
                    re[b] = re[a] - tr;  im[b] = im[a] - ti;
                    re[a] += tr;         im[a] += ti;
                }
        }
    }

    int N = 0;
    std::vector<int> rev;
    std::vector<double> twr, twi;
};

//==============================================================================
// Minimo su finestra scorrevole di L campioni (deque monotona).
class SlidingMin
{
public:
    void init (int len)
    {
        L = len;
        cap = len + 1;
        val.assign ((size_t) cap, 1.0);
        idx.assign ((size_t) cap, 0);
        reset();
    }
    void reset() { head = 0; count = 0; pos = 0; }

    double push (double v)
    {
        while (count > 0)
        {
            int back = head + count - 1;
            if (back >= cap) back -= cap;
            if (val[(size_t) back] >= v) --count; else break;
        }
        int w = head + count;
        if (w >= cap) w -= cap;
        val[(size_t) w] = v;
        idx[(size_t) w] = pos;
        ++count;
        while (idx[(size_t) head] <= pos - L)
        {
            if (++head == cap) head = 0;
            --count;
        }
        ++pos;
        return val[(size_t) head];
    }

private:
    int L = 1, cap = 2, head = 0, count = 0;
    long long pos = 0;
    std::vector<double> val;
    std::vector<long long> idx;
};

// Media mobile di lunghezza len, somma corrente.
class Box
{
public:
    void init (int len) { L = std::max (1, len); buf.assign ((size_t) L, 1.0); reset(); }
    void reset() { std::fill (buf.begin(), buf.end(), 1.0); sum = (double) L; pos = 0; }
    double push (double x)
    {
        sum += x - buf[(size_t) pos];
        buf[(size_t) pos] = x;
        if (++pos == L) pos = 0;
        return sum / (double) L;
    }

private:
    int L = 1, pos = 0;
    double sum = 1.0;
    std::vector<double> buf;
};

//==============================================================================
// Rilevatore di picco true-peak 8x (solo rilevazione). Restituisce il picco, sul segnale
// sovracampionato, attorno al campione "centrale" c = n - kHalf, massimo tra i due intervalli adiacenti.
// Se la finestra e' abbastanza bassa da non poter superare il ceiling (limite di Lebesgue del kernel),
// salta l'interpolazione: stesso risultato, meno CPU.
class TruePeak
{
public:
    static constexpr int kTaps = 16;
    static constexpr int kHalf = kTaps / 2;
    static constexpr int kPhases = 7; // sovracampionamento 8x -> 7 punti interpolati per intervallo

    void init()
    {
        const double beta = 7.0;
        const double i0b = besselI0 (beta);
        lebesgue = 1.0;
        for (int p = 1; p <= kPhases; ++p)
        {
            const double f = p / 8.0;
            double s = 0.0, sa = 0.0;
            for (int i = 0; i < kTaps; ++i)
            {
                const double d = (double) (i - kHalf + 1) - f;
                const double x = d / (double) kHalf;
                const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - x * x))) / i0b;
                const double sinc = std::sin (kPi * d) / (kPi * d);
                k[p - 1][i] = sinc * w;
                s += k[p - 1][i];
            }
            for (int i = 0; i < kTaps; ++i) { k[p - 1][i] /= s; sa += std::abs (k[p - 1][i]); }
            lebesgue = std::max (lebesgue, sa);
        }
        reset();
    }

    void reset()
    {
        std::fill (std::begin (hl), std::end (hl), 0.0);
        std::fill (std::begin (hr), std::end (hr), 0.0);
        pos = 0;
        prevPk = 0.0;
    }

    double process (double xl, double xr, double ceiling)
    {
        hl[pos] = hl[pos + kTaps] = xl;
        hr[pos] = hr[pos + kTaps] = xr;
        if (++pos == kTaps) pos = 0;
        const double* wl = &hl[pos];
        const double* wr = &hr[pos];
        double m = std::max (std::abs (wl[kHalf - 1]), std::abs (wr[kHalf - 1]));

        double wmax = 0.0;
        for (int i = 0; i < kTaps; ++i) wmax = std::max (wmax, std::max (std::abs (wl[i]), std::abs (wr[i])));
        if (wmax * lebesgue > ceiling)
        {
            for (int p = 0; p < kPhases; ++p)
            {
                double sl = 0.0, sr = 0.0;
                for (int i = 0; i < kTaps; ++i) { sl += k[p][i] * wl[i]; sr += k[p][i] * wr[i]; }
                m = std::max (m, std::max (std::abs (sl), std::abs (sr)));
            }
        }
        const double out = std::max (m, prevPk);
        prevPk = m;
        return out;
    }

private:
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int i = 1; i < 60; ++i)
        {
            const double t = x / (2.0 * i);
            term *= t * t;
            sum += term;
            if (term < 1e-16 * sum) break;
        }
        return sum;
    }

    double k[kPhases][kTaps] {};
    double hl[2 * kTaps] {}, hr[2 * kTaps] {};
    int pos = 0;
    double prevPk = 0.0, lebesgue = 1.5;
};

//==============================================================================
class Engine
{
public:
    static constexpr int kDisplayPoints = 200;

    struct Params
    {
        double thresholdDb = -18.0; // livello di banda (equivalente-sinusoide) oltre il quale si limita
        double gainDb      = 0.0;   // gain dopo lo stadio spettrale, prima del brickwall
        double balance     = 0.0;   // inclinazione dell'intervento: -1 = solo bassi ... +1 = solo alti (continuo)
        double ceilingDb   = -1.0;  // massimo assoluto in uscita (true-peak)
        double releaseMs   = 150.0; // recupero di entrambi gli stadi
        double ratio       = 100.0; // >= 100 = infinito (limiter vero); 1 = nessuna riduzione
        CurvePoint curve[kMaxCurvePoints]; // curva di focus (punti con type == kOff sono ignorati)
        double attackMs    = 0.0;   // attacco dello stadio spettrale (0 = istantaneo); per frame, minimo = hop
        double compensation = 0.0;  // 0..1: la soglia di ogni banda segue il suo livello medio rispetto allo spettro medio
        bool   dynamicComp = false; // false = profilo lento (~8 s), true = ogni banda segue il proprio livello (~0.25 s)
        double maxReductionDb = 60.0; // riduzione massima per banda nello stadio spettrale
        double resonance = 0.0;     // 0..1: sopprime i picchi stretti sopra l'inviluppo locale dello spettro
        bool   hold = false;        // "Learn": la riduzione per banda si accumula e non viene mai rilasciata
        double releaseTilt = 0.0;   // -1..+1: +1 = bassi con rilascio lento e alti veloce, -1 il contrario
    };

    // Dati per l'interfaccia (scritti dal thread audio, letti dalla UI).
    std::atomic<float> dispIn[kDisplayPoints];   // spettro in ingresso, dB
    std::atomic<float> dispOut[kDisplayPoints];  // spettro dopo lo stadio spettrale, dB
    std::atomic<float> dispGR[kDisplayPoints];   // riduzione per banda, dB (<= 0)
    std::atomic<float> dispThr[kDisplayPoints];  // soglia effettiva per banda (con compensazione), dB
    std::atomic<float> meterGRdb { 0.0f };       // riduzione massima brickwall dell'ultimo blocco, dB (<= 0)
    std::atomic<float> meterOutDb { -120.0f };   // picco in uscita dell'ultimo blocco, dB

    Engine()
    {
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            dispIn[i].store (-120.0f);
            dispOut[i].store (-120.0f);
            dispGR[i].store (0.0f);
            dispThr[i].store (-18.0f);
        }
    }

    // displayFs: frequenza di campionamento "di base" per l'asse del display (0 = uguale a sampleRate)
    void prepare (double sampleRate, int fftSize, double displayFs = 0.0)
    {
        fs = sampleRate;
        dispFs = displayFs > 0.0 ? displayFs : sampleRate;
        N = fftSize;
        H = N / 8;
        mask = N - 1;
        nb = N / 2 + 1;
        fft.init (N);

        win.assign ((size_t) N, 0.0);
        double sw = 0.0, sw2 = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double sn = std::sin (kPi * i / N);
            win[(size_t) i] = sn * sn; // Hann periodica, usata in analisi e in sintesi
            sw += win[(size_t) i];
            sw2 += win[(size_t) i] * win[(size_t) i];
        }
        magNorm = 2.0 / sw;               // ampiezza di una sinusoide = 1.0 nel suo bin
        enbw = (double) N * sw2 / (sw * sw);
        olaScale = 1.0 / (N * ((double) N / H) * sw2 / N); // 1 / (N * somma di w^2 sovrapposti)

        inL.assign ((size_t) N, 0.0);  inR.assign ((size_t) N, 0.0);
        olaL.assign ((size_t) N, 0.0); olaR.assign ((size_t) N, 0.0);
        re.assign ((size_t) N, 0.0);   im.assign ((size_t) N, 0.0);
        xlr.assign ((size_t) nb, 0.0); xli.assign ((size_t) nb, 0.0);
        xrr.assign ((size_t) nb, 0.0); xri.assign ((size_t) nb, 0.0);
        mag.assign ((size_t) nb, 0.0);
        prefix.assign ((size_t) nb + 1, 0.0);
        gain.assign ((size_t) nb, 1.0);
        tgt.assign ((size_t) nb, 1.0);
        dil.assign ((size_t) nb, 1.0);
        smoothK.assign ((size_t) (2 * kSmooth + 1), 0.0);
        {
            double s = 0.0;
            for (int j = -kSmooth; j <= kSmooth; ++j)
            {
                const double w = 0.5 * (1.0 + std::cos (kPi * j / (kSmooth + 1)));
                smoothK[(size_t) (j + kSmooth)] = w;
                s += w;
            }
            for (auto& w : smoothK) w /= s;
        }
        wBin.assign ((size_t) nb, 1.0);
        aDbv.assign ((size_t) nb, -100.0);
        envPrefix.assign ((size_t) nb + 1, 0.0);
        resRed.assign ((size_t) nb, 0.0);
        relSbin.assign ((size_t) nb, 0.9);
        relValid = false;
        lvl.assign ((size_t) nb, -100.0);
        lvlPrefix.assign ((size_t) nb + 1, 0.0);
        thrK.assign ((size_t) nb, -18.0);
        offK.assign ((size_t) nb, 0.0);
        {
            wRef.assign ((size_t) nb, 0.0);
            const double bh = fs / N;
            int kLo = std::max (1, (int) std::ceil (30.0 / bh));
            int kHi = std::min (nb - 1, (int) std::floor (std::min (16000.0, 0.499 * dispFs) / bh));
            if (kHi <= kLo) { kLo = 1; kHi = std::max (1, nb - 1); }
            double sum = 0.0;
            for (int k = kLo; k <= kHi; ++k) { wRef[(size_t) k] = 1.0 / (double) (k + 1); sum += wRef[(size_t) k]; } // ~ uniforme in frequenza log
            for (auto& v : wRef) v /= sum;
        }
        lvlCoefStatic = 1.0 - std::exp (-(double) H / (8.0 * fs));
        lvlCoefDyn = 1.0 - std::exp (-(double) H / (0.25 * fs));
        levelFrames = 0;
        wCurveValid = false;
        halfW.assign ((size_t) nb, 2);
        for (int k = 0; k < nb; ++k)
            halfW[(size_t) k] = std::max (2, (int) std::lround (0.05 * k));

        buildDisplayMap();

        lookahead = std::max (16, (int) std::lround (0.002 * fs));
        const int boxA = lookahead / 2;
        const int boxB = lookahead - boxA + 1;
        smin.init (lookahead + 2 * TruePeak::kHalf);
        box1.init (boxA);
        box2.init (boxB);
        tp.init();
        delay = lookahead - 1 + 2 * TruePeak::kHalf;
        dlL.assign ((size_t) delay + 1, 0.0);
        dlR.assign ((size_t) delay + 1, 0.0);

        smoothCoef = 1.0 - std::exp (-1.0 / (0.02 * fs));
        prepared = true;
        setParams (params);
        reset();
    }

    void reset()
    {
        std::fill (inL.begin(), inL.end(), 0.0);  std::fill (inR.begin(), inR.end(), 0.0);
        std::fill (olaL.begin(), olaL.end(), 0.0); std::fill (olaR.begin(), olaR.end(), 0.0);
        std::fill (gain.begin(), gain.end(), 1.0);
        std::fill (dlL.begin(), dlL.end(), 0.0);  std::fill (dlR.begin(), dlR.end(), 0.0);
        std::fill (lvl.begin(), lvl.end(), -100.0);
        levelFrames = 0;
        t = 0; frameCount = 0; dpos = 0; gPrev = 1.0;
        smin.reset(); box1.reset(); box2.reset(); tp.reset();
        gainSm = gainTarget;
    }

    void setParams (const Params& p)
    {
        params = p;
        if (! prepared) return;
        gainTarget = dbToLin (p.gainDb);
        updateWeights();
        ceilLin = dbToLin (p.ceilingDb);
        ceilDet = ceilLin * 0.9606; // margine -0.35 dB sulla rilevazione true-peak (gain vicini non uniformi)
        const double tau = std::max (p.releaseMs, 1.0) * 1e-3;
        relB = std::exp (-1.0 / (tau * fs));
        const double tauS = std::max (tau, 2.0 * H / fs);
        relS = std::exp (-(double) H / (tauS * fs));
        updateRelease();
        ratioSlope = p.ratio >= 100.0 ? 1.0 : 1.0 - 1.0 / std::max (1.0, p.ratio);
        atkS = p.attackMs <= 0.5 ? 1.0 : 1.0 - std::exp (-(double) H / (p.attackMs * 1e-3 * fs));
    }

    int latencySamples() const { return N + delay; }
    int fftSize() const { return N; }
    int bandCount() const { return nb; }
    double sampleRate() const { return fs; }

    void process (float* l, float* r, int n)
    {
        double gMin = 1.0, pkOut = 0.0;
        for (int s = 0; s < n; ++s)
        {
            gainSm += (gainTarget - gainSm) * smoothCoef;

            double xl = (double) l[s], xr = (double) r[s];
            if (! (std::abs (xl) < 1.0e6)) xl = 0.0; // scarta NaN e inf
            if (! (std::abs (xr) < 1.0e6)) xr = 0.0;

            double yl, yr;
            stftStep (xl, xr, yl, yr);
            yl *= gainSm;
            yr *= gainSm;

            double ol, orr, g;
            brickStep (yl, yr, ol, orr, g);
            l[s] = (float) ol;
            r[s] = (float) orr;
            gMin = std::min (gMin, g);
            pkOut = std::max (pkOut, std::max (std::abs (ol), std::abs (orr)));
        }
        meterGRdb.store ((float) linToDb (gMin));
        meterOutDb.store ((float) linToDb (pkOut));
    }

private:
    //----------------------------------------------------------------------------
    inline void stftStep (double inl, double inr, double& outl, double& outr)
    {
        const int slot = (int) (t & mask);
        outl = olaL[(size_t) slot];  olaL[(size_t) slot] = 0.0; // legge e libera prima del frame
        outr = olaR[(size_t) slot];  olaR[(size_t) slot] = 0.0;
        inL[(size_t) slot] = inl;
        inR[(size_t) slot] = inr;
        if (((t + 1) & (H - 1)) == 0) processFrame();
        ++t;
    }

    // Rilascio per bin: il rilascio nominale scalato in funzione della frequenza (Release Tilt), su griglia log.
    void updateRelease()
    {
        if (relValid && params.releaseMs == relMsCached && params.releaseTilt == relTiltCached) return;
        relValid = true; relMsCached = params.releaseMs; relTiltCached = params.releaseTilt;
        constexpr int G = 256;
        constexpr double fLo = 20.0, fHi = 20000.0;
        double grid[G];
        const double ratio = std::log (fHi / fLo);
        const double tau = std::max (params.releaseMs, 1.0) * 1e-3;
        const double tilt = std::max (-1.0, std::min (1.0, params.releaseTilt));
        for (int g = 0; g < G; ++g)
        {
            const double f = fLo * std::exp (ratio * g / (G - 1));
            const double factor = std::max (0.125, std::min (8.0, std::pow (2.0, -tilt * std::log2 (f / 500.0))));
            const double tk = std::max (tau * factor, 2.0 * H / fs);
            grid[g] = std::exp (-(double) H / (tk * fs));
        }
        const double binHz = fs / N;
        for (int k = 0; k < nb; ++k)
        {
            const double f = std::max (k * binHz, 1.0);
            const double pos = std::max (0.0, std::min ((double) (G - 1), std::log (f / fLo) / ratio * (G - 1)));
            const int i0 = std::min (G - 2, (int) pos);
            const double fr = pos - i0;
            relSbin[(size_t) k] = grid[i0] * (1.0 - fr) + grid[i0 + 1] * fr;
        }
    }

    // Pesi per bin dalla curva di focus + balance. Calcolo su griglia log di 512 punti, poi interpolazione.
    void updateWeights()
    {
        bool same = wCurveValid && params.balance == wBalance;
        for (int i = 0; same && i < kMaxCurvePoints; ++i) same = (params.curve[i] == wCurve[i]);
        if (same || nb <= 0) return;
        wBalance = params.balance;
        for (int i = 0; i < kMaxCurvePoints; ++i) wCurve[i] = params.curve[i];
        wCurveValid = true;

        constexpr int G = 512;
        constexpr double fLo = 20.0, fHi = 20000.0;
        double grid[G];
        const double ratio = std::log (fHi / fLo);
        for (int g = 0; g < G; ++g)
        {
            const double f = fLo * std::exp (ratio * g / (G - 1));
            grid[g] = curveWeight (wCurve, kMaxCurvePoints, f) * tiltWeight (wBalance, f);
        }
        const double binHz = fs / N;
        for (int k = 0; k < nb; ++k)
        {
            const double f = std::max (k * binHz, 1.0);
            const double pos = std::max (0.0, std::min ((double) (G - 1), std::log (f / fLo) / ratio * (G - 1)));
            const int i0 = std::min (G - 2, (int) pos);
            const double fr = pos - i0;
            wBin[(size_t) k] = grid[i0] * (1.0 - fr) + grid[i0 + 1] * fr;
        }
    }

    void processFrame()
    {
        const int nh = N / 2;
        for (int i = 0; i < N; ++i)
        {
            const int idx = (int) ((t + 1 + i) & mask);
            re[(size_t) i] = inL[(size_t) idx] * win[(size_t) i];
            im[(size_t) i] = inR[(size_t) idx] * win[(size_t) i];
        }
        fft.forward (re.data(), im.data());

        // separa L e R dallo spettro complesso impacchettato, magnitudine collegata (max dei due)
        for (int k = 0; k <= nh; ++k)
        {
            const int k2 = (N - k) & mask;
            const double zr = re[(size_t) k], zi = im[(size_t) k];
            const double cr = re[(size_t) k2], ci = im[(size_t) k2];
            const double lr = 0.5 * (zr + cr), li = 0.5 * (zi - ci);
            const double rr = 0.5 * (zi + ci), ri = 0.5 * (cr - zr);
            xlr[(size_t) k] = lr; xli[(size_t) k] = li;
            xrr[(size_t) k] = rr; xri[(size_t) k] = ri;
            const double ml = lr * lr + li * li, mr = rr * rr + ri * ri;
            const double m = magNorm * std::sqrt (std::max (ml, mr));
            mag[(size_t) k] = m;
            prefix[(size_t) k + 1] = prefix[(size_t) k] + m * m;
        }

        // 1) guadagno bersaglio per bin (soglia sul livello di banda, knee morbido).
        //    Con la compensazione la soglia di ogni banda si sposta in base al suo livello medio (in dB)
        //    rispetto al livello medio dello spettro: le bande naturalmente piu' forti (bassi) non lavorano di piu'.
        const double thr = params.thresholdDb;
        const double knee = 6.0;
        const double maxRed = std::max(0.0, params.maxReductionDb);
        const double comp = std::max (0.0, std::min (1.0, params.compensation));
        for (int k = 0; k <= nh; ++k)
        {
            const int lo = std::max (0, k - halfW[(size_t) k]);
            const int hi = std::min (nh, k + halfW[(size_t) k]);
            const double a2 = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / enbw;
            aDbv[(size_t) k] = a2 > 1.0e-10 ? 10.0 * std::log10 (a2) : -100.0;
        }
        {
            const double coef = params.dynamicComp ? lvlCoefDyn : lvlCoefStatic;
            const double ce = std::max (coef, 1.0 / (1.0 + (double) levelFrames)); // media corrente all'inizio
            if (levelFrames < 1000000) ++levelFrames;
            for (int k = 0; k <= nh; ++k) lvl[(size_t) k] += (aDbv[(size_t) k] - lvl[(size_t) k]) * ce;
        }
        if (comp > 0.0005)
        {
            lvlPrefix[0] = 0.0;
            for (int k = 0; k <= nh; ++k) lvlPrefix[(size_t) k + 1] = lvlPrefix[(size_t) k] + lvl[(size_t) k];
            double lref = 0.0;
            for (int k = 0; k <= nh; ++k)
            {
                const int w = params.dynamicComp ? halfW[(size_t) k] : std::max (2, (int) std::lround (0.2 * k));
                const int lo = std::max (0, k - w), hi = std::min (nh, k + w);
                const double e = (lvlPrefix[(size_t) hi + 1] - lvlPrefix[(size_t) lo]) / (double) (hi - lo + 1);
                offK[(size_t) k] = e; // livello medio di banda (provvisorio)
                lref += wRef[(size_t) k] * e;
            }
            for (int k = 0; k <= nh; ++k)
                offK[(size_t) k] = comp * std::max (-36.0, std::min (36.0, offK[(size_t) k] - lref));
        }
        else
            std::fill (offK.begin(), offK.end(), 0.0);

        // risonanze: picchi del livello di banda sopra l'inviluppo locale (media in dB su ~+-25% di frequenza)
        const double resAmt = std::max (0.0, std::min (1.0, params.resonance));
        const bool useRes = resAmt > 0.001;
        if (useRes)
        {
            envPrefix[0] = 0.0;
            for (int k = 0; k <= nh; ++k) envPrefix[(size_t) k + 1] = envPrefix[(size_t) k] + aDbv[(size_t) k];
            for (int k = 0; k <= nh; ++k)
            {
                const int w = std::max (3, (int) std::lround (0.25 * k));
                const int lo = std::max (0, k - w), hi = std::min (nh, k + w);
                const double env = (envPrefix[(size_t) hi + 1] - envPrefix[(size_t) lo]) / (double) (hi - lo + 1);
                const double ex = aDbv[(size_t) k] - env - 3.0; // tolleranza 3 dB
                resRed[(size_t) k] = ex > 0.0 ? ex * resAmt : 0.0;
            }
        }

        bool any = false;
        for (int k = 0; k <= nh; ++k)
        {
            const double thrk = thr + offK[(size_t) k];
            thrK[(size_t) k] = thrk;
            const double over = aDbv[(size_t) k] - thrk;
            double red = 0.0;
            if (over > -0.5 * knee)
            {
                red = over >= 0.5 * knee ? over
                                         : (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);
                red *= ratioSlope * wBin[(size_t) k];
            }
            if (useRes) red += resRed[(size_t) k] * wBin[(size_t) k];
            red = std::min (red, maxRed);
            double target = 1.0;
            if (red > 1.0e-4)
            {
                target = std::pow (10.0, -red * 0.05);
                any = true;
            }
            tgt[(size_t) k] = target;
        }

        // 2) smussa in frequenza: prima dilata il minimo di +-S bin (la riduzione sul picco resta intera),
        //    poi media con un kernel di Hann. Evita bordi netti nel guadagno, che causano aliasing temporale.
        if (any)
        {
            for (int k = 0; k <= nh; ++k)
            {
                double m = tgt[(size_t) k];
                for (int j = 1; j <= kSmooth; ++j)
                    m = std::min (m, std::min (tgt[(size_t) std::max (0, k - j)], tgt[(size_t) std::min (nh, k + j)]));
                dil[(size_t) k] = m;
            }
            for (int k = 0; k <= nh; ++k)
            {
                double acc = 0.0;
                for (int j = -kSmooth; j <= kSmooth; ++j)
                    acc += smoothK[(size_t) (j + kSmooth)] * dil[(size_t) std::max (0, std::min (nh, k + j))];
                tgt[(size_t) k] = acc;
            }
        }

        // 3) nel tempo: attacco istantaneo (il frame vede gia' tutta la finestra), rilascio esponenziale
        for (int k = 0; k <= nh; ++k)
        {
            double& g = gain[(size_t) k];
            const double target = tgt[(size_t) k];
            if (target < g) g += (target - g) * atkS;
            else if (! params.hold) g += (target - g) * (1.0 - relSbin[(size_t) k]);
        }

        // applica i guadagni e ricostruisci lo spettro impacchettato
        for (int k = 0; k <= nh; ++k)
        {
            const double g = gain[(size_t) k];
            const double lr = g * xlr[(size_t) k], li = g * xli[(size_t) k];
            const double rr = g * xrr[(size_t) k], ri = g * xri[(size_t) k];
            re[(size_t) k] = lr - ri;
            im[(size_t) k] = li + rr;
            if (k != 0 && k != nh)
            {
                const int k2 = N - k;
                re[(size_t) k2] = lr + ri;
                im[(size_t) k2] = rr - li;
            }
        }
        fft.inverse (re.data(), im.data());

        for (int i = 0; i < N; ++i)
        {
            const int idx = (int) ((t + 1 + i) & mask);
            const double w = win[(size_t) i] * olaScale;
            olaL[(size_t) idx] += re[(size_t) i] * w;
            olaR[(size_t) idx] += im[(size_t) i] * w;
        }

        if ((frameCount++ & 1) == 0) updateDisplay();
    }

    //----------------------------------------------------------------------------
    inline void brickStep (double xl, double xr, double& outl, double& outr, double& gOut)
    {
        dlL[(size_t) dpos] = xl;
        dlR[(size_t) dpos] = xr;

        const double pk = tp.process (xl, xr, ceilDet);
        const double req = pk > ceilDet ? ceilDet / pk : 1.0;
        const double gm = smin.push (req);
        const double gr = std::min (gm, gPrev + (1.0 - gPrev) * (1.0 - relB));
        gPrev = gr;
        const double g = box2.push (box1.push (gr));

        int rd = dpos - delay;
        if (rd < 0) rd += delay + 1;
        if (++dpos == delay + 1) dpos = 0;

        outl = std::max (-ceilLin, std::min (ceilLin, dlL[(size_t) rd] * g));
        outr = std::max (-ceilLin, std::min (ceilLin, dlR[(size_t) rd] * g));
        gOut = g;
    }

    //----------------------------------------------------------------------------
    void buildDisplayMap()
    {
        dLo.assign (kDisplayPoints, 0);
        dHi.assign (kDisplayPoints, 0);
        dFrac.assign (kDisplayPoints, 0.0);
        const double binHz = fs / N;
        const double fMax = std::min (20000.0, 0.499 * dispFs);
        const double fMin = 20.0;
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            const double e0 = fMin * std::pow (fMax / fMin, (double) p / kDisplayPoints);
            const double e1 = fMin * std::pow (fMax / fMin, (double) (p + 1) / kDisplayPoints);
            int lo = std::max (0, std::min (nb - 1, (int) std::lround (e0 / binHz)));
            int hi = std::max (lo, std::min (nb - 1, (int) std::lround (e1 / binHz) - 1));
            dLo[(size_t) p] = lo;
            dHi[(size_t) p] = hi;
            dFrac[(size_t) p] = std::sqrt (e0 * e1) / binHz;
        }
    }

    void updateDisplay()
    {
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            const int lo = dLo[(size_t) p], hi = dHi[(size_t) p];
            double mi, mo, gmin, th;
            if (hi > lo)
            {
                mi = 0.0; mo = 0.0; gmin = 1.0; th = 0.0;
                for (int k = lo; k <= hi; ++k)
                {
                    mi = std::max (mi, mag[(size_t) k]);
                    mo = std::max (mo, mag[(size_t) k] * gain[(size_t) k]);
                    gmin = std::min (gmin, gain[(size_t) k]);
                    th += thrK[(size_t) k];
                }
                th /= (double) (hi - lo + 1);
            }
            else // intervallo piu' stretto di un bin: interpola per una curva liscia
            {
                const double x = std::max (0.0, std::min ((double) (nb - 1), dFrac[(size_t) p]));
                const int k0 = (int) x, k1 = std::min (nb - 1, k0 + 1);
                const double f = x - k0;
                mi = mag[(size_t) k0] * (1 - f) + mag[(size_t) k1] * f;
                mo = mag[(size_t) k0] * gain[(size_t) k0] * (1 - f) + mag[(size_t) k1] * gain[(size_t) k1] * f;
                gmin = gain[(size_t) k0] * (1 - f) + gain[(size_t) k1] * f;
                th = thrK[(size_t) k0] * (1 - f) + thrK[(size_t) k1] * f;
            }
            dispIn[p].store ((float) linToDb (mi));
            dispOut[p].store ((float) linToDb (mo));
            dispGR[p].store ((float) linToDb (gmin));
            dispThr[p].store ((float) th);
        }
    }

    //----------------------------------------------------------------------------
    Params params;
    bool prepared = false;
    double fs = 48000.0, dispFs = 48000.0;
    int N = 2048, H = 256, mask = 2047, nb = 1025;
    FFT fft;
    static constexpr int kSmooth = 4; // raggio (bin) dello smussamento in frequenza dei guadagni
    std::vector<double> win, inL, inR, olaL, olaR, re, im, xlr, xli, xrr, xri, mag, prefix, gain, tgt, dil, smoothK;
    std::vector<int> halfW, dLo, dHi;
    std::vector<double> dFrac;
    double magNorm = 1.0, enbw = 1.2337, olaScale = 1.0;
    long long t = 0;
    long long frameCount = 0;
    double relS = 0.9, atkS = 1.0, ratioSlope = 1.0;
    std::vector<double> wBin, aDbv, lvl, lvlPrefix, thrK, offK, wRef, envPrefix, resRed, relSbin;
    bool relValid = false;
    double relMsCached = 0.0, relTiltCached = 0.0;
    double lvlCoefStatic = 0.001, lvlCoefDyn = 0.01;
    int levelFrames = 0;
    CurvePoint wCurve[kMaxCurvePoints];
    double wBalance = 0.0;
    bool wCurveValid = false;

    int lookahead = 96, delay = 103, dpos = 0;
    SlidingMin smin;
    Box box1, box2;
    TruePeak tp;
    std::vector<double> dlL, dlR;
    double gPrev = 1.0, relB = 0.999, ceilLin = 0.891, ceilDet = 0.856;

    double gainTarget = 1.0;
    double gainSm = 1.0, smoothCoef = 0.001;
};

} // namespace sl
