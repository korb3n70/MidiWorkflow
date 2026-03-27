# 🎹 Ultimate MIDI2SID Workflow (Multi-SID Edition)

Un toolchain completo, moderno e parametrizzabile per convertire file MIDI standard in brani musicali nativi per Commodore 64 (`.sid`). 

Questo progetto permette di superare i limiti storici dell'hardware originale, scalando dinamicamente la polifonia da un singolo chip SID (3 voci) fino a configurazioni estreme con **4 chip SID simultanei (12 voci)**, ideali per hardware moderno come l'**Ultimate 64**.

---

## 🚀 Le Novità del Fork (Rispetto al Codice Originale)

Il progetto originale forniva un'ottima base per il parsing degli eventi MIDI, ma era limitato a un'esecuzione standard a 3 voci e richiedeva interventi manuali nel codice per l'adattamento. Questo fork trasforma il codice in una vera e propria **suite di produzione musicale per PC**:

* **Supporto Multi-SID (Fino a 12 Voci):** Il motore di allocazione dinamica delle voci (Voice Stealing) scala automaticamente per distribuire le note su 1, 2 o 4 chip SID indipendenti.
* **Indirizzamento Dinamico (Assembly Generativo):** Il convertitore C non crea solo i dati delle note, ma genera codice Assembly e direttive di configurazione "al volo" per istruire il processore 6502 su quali indirizzi hardware usare (es. `$D400`, `$D420`, `$D500`, `$D600`).
* **Header PSID v2 Automatico:** Aggiunta la generazione di un header standard compatibile con tutti i player moderni (JSidPlay2, VSID, Sidplayfp), inclusa l'attivazione del flag per il Dual SID e l'inserimento dei metadati (Titolo, Autore).
* **Gestione Hardware Completa:** Ripristinata l'impostazione hardware vitale del *Pulse Width* (Duty Cycle) per permettere la corretta riproduzione delle onde quadre, e ottimizzato l'indirizzamento Indiretto del 6502 per azzerare la latenza tra i vari chip.

---

## 🛠️ Il Workflow (Come Funziona)

Il processo di generazione si divide in due step principali che collegano il mondo moderno (PC/C11) al mondo retro (Assembly 6502):

1.  **Parsing & Routing (Il Tool in C):** Il programma `midi2sid` legge il file `.mid`, interpreta le note, le assegna ai canali hardware disponibili e sputa fuori due file di testo contenenti costanti Assembly e byte compressi.
2.  **Assembling (Il Motore 6502):** Un compilatore incrociato (ACME) prende un motore di riproduzione scritto in Assembly puro (`player.asm`), include i dati appena generati dal tool in C, e "cuoce" il file `.sid` finale pronto da ascoltare.

### Requisiti di Sistema
* Compilatore C standard (es. **GCC** via MSYS2/MinGW su Windows, o nativo su Linux/macOS).
* Assemblatore 6502 **ACME** (versione 0.97 o superiore).

---

## 📖 Guida all'Uso (Comandi)

### 1. Compilare il Convertitore C (Solo la prima volta)
Nel terminale, naviga nella cartella del progetto e compila l'eseguibile:
bash
gcc converter.c -o midi2sid.exe

## 2. Generare i Dati dal File MIDI
Il tool accetta parametri da riga di comando per personalizzare l'hardware di destinazione. Per visualizzare la guida completa, lancia ./midi2sid.exe -h.

Per un C64 classico (1 SID, 3 Voci):
Bash
./midi2sid.exe brano.mid

Per un Dual SID (6 Voci):
```Bash
./midi2sid.exe brano.mid 2
```
Per l'Ultimate 64 (4 SID, 12 Voci, con indirizzi custom):
```Bash
./midi2sid.exe brano.mid 4 D400 D420 D500 D600
```
3. Assemblare il File .SID Finale
Una volta generati i dati, dai in pasto il motore audio ad ACME:

```Bash
acme -f plain -o brano.sid player.asm
```
Fatto! Il file brano.sid è pronto per essere riprodotto.

---

# 📂 Cosa Viene Generato?
Dopo l'esecuzione del workflow, troverai questi file nella tua cartella:

* song_config.asm: Creato dal tool C, contiene le direttive per l'Assembler. Dice ad ACME quanti SID compilare e su quali indirizzi di memoria "saldare" i collegamenti.

* song_data.asm: Creato dal tool C, è un enorme array di byte (!byte $xx) che rappresenta la timeline compressa di note On/Off e i salti di frame.

* brano.sid: Il prodotto finale. Un file eseguibile per processori MOS 6502 comprensivo di player interno e dati musicali.

---

# 🔮 Roadmap & Aree di Miglioramento
Attualmente il tool funziona interamente da riga di comando (CLI). Le future evoluzioni del progetto mirano a trasformarlo in una vera e propria Digital Audio Workstation (DAW) dedicata al chip SID:

*  Interfaccia Grafica (GUI): Abbandonare la CLI in favore di un'applicazione visiva (es. scritta in C++ con ImGui o in Python/Tkinter) per una gestione più rapida e user-friendly del workflow.

*   Gestione Strumenti (Patching): Implementare un pannello per mappare specifici canali MIDI a specifiche forme d'onda del SID (Triangolo, Sega, Quadra, Rumore) e personalizzare l'inviluppo ADSR per ogni voce.

* Visualizzatore di Tracce (Piano Roll): Una finestra di anteprima per visualizzare le note MIDI e controllare in tempo reale come il "Voice Stealer" le distribuisce sui chip fisici (utile per debuggare accordi tagliati).

* Playback Integrato: Integrare una libreria leggera di emulazione SID (come libsidplayfp o reSID) direttamente all'interno dell'interfaccia, per ascoltare la preview del brano generato senza dover aprire emulatori o player esterni.

* Esportazione in PRG: Aggiungere la possibilità di generare non solo file PSID, ma eseguibili nativi .prg con un piccolo player grafico da caricare direttamente sul Commodore 64 reale.
