# Makefile for MidiWorkflow
# Builds converter.c and player.asm (6502 SID player via ACME).

CC = gcc
CFLAGS = -O2 -std=c11 -Wall
CONVERTER = converter
CONVERTER_SRCS = converter.c

# ACME toolchain settings
ACME = acme
PLAYER_ASM = player.asm
PLAYER_SID = player.sid
SONG_DATA = song_data.asm

.PHONY: all clean build-converter build-sid

# Il comando di default compila tutto e genera il SID
all: build-converter build-sid

# 1. Compila il programma C (il convertitore)
build-converter: $(CONVERTER_SRCS)
	$(CC) $(CFLAGS) $(CONVERTER_SRCS) -o $(CONVERTER)

# 2. Genera i dati ed esegue l'assemblatore
# Dipende dalla presenza di input.mid e dal convertitore compilato
build-sid: build-converter input.mid
	./$(CONVERTER) input.mid
	$(ACME) -f plain -o $(PLAYER_SID) $(PLAYER_ASM)

clean:
	rm -f $(CONVERTER) $(CONVERTER).exe $(PLAYER_SID) $(SONG_DATA)