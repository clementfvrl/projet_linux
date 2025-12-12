# Makefile pour le projet ISY Messagerie

# Compilateur et flags
CC = gcc
CFLAGS = -Wall -g -I./include
LDFLAGS = -pthread

# R�pertoires
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INC_DIR = include

# Fichiers sources
COMMUN_SRC = $(SRC_DIR)/commun.c
CRYPTO_SRC = $(SRC_DIR)/crypto.c
STATS_SRC = $(SRC_DIR)/stats.c
GROUPE_SRC = $(SRC_DIR)/groupe.c

SERVEUR_SRC = $(SRC_DIR)/serveur.c
GROUPEISY_SRC = $(SRC_DIR)/groupe.c
CLIENT_SRC = $(SRC_DIR)/client.c
AFFICHAGE_SRC = $(SRC_DIR)/affichage.c

# Fichiers objets
COMMUN_OBJ = $(OBJ_DIR)/commun.o
CRYPTO_OBJ = $(OBJ_DIR)/crypto.o
STATS_OBJ = $(OBJ_DIR)/stats.o
GROUPE_OBJ = $(OBJ_DIR)/groupe.o

SERVEUR_OBJ = $(OBJ_DIR)/serveur.o
CLIENT_OBJ = $(OBJ_DIR)/client.o
AFFICHAGE_OBJ = $(OBJ_DIR)/affichage.o

# Ex�cutables
SERVEUR_BIN = $(BIN_DIR)/ServeurISY
GROUPEISY_BIN = $(BIN_DIR)/GroupeISY
CLIENT_BIN = $(BIN_DIR)/ClientISY
AFFICHAGE_BIN = $(BIN_DIR)/AffichageISY

# Objets communs partag�s
COMMON_OBJS = $(COMMUN_OBJ) $(CRYPTO_OBJ) $(STATS_OBJ)

# Cibles
.PHONY: all clean directories serveur groupe client affichage

all: directories $(SERVEUR_BIN) $(GROUPEISY_BIN) $(CLIENT_BIN) $(AFFICHAGE_BIN)

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p data

# R�gles de compilation des fichiers objets
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ServeurISY
serveur: directories $(SERVEUR_BIN)

$(SERVEUR_BIN): $(SERVEUR_OBJ) $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# GroupeISY
groupe: directories $(GROUPEISY_BIN)

$(GROUPEISY_BIN): $(GROUPE_OBJ) $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ClientISY
client: directories $(CLIENT_BIN)

$(CLIENT_BIN): $(CLIENT_OBJ) $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# AffichageISY
affichage: directories $(AFFICHAGE_BIN)

$(AFFICHAGE_BIN): $(AFFICHAGE_OBJ) $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Nettoyage
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	rm -rf data/*
	@echo "Nettoyage termin�"

# D�pendances
$(SERVEUR_OBJ): $(SRC_DIR)/serveur.c $(INC_DIR)/commun.h
$(GROUPE_OBJ): $(SRC_DIR)/groupe.c $(INC_DIR)/commun.h
$(CLIENT_OBJ): $(SRC_DIR)/client.c $(INC_DIR)/commun.h
$(AFFICHAGE_OBJ): $(SRC_DIR)/affichage.c $(INC_DIR)/commun.h
$(COMMUN_OBJ): $(SRC_DIR)/commun.c $(INC_DIR)/commun.h
$(CRYPTO_OBJ): $(SRC_DIR)/crypto.c $(INC_DIR)/crypto.h $(INC_DIR)/commun.h
$(STATS_OBJ): $(SRC_DIR)/stats.c $(INC_DIR)/stats.h $(INC_DIR)/commun.h
