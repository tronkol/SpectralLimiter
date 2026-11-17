// engine_cli: passa un file raw float32 stereo interleaved dal motore e scrive il risultato.
// uso: engine_cli in.f32 out.f32 fs fft thr gain bal ceil rel
// stampa su stdout: latency=<campioni> cpu_x_realtime=<fattore>
#include "../Source/Oversampler.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 10)
    {
        std::fprintf (stderr, "uso: %s in out fs fft thr gain bal ceil rel\n", argv[0]);
        return 1;
    }
    const double fs = std::atof (argv[3]);
    const int fftSize = std::atoi (argv[4]);
    sl::Engine::Params p;
    p.thresholdDb = std::atof (argv[5]);
    p.gainDb = std::atof (argv[6]);
    p.balance = std::atof (argv[7]);
    p.ceilingDb = std::atof (argv[8]);
    p.releaseMs = std::atof (argv[9]);
    if (argc > 10) p.attackMs = std::atof (argv[10]);
    if (argc > 11) p.ratio = std::atof (argv[11]);
    const int os = argc > 12 ? std::atoi (argv[12]) : 1;

    FILE* f = std::fopen (argv[1], "rb");
    if (! f) { std::fprintf (stderr, "input non trovato\n"); return 1; }
    std::vector<float> data;
    float buf[4096];
    size_t n;
    while ((n = std::fread (buf, sizeof (float), 4096, f)) > 0) data.insert (data.end(), buf, buf + n);
    std::fclose (f);

    const size_t frames = data.size() / 2;
    std::vector<float> l (frames), r (frames);
    for (size_t i = 0; i < frames; ++i) { l[i] = data[2 * i]; r[i] = data[2 * i + 1]; }

    sl::OsEngine eng;
    eng.prepare (fs, fftSize, os);
    eng.setParams (p);
    eng.reset();

    const auto t0 = std::chrono::steady_clock::now();
    const size_t block = 480; // volutamente non potenza di due
    for (size_t pos = 0; pos < frames; pos += block)
    {
        const int cnt = (int) std::min (block, frames - pos);
        eng.process (l.data() + pos, r.data() + pos, cnt);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double cpu = std::chrono::duration<double> (t1 - t0).count();

    for (size_t i = 0; i < frames; ++i) { data[2 * i] = l[i]; data[2 * i + 1] = r[i]; }
    f = std::fopen (argv[2], "wb");
    std::fwrite (data.data(), sizeof (float), data.size(), f);
    std::fclose (f);

    std::printf ("latency=%d cpu_x_realtime=%.5f\n", eng.latencySamples(), cpu / ((double) frames / fs));
    return 0;
}
