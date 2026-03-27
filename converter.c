#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 50000
#define MAX_TEMPO_EVENTS 1024
#define EVENT_TIME_MAX 0x00ffffffUL
#define CMD_NOTE_OFF(ch) ((uint8_t)(0x80 | ((ch) & 0x0f)))
#define CMD_NOTE_ON(ch) ((uint8_t)(0x90 | ((ch) & 0x0f)))
#define FRAME_US 20000UL

typedef struct event_time24 { uint8_t lo, mid, hi; } event_time24;
typedef struct voice_state { bool active; uint8_t note; uint8_t channel; uint32_t age; } voice_state;

static event_time24 event_time[MAX_EVENTS];
static uint8_t  event_cmd[MAX_EVENTS];
static uint8_t  event_note[MAX_EVENTS];
static uint32_t event_count = 0;

static uint32_t tempo_tick[MAX_TEMPO_EVENTS];
static uint32_t tempo_us_value[MAX_TEMPO_EVENTS];
static uint32_t tempo_count = 0;

static uint32_t frame_denom = 0;
static uint16_t division = 0;
static bool     event_overflow = false;

static voice_state voices[12];
static uint32_t voice_age = 1;

// Variabili per la parametrizzazione
static int num_sids = 1;
static int num_voices = 3;
static uint16_t sid_addrs[4] = {0xD400, 0xD420, 0xD500, 0xD600}; // Default

/* --- Schermata di Aiuto --- */
void print_help(const char* prog_name) {
    printf("==================================================\n");
    printf(" MIDI to SID Converter - Ultimate Edition\n");
    printf("==================================================\n\n");
    printf("USO:\n");
    printf("  %s <file.mid> [num_sids] [addr1] [addr2] [addr3] [addr4]\n\n", prog_name);
    printf("PARAMETRI:\n");
    printf("  <file.mid> : Il file MIDI da convertire (obbligatorio)\n");
    printf("  [num_sids] : Numero di chip SID da usare: 1, 2 o 4 (default: 1)\n");
    printf("  [addr1]..  : Indirizzi base dei SID in esadecimale (default: D400 D420 D500 D600)\n\n");
    printf("ESEMPI:\n");
    printf("  %s brano.mid                   -> 1 SID  (3 voci)  su D400\n", prog_name);
    printf("  %s brano.mid 2                 -> 2 SIDs (6 voci)  su D400, D420\n", prog_name);
    printf("  %s brano.mid 2 D400 DE00       -> 2 SIDs (6 voci)  forzati su D400 e DE00\n", prog_name);
    printf("  %s brano.mid 4                 -> 4 SIDs (12 voci) su D400, D420, D500, D600\n\n", prog_name);
}

