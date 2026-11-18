# CONTROLUNA SAUCE

Limiter multibanda "a FFT": ogni bin di frequenza ha il suo limiter (da 513 a 32769 bande), seguito da un brickwall true-peak.
Formati: VST3, AU, Standalone. Stereo, fino a 48 kHz (a frequenze più alte il codice gira, ma non è stato verificato).

## Controlli
- **Threshold** (−60…0 dB): livello oltre cui ogni banda viene limitata. Il livello di banda è l'ampiezza equivalente-sinusoide, quindi un seno a −20 dBFS con soglia −20 dB non viene toccato.
- **Ratio** (1:1…inf:1, default inf): rapporto di riduzione per banda sopra la soglia. Con inf è un limiter vero; con valori più bassi la soglia diventa un compressore multibanda spettrale (il brickwall al Ceiling resta sempre attivo).
- **Compensation** (0…100%) e tasto **Dynamic**: compensazione spettrale automatica, banda per banda. Il motore misura il livello medio (in dB) di ogni banda e lo confronta con il livello medio dello spettro; la soglia di ogni banda si sposta di conseguenza. Così le bande naturalmente più forti (i bassi di un mix) non lavorano più delle altre: a 0% la soglia è la stessa per tutte le bande, a 100% la riduzione è praticamente uguale su tutto lo spettro (misurato su rumore rosa: bassi e alti 20,6 / 10,2 dB a 0%, 15,6 / 13,2 dB a 100%). Con Dynamic spento il profilo è lento (~8 s): è la forma tonale del brano. Con Dynamic acceso ogni banda segue il proprio livello recente (~0,25 s): il limiter lavora sui picchi di ogni banda rispetto al suo livello e lascia passare più a lungo i passaggi sostenuti. La linea tratteggiata sul display è la soglia effettiva di ogni banda.
- **Ratio** parte da 1:1 e **Balance/Compensation/curva** partono piatti: all'apertura lo stadio spettrale non fa nulla (resta solo il brickwall sul Ceiling). Alza il Ratio per attivare l'intervento.
- **Resonance** (0…100%, sperimentale): sopprime i picchi stretti che sporgono più di 3 dB sopra l'inviluppo locale dello spettro (±25% di frequenza), tipo tamer di risonanze. Rispetta la curva di focus. Misurato: un picco a 2 kHz scende di 6,4 dB al 50% e 12,7 dB al 100%, il resto dello spettro cambia meno di 1 dB.
- **Release Tilt** (−100…+100%, sperimentale): rilascio dipendente dalla frequenza. Positivo = bassi lenti e alti veloci (fino a 8× più lento / 8× più veloce del Release), negativo il contrario.
- **LEARN** (tasto, sperimentale): la riduzione di ogni banda si accumula e non viene mai rilasciata. Suona la parte più forte del brano con LEARN acceso: il limiter si trasforma in un EQ spettrale statico che ricorda la riduzione massima. Spegni il tasto per tornare al rilascio normale.
- **Range** (0…60 dB, default 60): riduzione massima per banda dello stadio spettrale.
- **Curva di focus** (stile Soothe, sul display): fino a 6 punti che pesano quanto il limiter interviene in ogni zona. Doppio click su un punto vuoto del display = aggiungi un punto (Peak); trascina per frequenza e guadagno; rotella = Q; tasto destro = cambia tipo (Low cut, Low shelf, Peak, High shelf, High cut) o elimina; doppio click su un punto = elimina. La riga a 0 dB è l'intervento pieno; sotto lo 0 il limiter interviene meno in quella zona (−24 dB = quasi niente), sopra lo 0 interviene di più (fino a +6 dB = doppia riduzione). I cut sono a 12 dB/ott. Il brickwall finale sul Ceiling resta sempre attivo su tutto.
- **Gain** (−12…+24 dB): guadagno dopo lo stadio spettrale e prima del brickwall.
- **Balance** (bassi ← 0 → alti): sposta in modo continuo dove interviene il limiter. A 0 l'intervento è uniforme; verso −100% si concentra sui bassi (sopra ~8 kHz non interviene più), verso +100% sugli alti (sotto ~200 Hz non interviene più). In mezzo la transizione è graduale.
- **Attack** (0…500 ms, default istantaneo): attacco dello stadio spettrale. Agisce per frame FFT, quindi con finestre grandi il minimo effettivo è l'hop (finestra/8). Il brickwall finale ha comunque un lookahead fisso di ~2 ms e intercetta i picchi che l'attacco lascia passare.
- **Oversampling** (off / 2x / 4x): l'intero motore gira a frequenza doppia o quadrupla, con FFT scalata (stesse bande e stessa latenza di finestra) e filtri FIR a fase lineare (+64 campioni di latenza). Costa CPU circa 2x/3-4x e RAM (4x a 32769 bande: ~50 MB). Il motore viene creato in background alla prima selezione: il cambio può richiedere un attimo.
- Extra, aggiunti da me: **Ceiling** (−12…0 dB, default −1), **Release** (10…1000 ms), **Bands** (513/1025/2049/4097/8193/16385/32769).

Bande: a 48 kHz 24.000 bande = 1 Hz per banda, che richiede una finestra di ~1 s. Con 32769 bande (FFT 65536, 0,73 Hz per banda) le copri tutte, ma la latenza è ~1,37 s; 16385 bande (FFT 32768) ≈ 0,69 s. Per questo il plugin dichiara all'host la latenza e non reagisce ai transienti rapidi con la parte spettrale (ci pensa il brickwall).

Latenza (48 kHz): 513 bande ≈ 24 ms, 2049 ≈ 88 ms (default), 8193 ≈ 344 ms, 16385 ≈ 685 ms, 32769 ≈ 1,37 s. Controlla che il tuo host compensi latenze così alte.

## Compilare senza Xcode: GitHub Actions
1. Crea un repo GitHub (privato va bene) e carica tutto il contenuto di questa cartella.
2. Tab **Actions** → *Build macOS* → **Run workflow**.
3. A fine build scarica l'artifact `LimiterSpettrale-macOS` e decomprimilo.

## GUI
La finestra si ridimensiona trascinando l'angolo in basso a destra (mantiene le proporzioni). Interfaccia in inglese. Tema chiaro attenuato: sfondo grigio caldo, manopole nere a forma di squircle (stessa geometria di ULTRAMEGASUPERMAXICHORUS), accenti verdi. Il titolo è in Helvetica Bold.

## Installare
Il plugin ora si chiama CONTROLUNA SAUCE: elimina il vecchio `Limiter Spettrale.component` / `.vst3` dalle cartelle dei plugin, altrimenti Logic ti mostra due versioni.
- VST3 → `~/Library/Audio/Plug-Ins/VST3/`
- AU (.component) → `~/Library/Audio/Plug-Ins/Components/`

Se macOS lo blocca: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"CONTROLUNA SAUCE.vst3"` (idem per il .component). Per Logic riavvia e, se serve, rilancia la validazione AU.

## Build locale (non verificata)
Servono Command Line Tools (`xcode-select --install`) e cmake: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.

## Test del motore DSP
`g++ -O2 -std=c++17 -o engine_cli tests/engine_cli.cpp && python3 tests/run_tests.py ./engine_cli /tmp/sl` (numpy + scipy).

## Licenza
JUCE è AGPLv3 o commerciale: se distribuisci il plugin devi rispettare una delle due.
