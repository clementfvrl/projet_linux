#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "Serveur.h"

// Socket global pour le handler
int sockfd = -1;

/**
 * Handler pour SIGINT (CTRL-C)
 */
void handler_sigint(int sig)
{
    (void)sig;
    printf("\n[SERVEUR] Arrêt du serveur...\n");
    lister_groupes();

    if (sockfd != -1)
    {
        close(sockfd);
    }
    exit(EXIT_SUCCESS);
}

/**
 * Configure le gestionnaire de signal
 */
void configurer_signal_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = handler_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Erreur sigaction");
        exit(EXIT_FAILURE);
    }
}

/**
 * Crée et configure le socket UDP du serveur
 */
int creer_socket_serveur(void)
{
    int sock;
    struct sockaddr_in addr_local;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        perror("Erreur création socket");
        exit(EXIT_FAILURE);
    }

    memset(&addr_local, 0, sizeof(addr_local));
    addr_local.sin_family = AF_INET;
    addr_local.sin_port = htons(PORT_SERVEUR);
    addr_local.sin_addr.s_addr = inet_addr(ADRESSE_IP);

    if (bind(sock, (struct sockaddr *)&addr_local, sizeof(addr_local)) == -1)
    {
        perror("Erreur bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

/**
 * Traite un message reçu et applique la logique de groupe
 */
void traiter_message(struct_message *msg)
{
    Groupe *g = trouver_groupe(msg->Groupe);

    // Si le groupe n'existe pas, le créer automatiquement
    if (g == NULL)
    {
        printf("[SERVEUR] Création automatique du groupe '%s'\n", msg->Groupe);
        if (creer_groupe(msg->Groupe, msg->Identifiant) == 0)
        {
            g = trouver_groupe(msg->Groupe);
        }
        else
        {
            fprintf(stderr, "[SERVEUR] Échec création groupe '%s'\n", msg->Groupe);
            return;
        }
    }

    // Si le client n'est pas membre, l'ajouter
    int est_membre = 0;
    for (int i = 0; i < g->nb_membres; i++)
    {
        if (g->membres[i] == msg->Identifiant)
        {
            est_membre = 1;
            break;
        }
    }

    if (!est_membre)
    {
        rejoindre_groupe(msg->Groupe, msg->Identifiant);
    }

    // Affichage du message
    printf("[GROUPE: %s] Client %d : \"%s\"\n",
           msg->Groupe, msg->Identifiant, msg->Texte);
}

/**
 * Boucle principale du serveur
 */
void lancer_serveur(int socket)
{
    struct_message msg;
    struct sockaddr_in addr_expediteur;
    socklen_t addr_len = sizeof(addr_expediteur);

    printf("[SERVEUR] Serveur démarré sur %s:%d\n", ADRESSE_IP, PORT_SERVEUR);
    printf("[SERVEUR] En attente de messages...\n\n");

    // Créer le groupe "general" par défaut
    creer_groupe("general", 0);

    while (1)
    {
        ssize_t received = recvfrom(socket, &msg, sizeof(struct_message), 0,
                                    (struct sockaddr *)&addr_expediteur, &addr_len);

        if (received == -1)
        {
            perror("Erreur réception message");
            continue;
        }

        // Traiter le message
        traiter_message(&msg);
    }
}

int main(void)
{
    configurer_signal_handler();
    sockfd = creer_socket_serveur();
    lancer_serveur(sockfd);
    return 0;
}