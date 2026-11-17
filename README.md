# Limiter Spettrale

Limiter multibanda "a FFT": ogni bin di frequenza ha il suo limiter (da 513 a 32769 bande), seguito da un brickwall true-peak.
Formati: VST3, AU, Standalone. Stereo, fino a 48 kHz (a frequenze più alte il codice gira, ma non è stato verificato).

## Controlli
- **Threshold** (−48…0 dB): livello oltre cui ogni banda viene limitata. Il livello di banda è l'ampiezza equivalente-sinusoide, quindi un seno a −20 dBFS con soglia −20 dB non viene toccato.
- **Ratio** (1:1…inf:1, default inf): rapporto di riduzione per banda sopra la soglia. Con inf è un limiter vero; con valori più bassi la soglia diventa un compressore multibanda spettrale (il brickwall al Ceiling resta sempre attivo).
- **Gain** (−12…+24 dB): guadagno dopo lo stadio spettrale e prima del brickwall.
- **Balance** (L100…R100): attenua linearmente il canale opposto (R 100% = sinistra muta). È una scelta mia: se la intendevi come balance tra bande/frequenze dimmelo.
- **Attack** (0…500 ms, default istantaneo): attacco dello stadio spettrale. Agisce per frame FFT, quindi con finestre grandi il minimo effettivo è l'hop (finestra/8). Il brickwall finale ha comunque un lookahead fisso di ~2 ms e intercetta i picchi che l'attacco lascia passare.
- **Oversampling** (off / 2x / 4x): l'intero motore gira a frequenza doppia o quadrupla, con FFT scalata (stesse bande e stessa latenza di finestra) e filtri FIR a fase lineare (+64 campioni di latenza). Costa CPU circa 2x/3-4x e RAM (4x a 32769 bande: ~50 MB). Il motore viene creato in background alla prima selezione: il cambio può richiedere un attimo.
- Extra, aggiunti da me: **Ceiling** (−12…0 dB, default −1), **Release** (10…1000 ms), **Bands** (513/1025/2049/4097/8193/16385/32769).

Bande: a 48 kHz 24.000 bande = 1 Hz per banda, che richiede una finestra di ~1 s. Con 32769 bande (FFT 65536, 0,73 Hz per banda) le copri tutte, ma la latenza è ~1,37 s; 16385 bande (FFT 32768) ≈ 0,69 s. Per questo il plugin dichiara all'host la latenza e non reagisce ai transienti rapidi con la parte spettrale (ci pensa il brickwall).

Latenza (48 kHz): 513 bande ≈ 24 ms, 2049 ≈ 88 ms (default), 8193 ≈ 344 ms, 16385 ≈ 685 ms, 32769 ≈ 1,37 s. Controlla che il tuo host compensi latenze così alte.

## Compilare senza Xcode: GitHub Actions
1. Crea un repo GitHub (privato va bene) e carica tutto il contenuto di questa cartella.
2. Tab **Actions** → *Build macOS* → **Run workflow**.
3. A fine build scarica l'artifact `LimiterSpettrale-macOS` e decomprimilo.

## Installare
- VST3 → `~/Library/Audio/Plug-Ins/VST3/`
- AU (.component) → `~/Library/Audio/Plug-Ins/Components/`

Se macOS lo blocca: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Limiter Spettrale.vst3"` (idem per il .component). Per Logic riavvia e, se serve, rilancia la validazione AU.

## Build locale (non verificata)
Servono Command Line Tools (`xcode-select --install`) e cmake: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.

## Test del motore DSP
`g++ -O2 -std=c++17 -o engine_cli tests/engine_cli.cpp && python3 tests/run_tests.py ./engine_cli /tmp/sl` (numpy + scipy).

## Licenza
JUCE è AGPLv3 o commerciale: se distribuisci il plugin devi rispettare una delle due.
