!cpu 6502

; ===========================================================================
; 1. HEADER PSID v2
; ===========================================================================
* = $0000 
!byte $50, $53, $49, $44 ; "PSID" (Magic string)
!byte $00, $02           ; Versione 2 (Big Endian)
!byte $00, $7C           ; Data offset (124 byte di header)
!byte $10, $00           ; Load address ($1000 in Big Endian)
!byte >init_player, <init_player 
!byte >play_frame, <play_frame   
!byte $00, $01           ; Numero di canzoni
!byte $00, $01           ; Canzone di partenza
!byte $00, $00, $00, $00 ; Speed (0 = 50Hz)

; --- METADATI ---
str_title: !text "ULTIMATE SINFONIA"
!fill 32 - (* - str_title), 0

str_author: !text "SID MAESTRO"
!fill 32 - (* - str_author), 0

str_copy: !text "2026 ETICHETTA"
!fill 32 - (* - str_copy), 0
; ----------------

!byte $00, $00           ; Flags
!byte $00                ; Start page
!byte $00                ; Page length
!byte $00, $00           ; Il player del PC usera' le impostazioni emulatore

; ===========================================================================
; 2. IL PLAYER 6502 UNIVERSALE (Compilato per l'indirizzo $1000)
; ===========================================================================
!pseudopc $1000 {

        ; Salto obbligatorio (i file inclusi non devono essere eseguiti)
        jmp init_player 
        
        ; -------------------------------------------------------------
        ; IMPORTA LE COSTANTI MAGICHE GENERATE DAL C (Es. SID1_BASE)
        ; -------------------------------------------------------------
        !source "song_config.asm"

        stream_ptr = $FC         
        delay_cnt  = $FE         
        sid_ptr    = $FA         

init_player:
        ; PULIZIA DINAMICA (Azzera i registri solo dei SID attivi)
        ldx #0
        txa
.clr_sid:
        !if NUM_SIDS >= 1 { sta SID1_BASE,x }
        !if NUM_SIDS >= 2 { sta SID2_BASE,x }
        !if NUM_SIDS >= 4 { sta SID3_BASE,x : sta SID4_BASE,x }
        inx
        cpx #25
        bne .clr_sid

        ; INIZIALIZZAZIONE ADSR E PULSE WIDTH
        !if NUM_SIDS >= 1 {
                lda #$0F : sta SID1_BASE+24
                lda #$08 : sta SID1_BASE+3 : sta SID1_BASE+10 : sta SID1_BASE+17
                lda #$08 : sta SID1_BASE+5 : sta SID1_BASE+12 : sta SID1_BASE+19
                lda #$A8 : sta SID1_BASE+6 : sta SID1_BASE+13 : sta SID1_BASE+20
        }
        !if NUM_SIDS >= 2 {
                lda #$0F : sta SID2_BASE+24
                lda #$08 : sta SID2_BASE+3 : sta SID2_BASE+10 : sta SID2_BASE+17
                lda #$08 : sta SID2_BASE+5 : sta SID2_BASE+12 : sta SID2_BASE+19
                lda #$A8 : sta SID2_BASE+6 : sta SID2_BASE+13 : sta SID2_BASE+20
        }
        !if NUM_SIDS >= 4 {
                lda #$0F : sta SID3_BASE+24 : sta SID4_BASE+24
                lda #$08 : sta SID3_BASE+3 : sta SID3_BASE+10 : sta SID3_BASE+17
                           sta SID4_BASE+3 : sta SID4_BASE+10 : sta SID4_BASE+17
                lda #$08 : sta SID3_BASE+5 : sta SID3_BASE+12 : sta SID3_BASE+19
                           sta SID4_BASE+5 : sta SID4_BASE+12 : sta SID4_BASE+19
                lda #$A8 : sta SID3_BASE+6 : sta SID3_BASE+13 : sta SID3_BASE+20
                           sta SID4_BASE+6 : sta SID4_BASE+13 : sta SID4_BASE+20
        }

        ; Inizializza puntatore
        lda #<song_data
        sta stream_ptr
        lda #>song_data
        sta stream_ptr+1
        
        lda #0
        sta delay_cnt
        rts

play_frame:
        lda delay_cnt
        beq .read_loop
        dec delay_cnt
        rts

.read_loop:
        ldy #0
        lda (stream_ptr),y
        cmp #$FF
        bne .check_delay
        rts

.check_delay:
        cmp #$80
        bcs .is_event
        sta delay_cnt
        jsr inc_ptr
        rts

.is_event:
        pha                 
        and #$0F            
        tax                 
        
        ; Magia dell'indirizzamento Indiretto: il 6502 sa esattamenta a quale chip puntare!
        lda voice_base_lo,x
        sta sid_ptr
        lda voice_base_hi,x
        sta sid_ptr+1
        
        pla                 
        pha                 
        and #$F0
        cmp #$90
        beq .do_note_on

.do_note_off:
        pla                 
        lda voice_waves,x
        ldy #4
        sta (sid_ptr),y     
        jsr inc_ptr
        jmp .read_loop

.do_note_on:
        jsr inc_ptr         
        
        ldy #0
        lda (stream_ptr),y  
        tay                 
        
        lda freq_table_lo,y
        pha
        lda freq_table_hi,y
        pha
        
        pla
        ldy #1
        sta (sid_ptr),y
        
        pla
        dey                 
        sta (sid_ptr),y
        
        pla                 
        and #$0F            
        tax                 
        
        lda voice_waves,x   
        ora #$01            
        ldy #4
        sta (sid_ptr),y     
        
        jsr inc_ptr
        jmp .read_loop

inc_ptr:
        inc stream_ptr
        bne +
        inc stream_ptr+1
+       rts

; ===========================================================================
; 3. TABELLE FREQUENZE E DATI
; ===========================================================================

freq_table_lo:
        !byte <139, <147, <156, <166, <175, <186, <197, <209
        !byte <221, <234, <248, <263, <278, <295, <313, <331
        !byte <351, <372, <394, <417, <442, <468, <496, <526
        !byte <557, <590, <625, <662, <702, <743, <788, <834
        !byte <884, <937, <992, <1051, <1114, <1180, <1250, <1325
        !byte <1403, <1487, <1575, <1669, <1768, <1873, <1985, <2103
        !byte <2228, <2360, <2500, <2649, <2807, <2973, <3150, <3338
        !byte <3536, <3746, <3969, <4205, <4455, <4720, <5001, <5298
        !byte <5613, <5947, <6300, <6675, <7072, <7493, <7938, <8410
        !byte <8910, <9440, <10001, <10596, <11226, <11894, <12601, <13350
        !byte <14144, <14985, <15876, <16820, <17820, <18880, <20003, <21192
        !byte <22452, <23787, <25202, <26700, <28288, <29970, <31752, <33640
        !byte <35641, <37760, <40005, <42384, <44904, <47574, <50403, <53401
        !byte <56576, <59940, <63504, <65535, <65535, <65535, <65535, <65535
        !byte <65535, <65535, <65535, <65535, <65535, <65535, <65535, <65535
        !byte <65535, <65535, <65535, <65535, <65535, <65535, <65535, <65535

freq_table_hi:
        !byte >139, >147, >156, >166, >175, >186, >197, >209
        !byte >221, >234, >248, >263, >278, >295, >313, >331
        !byte >351, >372, >394, >417, >442, >468, >496, >526
        !byte >557, >590, >625, >662, >702, >743, >788, >834
        !byte >884, >937, >992, >1051, >1114, >1180, >1250, >1325
        !byte >1403, >1487, >1575, >1669, >1768, >1873, >1985, >2103
        !byte >2228, >2360, >2500, >2649, >2807, >2973, >3150, >3338
        !byte >3536, >3746, >3969, >4205, >4455, >4720, >5001, >5298
        !byte >5613, >5947, >6300, >6675, >7072, >7493, >7938, >8410
        !byte >8910, >9440, >10001, >10596, >11226, >11894, >12601, >13350
        !byte >14144, >14985, >15876, >16820, >17820, >18880, >20003, >21192
        !byte >22452, >23787, >25202, >26700, >28288, >29970, >31752, >33640
        !byte >35641, >37760, >40005, >42384, >44904, >47574, >50403, >53401
        !byte >56576, >59940, >63504, >65535, >65535, >65535, >65535, >65535
        !byte >65535, >65535, >65535, >65535, >65535, >65535, >65535, >65535
        !byte >65535, >65535, >65535, >65535, >65535, >65535, >65535, >65535

; INCLUDIAMO INFINE LA SEQUENZA DI NOTE
!source "song_data.asm"

} ; Fine pseudopc