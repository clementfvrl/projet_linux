// serveur.c - Serveur ISY
// PHASE 1 : Communication UDP de base

#include "commun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

// Variable globale pour gestion propre du signal SIGINT
static volatile int serveur_actif = 1;

/* ========================================================================
   GESTION DES SIGNAUX
   ======================================================================== */

void gestionnaire_sigint(int sig)
{
    (void)sig; // Évite warning unused parameter
    printf("\n[INFO] Arrêt du serveur demandé...\n");
    serveur_actif = 0;
}

/* ========================================================================
   FONCTION PRINCIPALE
   ======================================================================== */

int main(void)
{
    printf("╔════════════════════════════════════════╗\n");
    printf("║     SERVEUR ISY - PHASE 1              ║\n");
    printf("║     Communication UDP de base          ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    // Configuration du gestionnaire de signaux
    signal(SIGINT, gestionnaire_sigint);

    // Création du socket UDP
    int sockfd = creer_socket_udp();

    // Configuration de l'adresse du serveur
    struct sockaddr_in adresse_serveur;
    initialiser_adresse(&adresse_serveur, IP_SERVEUR, PORT_SERVEUR);

    // Binding du socket sur le port
    if (bind(sockfd, (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        close(sockfd);
        erreur_fatale("Erreur bind()");
    }

    printf("[OK] Serveur en écoute sur %s:%d\n", IP_SERVEUR, PORT_SERVEUR);
    printf("[INFO] Appuyez sur Ctrl+C pour arrêter\n");
    printf("─────────────────────────────────────────\n\n");

    // Buffers pour réception
    Message msg_recu;
    struct sockaddr_in adresse_client;
    socklen_t taille_adresse = sizeof(adresse_client);

    // Boucle principale de réception
    while (serveur_actif)
    {
        memset(&msg_recu, 0, sizeof(msg_recu));

        ssize_t nb_octets = recvfrom(
            sockfd,
            &msg_recu,
            sizeof(msg_recu),
            0,
            (struct sockaddr *)&adresse_client,
            &taille_adresse);

        if (nb_octets < 0)
        {
            if (serveur_actif)
                perror("[ERREUR] recvfrom()");
            continue;
        }

        // Affichage des infos client
        char ip_client[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &adresse_client.sin_addr, ip_client, sizeof(ip_client));

        printf("\n[RÉCEPTION] %ld octets depuis %s:%d\n",
               (long)nb_octets,
               ip_client,
               ntohs(adresse_client.sin_port));

        // Debug du message reçu
        debug_afficher_message(&msg_recu);

        // PHASE 1 : Simple affichage, pas de traitement
        printf("[INFO] Message traité (pas d'action pour l'instant)\n");
    }

    // Fermeture propre
    close(sockfd);
    printf("\n[OK] Serveur arrêté proprement\n");

    return EXIT_SUCCESS;
}