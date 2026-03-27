; --- Configurazione Dinamica Hardware --- 
NUM_SIDS = 4
SID1_BASE = $D400
SID2_BASE = $D420
SID3_BASE = $D500
SID4_BASE = $D600
voice_base_lo: !byte $00, $07, $0E, $20, $27, $2E, $00, $07, $0E, $00, $07, $0E
voice_base_hi: !byte $D4, $D4, $D4, $D4, $D4, $D4, $D5, $D5, $D5, $D6, $D6, $D6
voice_waves: !byte $40, $10, $40, $40, $10, $40, $40, $10, $40, $40, $10, $40
