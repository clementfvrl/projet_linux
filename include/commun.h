/**********************************************
 * FICHIER : Commun.h
 * Rôle   : Déclarations communes ISY
 *********************************************/

#ifndef COMMUN_H
#define COMMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ==== CONSTANTES GÉNÉRALES ==== */

#define ISY_PORT_SERVEUR 8000
#define ISY_IP_SERVEUR "127.0.0.1"

#define ISY_MAX_GROUPES 16
#define ISY_MAX_MEMBRES 32

#define ISY_TAILLE_ORDRE 4
#define ISY_TAILLE_NOM 20
#define ISY_TAILLE_TEXTE 100

/* ==== STRUCTURE DE MESSAGE ==== */
/* Ordres simples (3 lettres + '\0') :
 * "LST" : liste groupes
 * "CRG" : créer groupe
 * "JNG" : joindre groupe
 * "ACK" : acquittement
 * "ERR" : erreur
 * "MSG" : message de groupe
 */

typedef struct struct_message
{
    char Ordre[ISY_TAILLE_ORDRE];
    char Emetteur[ISY_TAILLE_NOM];
    char Texte[ISY_TAILLE_TEXTE];
} MessageISY;

/* ==== DESCRIPTION D'UN GROUPE CÔTÉ SERVEUR ==== */

typedef struct
{
    int actif; /* 0 = slot libre, 1 = utilisé */
    int id;    /* index dans le tableau */
    char nom[32];
    int port;                            /* port UDP du groupe (ex: 8100 + id) */
    int pid;                             /* PID du processus GroupeISY associé */
    char moderateurName[ISY_TAILLE_NOM]; /* nom du créateur/modérateur */
} GroupeServeur;

/* =================================================
 * FONCTIONS UTILITAIRES COMMUNES (static inline)
 * =================================================
 *
 * Ces fonctions sont définies ici pour éviter d'avoir
 * un cinquième fichier .c (Commun.c). Chaque unité de
 * compilation qui inclut Commun.h en aura sa propre
 * copie, ce qui respecte la contrainte des 4 fichiers .c.
 */

/* Création d'un socket UDP */
static inline int creer_socket_udp(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
    }
    return sock;
}

/* Fermeture d'un socket UDP */
static inline void fermer_socket_udp(int sock)
{
    if (sock >= 0)
    {
        close(sock);
    }
}

/* Initialisation d'une struct sockaddr_in */
static inline void init_sockaddr(struct sockaddr_in *addr,
                                 const char *ip,
                                 int port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    addr->sin_addr.s_addr = inet_addr(ip);
}

/* Affichage debug d'un message ISY */
static inline void afficher_message_debug(const char *prefix,
                                          const MessageISY *msg)
{
    printf("[%s] Ordre='%s' Emetteur='%s' Texte='%s'\n",
           prefix, msg->Ordre, msg->Emetteur, msg->Texte);
}

#endif /* COMMUN_H */