/* --- I/O File e Parsing (invariati) --- */
static bool file_read_u8(FILE * fp, uint8_t * out) { return fread(out, 1, 1, fp) == 1; }
static bool file_read_be16(FILE * fp, uint16_t * out) { uint8_t hi, lo; if (!file_read_u8(fp, &hi) || !file_read_u8(fp, &lo)) return false; *out = ((uint16_t)hi << 8) | lo; return true; }
static bool file_read_be32(FILE * fp, uint32_t * out) { uint8_t b[4]; if (fread(b, 1, 4, fp) != 4) return false; *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; return true; }
static bool file_skip_bytes(FILE * fp, uint32_t count) { return fseek(fp, count, SEEK_CUR) == 0; }
static bool file_read_chunk_id(FILE * fp, char id[4]) { return fread(id, 1, 4, fp) == 4; }
static uint32_t event_time_get(const event_time24 * t) { return (uint32_t)t->lo | ((uint32_t)t->mid << 8) | ((uint32_t)t->hi << 16); }
static bool event_time_set(event_time24 * t, uint32_t v) { if (v > EVENT_TIME_MAX) return false; t->lo = (uint8_t)(v & 0xff); t->mid = (uint8_t)((v >> 8) & 0xff); t->hi = (uint8_t)((v >> 16) & 0xff); return true; }
static bool push_note_event(uint32_t tick, uint8_t cmd, uint8_t note) { if (event_count >= MAX_EVENTS) { event_overflow = true; return false; } if (!event_time_set(&event_time[event_count], tick)) { event_overflow = true; return false; } event_cmd[event_count] = cmd; event_note[event_count] = note; event_count++; return true; }
static bool event_note_is_usable(uint8_t channel, uint8_t note) { return channel != 9 && note >= 24 && note <= 96; }
static bool push_tempo_event(uint32_t tick, uint32_t tempo_us) { if (tempo_count >= MAX_TEMPO_EVENTS) return false; if (tempo_us == 0) tempo_us = 500000UL; tempo_tick[tempo_count] = tick; tempo_us_value[tempo_count] = tempo_us; tempo_count++; return true; }
static bool track_read_u8(FILE * fp, uint32_t * remaining, uint8_t * out) { if (*remaining == 0) return false; if (!file_read_u8(fp, out)) return false; (*remaining)--; return true; }
static bool track_skip_bytes(FILE * fp, uint32_t * remaining, uint32_t count) { if (count > *remaining) return false; file_skip_bytes(fp, count); *remaining -= count; return true; }
static bool track_read_varlen(FILE * fp, uint32_t * remaining, uint32_t * out) { uint32_t v = 0; uint8_t b; uint8_t i = 0; do { if (i >= 4) return false; if (!track_read_u8(fp, remaining, &b)) return false; v = (v << 7) | (b & 0x7f); i++; } while (b & 0x80); *out = v; return true; }
static bool parse_track_events(FILE * fp, uint32_t track_len, uint16_t * note_on_count) {
    uint32_t remaining = track_len; uint8_t running_status = 0; uint32_t track_tick = 0; *note_on_count = 0;
    while (remaining > 0) {
        uint32_t delta; uint8_t first, status, data1 = 0, data2 = 0; bool have_data1 = false;
        if (!track_read_varlen(fp, &remaining, &delta)) return false; track_tick += delta;
        if (!track_read_u8(fp, &remaining, &first)) return false;
        if (first & 0x80) { status = first; } else { if (!running_status) return false; status = running_status; data1 = first; have_data1 = true; }
        if (status == 0xff) {
            uint8_t meta_type; uint32_t meta_len; running_status = 0;
            if (!track_read_u8(fp, &remaining, &meta_type) || !track_read_varlen(fp, &remaining, &meta_len)) return false;
            if (meta_type == 0x2f) { track_skip_bytes(fp, &remaining, meta_len); break; }
            else if (meta_type == 0x51 && meta_len == 3) { uint8_t t0, t1, t2; if (track_read_u8(fp, &remaining, &t0) && track_read_u8(fp, &remaining, &t1) && track_read_u8(fp, &remaining, &t2)) push_tempo_event(track_tick, ((uint32_t)t0 << 16) | ((uint32_t)t1 << 8) | t2); }
            else { track_skip_bytes(fp, &remaining, meta_len); } continue;
        }
        if (status >= 0xf0) { if (status == 0xf0 || status == 0xf7) { uint32_t syx_len; running_status = 0; if (track_read_varlen(fp, &remaining, &syx_len)) track_skip_bytes(fp, &remaining, syx_len); continue; } return false; }
        uint8_t msg = status & 0xf0; uint8_t ch = status & 0x0f; bool need_two = !(msg == 0xc0 || msg == 0xd0);
        if (!have_data1) { if (!track_read_u8(fp, &remaining, &data1)) return false; }
        if (need_two) { if (!track_read_u8(fp, &remaining, &data2)) return false; }
        running_status = status;
        if (msg == 0x90) { if (data2 == 0) { if (event_note_is_usable(ch, data1)) push_note_event(track_tick, CMD_NOTE_OFF(ch), data1); } else { if (event_note_is_usable(ch, data1)) { (*note_on_count)++; push_note_event(track_tick, CMD_NOTE_ON(ch), data1); } } } 
        else if (msg == 0x80) { if (event_note_is_usable(ch, data1)) push_note_event(track_tick, CMD_NOTE_OFF(ch), data1); }
    }
    if (remaining > 0) track_skip_bytes(fp, &remaining, remaining); return true;
}
static bool note_event_before(const event_time24 * ta, uint8_t ca, const event_time24 * tb, uint8_t cb) {
    if (ta->hi != tb->hi) return ta->hi < tb->hi; if (ta->mid != tb->mid) return ta->mid < tb->mid; if (ta->lo != tb->lo) return ta->lo < tb->lo;
    uint8_t ma = ca & 0xf0; uint8_t mb = cb & 0xf0; if (ma != mb) { if (ma == 0x80) return true; if (mb == 0x80) return false; } return ca < cb;
}
static void sort_note_events(void) {
    uint16_t gap, i, j;
    for (gap = event_count >> 1; gap > 0; gap >>= 1) {
        for (i = gap; i < event_count; ++i) {
            event_time24 tt = event_time[i]; uint8_t tc = event_cmd[i], tn = event_note[i]; j = i;
            while (j >= gap && note_event_before(&tt, tc, &event_time[j - gap], event_cmd[j - gap])) { event_time[j] = event_time[j - gap]; event_cmd[j] = event_cmd[j - gap]; event_note[j] = event_note[j - gap]; j -= gap; }
            event_time[j] = tt; event_cmd[j] = tc; event_note[j] = tn;
        }
    }
}
static void sort_tempo_events(void) {
    uint8_t gap, i, j;
    for (gap = tempo_count >> 1; gap > 0; gap >>= 1) {
        for (i = gap; i < tempo_count; ++i) {
            uint32_t tt = tempo_tick[i], tv = tempo_us_value[i]; j = i;
            while (j >= gap && tt < tempo_tick[j - gap]) { tempo_tick[j] = tempo_tick[j - gap]; tempo_us_value[j] = tempo_us_value[j - gap]; j -= gap; }
            tempo_tick[j] = tt; tempo_us_value[j] = tv;
        }
    }
}
static void advance_frames_by_ticks(uint32_t delta_ticks, uint32_t tempo_us, uint32_t * cur_frame, uint32_t * frame_numer) {
    while (delta_ticks > 0) { uint16_t step = (delta_ticks > 255UL) ? 255 : (uint16_t)delta_ticks; delta_ticks -= step; *frame_numer += (uint32_t)step * tempo_us; while (*frame_numer >= frame_denom) { *frame_numer -= frame_denom; (*cur_frame)++; } }
}
static void convert_ticks_to_frames(void) {
    uint32_t cur_tick = 0, cur_frame = 0, frame_numer = 0, tempo_us = 500000UL; uint8_t ti = 0;
    while (ti < tempo_count && tempo_tick[ti] == 0) { tempo_us = tempo_us_value[ti]; ti++; }
    for (uint16_t i = 0; i < event_count; ++i) {
        uint32_t target_tick = event_time_get(&event_time[i]);
        while (ti < tempo_count && tempo_tick[ti] <= target_tick) { advance_frames_by_ticks(tempo_tick[ti] - cur_tick, tempo_us, &cur_frame, &frame_numer); cur_tick = tempo_tick[ti]; tempo_us = tempo_us_value[ti]; ti++; }
        advance_frames_by_ticks(target_tick - cur_tick, tempo_us, &cur_frame, &frame_numer); cur_tick = target_tick;
        if (!event_time_set(&event_time[i], cur_frame)) { event_count = i; event_overflow = true; break; }
    }
    if (event_count > 0 && event_time_get(&event_time[0]) > 0) { uint32_t base = event_time_get(&event_time[0]); for (uint16_t i = 0; i < event_count; ++i) event_time_set(&event_time[i], event_time_get(&event_time[i]) - base); }
}
static bool load_and_convert_midi(const char * filename) {
    FILE * fp = fopen(filename, "rb"); if (!fp) { printf("Errore: Impossibile aprire %s\n", filename); return false; }
    char chunk_id[4]; uint32_t chunk_len; uint16_t format, ntracks, file_division;
    if (!file_read_chunk_id(fp, chunk_id) || memcmp(chunk_id, "MThd", 4) != 0) { fclose(fp); return false; }
    if (!file_read_be32(fp, &chunk_len) || !file_read_be16(fp, &format) || !file_read_be16(fp, &ntracks) || !file_read_be16(fp, &file_division)) { fclose(fp); return false; }
    if (chunk_len > 6) file_skip_bytes(fp, chunk_len - 6);
    division = file_division; frame_denom = (uint32_t)division * FRAME_US;
    uint16_t parsed_tracks = 0, note_on_count = 0;
    for (uint16_t i = 0; i < ntracks; ++i) {
        if (!file_read_chunk_id(fp, chunk_id) || !file_read_be32(fp, &chunk_len)) break;
        if (memcmp(chunk_id, "MTrk", 4) != 0) { file_skip_bytes(fp, chunk_len); continue; }
        parse_track_events(fp, chunk_len, &note_on_count); parsed_tracks++;
    }
    fclose(fp);
    if (!parsed_tracks || !event_count) return false;
    sort_note_events(); sort_tempo_events(); convert_ticks_to_frames(); return true;
}

