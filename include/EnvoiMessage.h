#ifndef ENVOI_MESSAGE_H
#define ENVOI_MESSAGE_H

#include <signal.h>
#include "Message.h"

#define PORT_ENVOI 8000
#define ADRESSE_IP "127.0.0.1"

// Socket global pour le handler SIGINT
extern int sockfd;

// Fonctions principales
void configurer_signal_handler(void);
void handler_sigint(int sig);
int creer_socket_envoi(void);
void envoyer_message(int socket, int identifiant_client, const char *nom_groupe);

#endif