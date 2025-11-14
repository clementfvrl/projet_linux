// commun.c - Fonctions utilitaires partagées
// PHASE 1 : Communication UDP de base

#include "commun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* ========================================================================
   GESTION DES ERREURS
   ======================================================================== */

void erreur_fatale(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

/* ========================================================================
   MANIPULATION DE CHAÎNES
   ======================================================================== */

void supprimer_retour_ligne(char *chaine)
{
    if (chaine == NULL)
        return;

    size_t len = strlen(chaine);
    if (len > 0 && chaine[len - 1] == '\n')
        chaine[len - 1] = '\0';
}

/* ========================================================================
   DEBUG ET AFFICHAGE
   ======================================================================== */

void debug_afficher_message(const Message *msg)
{
    if (msg == NULL)
    {
        printf("[DEBUG] Message NULL\n");
        return;
    }

    printf("\n┌─────────────────────────────────────┐\n");
    printf("│ MESSAGE DEBUG                       │\n");
    printf("├─────────────────────────────────────┤\n");
    printf("│ Type       : %d                     \n", msg->type);
    printf("│ ID Client  : %d                     \n", msg->id_client);
    printf("│ ID Groupe  : %d                     \n", msg->id_groupe);
    printf("│ Pseudo     : %-20s │\n", msg->pseudo);
    printf("│ Texte      : %-20s │\n", msg->texte);
    printf("└─────────────────────────────────────┘\n");
}

/* ========================================================================
   UTILITAIRES SOCKET UDP
   ======================================================================== */

int creer_socket_udp(void)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        erreur_fatale("Erreur création socket UDP");

    printf("[INFO] Socket UDP créé (fd=%d)\n", sockfd);
    return sockfd;
}

void initialiser_adresse(struct sockaddr_in *addr, const char *ip, int port)
{
    if (addr == NULL)
        erreur_fatale("Adresse NULL");

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
        erreur_fatale("Adresse IP invalide");
}

/* ========================================================================
   MANIPULATION DE MESSAGES
   ======================================================================== */

void initialiser_message(Message *msg)
{
    if (msg == NULL)
        return;

    memset(msg, 0, sizeof(Message));
    msg->type = MSG_ERREUR;
    msg->id_client = -1;
    msg->id_groupe = ID_GROUPE_AUCUN;
}