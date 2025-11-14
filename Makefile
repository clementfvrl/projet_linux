# Makefile - Projet ISY - Messagerie Instantanée
# Architecture modulaire : serveur, client, groupe, affichage, crypto, stats

# === CONFIGURATION ===
CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -I./include -std=c11
LDFLAGS = -lm

# Répertoires
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

# === FICHIERS SOURCES ===
SRC_SERVER = $(SRC_DIR)/serveur.c
SRC_CLIENT = $(SRC_DIR)/client.c
SRC_GROUPE = $(SRC_DIR)/groupe.c
SRC_AFFICHAGE = $(SRC_DIR)/affichage.c
SRC_COMMUN = $(SRC_DIR)/commun.c
SRC_CRYPTO = $(SRC_DIR)/crypto.c
SRC_STATS = $(SRC_DIR)/stats.c

# === FICHIERS OBJETS (modules partagés) ===
OBJ_COMMUN = $(OBJ_DIR)/commun.o
OBJ_CRYPTO = $(OBJ_DIR)/crypto.o
OBJ_STATS = $(OBJ_DIR)/stats.o

# Tous les objets partagés
SHARED_OBJS = $(OBJ_COMMUN) $(OBJ_CRYPTO) $(OBJ_STATS)

# === EXÉCUTABLES ===
BIN_SERVER = $(BIN_DIR)/serveur
BIN_CLIENT = $(BIN_DIR)/client
BIN_GROUPE = $(BIN_DIR)/groupe
BIN_AFFICHAGE = $(BIN_DIR)/affichage

ALL_BINS = $(BIN_SERVER) $(BIN_CLIENT) $(BIN_GROUPE) $(BIN_AFFICHAGE)

# === RÈGLES PRINCIPALES ===
.PHONY: all clean directories help test run-serveur run-client

# Règle par défaut : tout compiler
all: directories $(ALL_BINS)
	@echo "============================================="
	@echo "✓ Compilation complète réussie !"
	@echo "============================================="

# Création des répertoires nécessaires
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

# === COMPILATION DES EXÉCUTABLES ===

# Serveur ISY (nécessite tous les modules)
$(BIN_SERVER): $(SRC_SERVER) $(SHARED_OBJS)
	@echo "[BUILD] Serveur ISY..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Serveur compilé -> $@"

# Client ISY (interface utilisateur)
$(BIN_CLIENT): $(SRC_CLIENT) $(SHARED_OBJS)
	@echo "[BUILD] Client ISY..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Client compilé -> $@"

# Processus Groupe (si nécessaire pour gestion multi-groupe)
$(BIN_GROUPE): $(SRC_GROUPE) $(SHARED_OBJS)
	@echo "[BUILD] Processus Groupe..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Groupe compilé -> $@"

# Processus Affichage (affichage parallèle des messages)
$(BIN_AFFICHAGE): $(SRC_AFFICHAGE) $(SHARED_OBJS)
	@echo "[BUILD] Processus Affichage..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Affichage compilé -> $@"

# === COMPILATION DES MODULES OBJETS ===

$(OBJ_COMMUN): $(SRC_COMMUN) $(INC_DIR)/commun.h
	@echo "[OBJ] Compilation commun.o..."
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_CRYPTO): $(SRC_CRYPTO) $(INC_DIR)/crypto.h $(INC_DIR)/commun.h
	@echo "[OBJ] Compilation crypto.o..."
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_STATS): $(SRC_STATS) $(INC_DIR)/stats.h $(INC_DIR)/commun.h
	@echo "[OBJ] Compilation stats.o..."
	$(CC) $(CFLAGS) -c $< -o $@

# === NETTOYAGE ===

# Nettoyage complet (objets + exécutables)
clean:
	@echo "Nettoyage en cours..."
	@rm -rf $(OBJ_DIR)
	@rm -rf $(BIN_DIR)
	@echo "✓ Nettoyage complet effectué"

# === TESTS ET LANCEMENT ===

# Lancer le serveur
run-serveur: $(BIN_SERVER)
	@echo "=== Démarrage Serveur ISY ==="
	./$(BIN_SERVER)

# Lancer un client
run-client: $(BIN_CLIENT)
	@echo "=== Démarrage Client ISY ==="
	./$(BIN_CLIENT)

# Test rapide : compile et vérifie que les binaires existent
test: all
	@echo "=== Vérification des binaires ==="
	@test -f $(BIN_SERVER) && echo "✓ serveur OK" || echo "✗ serveur manquant"
	@test -f $(BIN_CLIENT) && echo "✓ client OK" || echo "✗ client manquant"
	@test -f $(BIN_GROUPE) && echo "✓ groupe OK" || echo "✗ groupe manquant"
	@test -f $(BIN_AFFICHAGE) && echo "✓ affichage OK" || echo "✗ affichage manquant"

# === AIDE ===
help:
	@echo "╔════════════════════════════════════════════════╗"
	@echo "║       MAKEFILE PROJET ISY - AIDE               ║"
	@echo "╚════════════════════════════════════════════════╝"
	@echo ""
	@echo "Commandes de compilation :"
	@echo "  make              - Compile tout le projet"
	@echo "  make clean        - Supprime objets et binaires"
	@echo "  make test         - Compile et vérifie binaires"
	@echo ""
	@echo "Commandes de lancement :"
	@echo "  make run-serveur   - Lance le serveur ISY"
	@echo "  make run-client   - Lance un client ISY"
	@echo ""
	@echo "Exécutables générés :"
	@echo "  bin/serveur       - Serveur central UDP"
	@echo "  bin/client        - Interface utilisateur"
	@echo "  bin/groupe        - Processus de groupe (IPC)"
	@echo "  bin/affichage     - Affichage parallèle messages"
	@echo ""
	@echo "Structure de fichiers requise :"
	@echo "  src/             - Code source (.c)"
	@echo "  include/         - Headers (.h)"
	@echo "  obj/             - Fichiers objets (généré)"
	@echo "  bin/             - Exécutables (généré)"
	@echo ""

# Indique que ce ne sont pas des fichiers
.PHONY: all clean directories help test run-serveur run-client