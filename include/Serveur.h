#ifndef SERVEUR_H
#define SERVEUR_H

#include <signal.h>
#include "Message.h"
#include <Groupe.h>

#define PORT_SERVEUR 8000
#define ADRESSE_IP "127.0.0.1"

// Socket global pour le handler SIGINT
extern int sockfd;

// Fonctions principales
void configurer_signal_handler(void);
void handler_sigint(int sig);
int creer_socket_serveur(void);
void traiter_message(struct_message *msg);
void lancer_serveur(int socket);

#endif