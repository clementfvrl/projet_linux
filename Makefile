# Makefile - Projet Messagerie Instantanée
# Compilation des programmes de messagerie UDP

# Compilateur et flags
CC = gcc
CFLAGS = -Wall -Wextra -g -I./include
LDFLAGS = 

# Répertoires
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin
DATA_DIR = data

# Fichiers sources
SRC_ENVOI = $(SRC_DIR)/EnvoiMessage.c
SRC_SERVEUR = $(SRC_DIR)/Serveur.c
SRC_MESSAGE = $(SRC_DIR)/Message.c
SRC_GROUPE = $(SRC_DIR)/Groupe.c
SRC_STATS = $(SRC_DIR)/Stats.c
SRC_UTILS = $(SRC_DIR)/Utils.c

# Fichiers objets
OBJ_MESSAGE = $(OBJ_DIR)/Message.o
OBJ_GROUPE = $(OBJ_DIR)/Groupe.o
OBJ_STATS = $(OBJ_DIR)/Stats.o
OBJ_UTILS = $(OBJ_DIR)/Utils.o

# Exécutables
BIN_ENVOI = $(BIN_DIR)/EnvoiMessage
BIN_SERVEUR = $(BIN_DIR)/Serveur

# Règle par défaut
all: directories $(BIN_ENVOI) $(BIN_SERVEUR)

# Création des répertoires nécessaires
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(DATA_DIR)/groups

# Compilation de EnvoiMessage
$(BIN_ENVOI): $(SRC_ENVOI) $(OBJ_MESSAGE) $(OBJ_GROUPE) $(OBJ_STATS) $(OBJ_UTILS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ EnvoiMessage compilé avec succès"

# Compilation de Serveur
$(BIN_SERVEUR): $(SRC_SERVEUR) $(OBJ_MESSAGE) $(OBJ_GROUPE) $(OBJ_STATS) $(OBJ_UTILS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Serveur compilé avec succès"

# Compilation des modules
$(OBJ_DIR)/Message.o: $(SRC_MESSAGE) $(INC_DIR)/Message.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/Groupe.o: $(SRC_GROUPE) $(INC_DIR)/Groupe.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/Stats.o: $(SRC_STATS) $(INC_DIR)/Stats.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/Utils.o: $(SRC_UTILS) $(INC_DIR)/Utils.h
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyage complet
clean:
	@rm -rf $(OBJ_DIR)
	@rm -rf $(BIN_DIR)
	@echo "✓ Nettoyage complet effectué"

# Nettoyage des données (groupes et stats)
clean-data:
	@rm -rf $(DATA_DIR)/groups/*
	@echo "✓ Données supprimées"

# Règles pour les tests
test-envoi: $(BIN_ENVOI)
	@echo "Lancement de EnvoiMessage..."
	./$(BIN_ENVOI) 1

test-reception: $(BIN_SERVEUR)
	@echo "Lancement de Serveur..."
	./$(BIN_SERVEUR)

# Aide
help:
	@echo "Commandes disponibles:"
	@echo "  make          - Compile tout le projet"
	@echo "  make clean    - Supprime objets et exécutables"
	@echo "  make clean-data - Supprime les données"
	@echo "  make test-envoi - Lance EnvoiMessage"
	@echo "  make test-reception - Lance Serveur"
	@echo "  make help     - Affiche cette aide"

# Indique que ce ne sont pas des fichiers
.PHONY: all clean directories clean-data test-envoi test-reception help