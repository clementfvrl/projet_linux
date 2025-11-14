CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11
LDFLAGS = 

SRC     = ServeurISY.c GroupeISY.c ClientISY.c AffichageISY.c
OBJ     = $(SRC:.c=.o)

all: ServeurISY GroupeISY ClientISY AffichageISY

ServeurISY: ServeurISY.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

GroupeISY: GroupeISY.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ClientISY: ClientISY.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

AffichageISY: AffichageISY.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c Commun.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o ServeurISY GroupeISY ClientISY AffichageISY