static int allocate_voice(uint8_t note, uint8_t channel) {
    for (int i=0; i<num_voices; i++) if (!voices[i].active) return i;
    for (int i=0; i<num_voices; i++) if (voices[i].channel == channel) return i;
    int oldest = 0; uint32_t min_age = voices[0].age;
    for (int i=1; i<num_voices; i++) { if (voices[i].age < min_age) { min_age = voices[i].age; oldest = i; } }
    return oldest;
}
static int find_voice_playing(uint8_t note) {
    for (int i=0; i<num_voices; i++) if (voices[i].active && voices[i].note == note) return i;
    return -1;
}

void generate_asm_output() {
    // 1. GENERIAMO IL FILE DI CONFIGURAZIONE PER ACME
    FILE* fcfg = fopen("song_config.asm", "w");
    fprintf(fcfg, "; --- Configurazione Dinamica Hardware --- \n");
    fprintf(fcfg, "NUM_SIDS = %d\n", num_sids);
    
    // Esporta le costanti di base per il file Assembly
    for(int i = 0; i < 4; i++) {
        fprintf(fcfg, "SID%d_BASE = $%04X\n", i+1, sid_addrs[i]);
    }
    
    // Calcola e esporta i Byte Bassi e Alti per l'indirizzamento dinamico delle 12 voci
    fprintf(fcfg, "voice_base_lo: !byte ");
    for(int i=0; i<num_voices; i++) {
        int sid_idx = i / 3;
        int voice_offset = (i % 3) * 7;
        uint8_t low_byte = (sid_addrs[sid_idx] + voice_offset) & 0xFF;
        fprintf(fcfg, "$%02X%s", low_byte, i==num_voices-1 ? "\n" : ", ");
    }
    
    fprintf(fcfg, "voice_base_hi: !byte ");
    for(int i=0; i<num_voices; i++) {
        int sid_idx = i / 3;
        int voice_offset = (i % 3) * 7;
        uint8_t high_byte = ((sid_addrs[sid_idx] + voice_offset) >> 8) & 0xFF;
        fprintf(fcfg, "$%02X%s", high_byte, i==num_voices-1 ? "\n" : ", ");
    }
    
    // Assegna onde (Quadra e Triangolo)
    fprintf(fcfg, "voice_waves: !byte ");
    for(int i=0; i<num_voices; i++) {
        uint8_t wave = (i % 3 == 1) ? 0x10 : 0x40;
        fprintf(fcfg, "$%02X%s", wave, i==num_voices-1 ? "\n" : ", ");
    }
    fclose(fcfg);

    // 2. GENERIAMO I DATI DELLA CANZONE
    FILE* fout = fopen("song_data.asm", "w");
    fprintf(fout, "song_data:\n");

    uint32_t last_out_frame = 0;
    for (int i = 0; i < num_voices; i++) { voices[i].active = false; voices[i].age = 0; }

    for (uint32_t i = 0; i < event_count; ++i) {
        uint32_t frame = event_time_get(&event_time[i]);
        uint32_t delta = frame - last_out_frame;
        while (delta > 0) {
            uint8_t d = (delta > 127) ? 127 : delta;
            fprintf(fout, "    !byte $%02X\n", d);
            delta -= d;
        }
        last_out_frame = frame;

        uint8_t cmd = event_cmd[i] & 0xF0;
        uint8_t ch = event_cmd[i] & 0x0F;
        uint8_t note = event_note[i];

        if (cmd == 0x90) {
            int v = allocate_voice(note, ch);
            fprintf(fout, "    !byte $%02X, $%02X ; ON Voce %d, Nota %d\n", 0x90 + v, note, v+1, note);
            voices[v].active = true; voices[v].note = note; voices[v].channel = ch; voices[v].age = voice_age++;
        } else if (cmd == 0x80) {
            int v = find_voice_playing(note);
            if (v >= 0) {
                fprintf(fout, "    !byte $%02X       ; OFF Voce %d, Nota %d\n", 0x80 + v, v+1, note);
                voices[v].active = false;
            }
        }
    }
    fprintf(fout, "    !byte $FF ; Fine brano\n");
    fclose(fout);
    
    printf("\n>>> Configurazione esportata in song_config.asm\n");
    printf(">>> Dati esportati in song_data.asm\n");
    printf(">>> Compilazione per %d SID (%d voci) completata!\n", num_sids, num_voices);
}

int main(int argc, char** argv) {
    // Controllo Help
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }
    
    // Parsing Numero SID (1, 2 o 4)
    if (argc >= 3) {
        num_sids = atoi(argv[2]);
        if (num_sids != 1 && num_sids != 2 && num_sids != 4) {
            printf("Attenzione: Numero SID non supportato (%d). Fallback a 1 SID.\n", num_sids);
            num_sids = 1;
        }
    }
    num_voices = num_sids * 3;

    // Parsing Indirizzi Personalizzati
    for (int i = 0; i < num_sids && (3 + i) < argc; i++) {
        sid_addrs[i] = (uint16_t)strtol(argv[3 + i], NULL, 16);
    }

    printf("Elaborazione file: %s...\n", argv[1]);
    printf("Configurazione: %d SID attivi (Indirizzi: ", num_sids);
    for(int i=0; i<num_sids; i++) printf("$%04X ", sid_addrs[i]);
    printf(")\n");

    if (!load_and_convert_midi(argv[1])) {
        printf("Errore: Il file non e' un MIDI valido o non contiene tracce compatibili.\n");
        return 1;
    }
    generate_asm_output();
    return 0;
}