CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude -D_POSIX_C_SOURCE
LDFLAGS = 

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SRCS = $(SRC_DIR)/ServeurISY.c \
       $(SRC_DIR)/GroupeISY.c  \
       $(SRC_DIR)/ClientISY.c  \
       $(SRC_DIR)/AffichageISY.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: $(BIN_DIR)/ServeurISY $(BIN_DIR)/GroupeISY $(BIN_DIR)/ClientISY $(BIN_DIR)/AffichageISY

# CORRECTION : Ajout de '| $(BIN_DIR)' sur la même ligne pour l'ordonnancement
$(BIN_DIR)/ServeurISY: $(OBJ_DIR)/ServeurISY.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_DIR)/GroupeISY: $(OBJ_DIR)/GroupeISY.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_DIR)/ClientISY: $(OBJ_DIR)/ClientISY.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_DIR)/AffichageISY: $(OBJ_DIR)/AffichageISY.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c include/commun.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(OBJ_DIR)/*.o $(BIN_DIR)/ServeurISY $(BIN_DIR)/GroupeISY $(BIN_DIR)/ClientISY $(BIN_DIR)/AffichageISY

.PHONY: all clean
