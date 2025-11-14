// client.c - Client ISY
// PHASE 1 : Communication UDP de base

#include "commun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ========================================================================
   VARIABLES GLOBALES
   ======================================================================== */

static int sockfd = -1;
static struct sockaddr_in adresse_serveur;
static int id_client_local = 0; // Sera assigné dynamiquement plus tard

/* ========================================================================
   AFFICHAGE DU MENU
   ======================================================================== */

void afficher_menu(void)
{
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║       CLIENT ISY - Menu Principal      ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ 1. Envoyer un message test             ║\n");
    printf("║ 2. Se connecter au serveur             ║\n");
    printf("║ 3. Se déconnecter                      ║\n");
    printf("║ 4. Quitter                             ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("Choix : ");
}

/* ========================================================================
   ENVOI DE MESSAGE TEST
   ======================================================================== */

void envoyer_message_test(void)
{
    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_ENVOI_MESSAGE;
    msg.id_client = id_client_local;
    msg.id_groupe = ID_GROUPE_AUCUN;
    snprintf(msg.pseudo, TAILLE_PSEUDO, "TestUser%d", id_client_local);
    snprintf(msg.texte, TAILLE_TEXTE, "Hello from client %d!", id_client_local);

    printf("\n[ENVOI] Envoi du message test...\n");
    debug_afficher_message(&msg);

    ssize_t nb_octets = sendto(
        sockfd,
        &msg,
        sizeof(msg),
        0,
        (struct sockaddr *)&adresse_serveur,
        sizeof(adresse_serveur));

    if (nb_octets < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    printf("[OK] %ld octets envoyés au serveur\n", (long)nb_octets);
}

/* ========================================================================
   CONNEXION AU SERVEUR
   ======================================================================== */

void se_connecter(void)
{
    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_CONNEXION;
    msg.id_client = id_client_local;
    msg.id_groupe = ID_GROUPE_AUCUN;

    printf("\nEntrez votre pseudo : ");
    if (fgets(msg.pseudo, TAILLE_PSEUDO, stdin) == NULL)
    {
        printf("[ERREUR] Lecture pseudo échouée\n");
        return;
    }
    supprimer_retour_ligne(msg.pseudo);

    snprintf(msg.texte, TAILLE_TEXTE, "Demande de connexion");

    printf("[ENVOI] Connexion au serveur...\n");

    ssize_t nb_octets = sendto(
        sockfd,
        &msg,
        sizeof(msg),
        0,
        (struct sockaddr *)&adresse_serveur,
        sizeof(adresse_serveur));

    if (nb_octets < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    printf("[OK] Connexion envoyée (pseudo: %s)\n", msg.pseudo);
}

/* ========================================================================
   DÉCONNEXION DU SERVEUR
   ======================================================================== */

void se_deconnecter(void)
{
    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_DECONNEXION;
    msg.id_client = id_client_local;
    msg.id_groupe = ID_GROUPE_AUCUN;
    snprintf(msg.texte, TAILLE_TEXTE, "Déconnexion");

    printf("[ENVOI] Déconnexion du serveur...\n");

    sendto(
        sockfd,
        &msg,
        sizeof(msg),
        0,
        (struct sockaddr *)&adresse_serveur,
        sizeof(adresse_serveur));

    printf("[OK] Déconnexion envoyée\n");
}

/* ========================================================================
   FONCTION PRINCIPALE
   ======================================================================== */

int main(void)
{
    printf("╔════════════════════════════════════════╗\n");
    printf("║      CLIENT ISY - PHASE 1              ║\n");
    printf("║      Communication UDP de base         ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    // Génération d'un ID client local (basique pour PHASE 1)
    id_client_local = getpid() % 1000;
    printf("[INFO] ID Client local : %d\n", id_client_local);

    // Création du socket UDP
    sockfd = creer_socket_udp();

    // Configuration de l'adresse du serveur
    initialiser_adresse(&adresse_serveur, IP_SERVEUR, PORT_SERVEUR);
    printf("[INFO] Serveur cible : %s:%d\n", IP_SERVEUR, PORT_SERVEUR);
    printf("─────────────────────────────────────────\n");

    // Boucle du menu
    int quitter = 0;
    int choix;

    while (!quitter)
    {
        afficher_menu();

        if (scanf("%d", &choix) != 1)
        {
            // Nettoyage du buffer stdin en cas d'erreur
            while (getchar() != '\n')
                ;
            printf("⚠ Saisie invalide, réessayez.\n");
            continue;
        }

        // Nettoyage du '\n' restant
        while (getchar() != '\n')
            ;

        switch (choix)
        {
        case 1:
            envoyer_message_test();
            break;

        case 2:
            se_connecter();
            break;

        case 3:
            se_deconnecter();
            break;

        case 4:
            printf("\n[INFO] Fermeture du client...\n");
            quitter = 1;
            break;

        default:
            printf("⚠ Choix invalide (1-4)\n");
        }
    }

    // Fermeture propre
    close(sockfd);
    printf("[OK] Client arrêté\n");

    return EXIT_SUCCESS;
}