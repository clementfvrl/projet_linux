#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "EnvoiMessage.h"

// Variable globale pour le socket (accessible par le handler)
int sockfd = -1;

/**
 * Handler pour SIGINT (CTRL-C)
 */
void handler_sigint(int sig)
{
    (void)sig;
    printf("\nFin du programme\n");

    if (sockfd >= 0)
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
 * Crée le socket UDP pour l'envoi
 */
int creer_socket_envoi(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        perror("Erreur création socket");
        exit(EXIT_FAILURE);
    }
    return sock;
}

/**
 * Boucle d'envoi de messages
 */
void envoyer_message(int socket, int identifiant_client, const char *nom_groupe)
{
    struct sockaddr_in adresse_dest;
    struct_message message;
    char buffer_saisie[BUFSIZE];

    // Configuration de l'adresse de destination
    memset(&adresse_dest, 0, sizeof(adresse_dest));
    adresse_dest.sin_family = AF_INET;
    adresse_dest.sin_port = htons(PORT_ENVOI);
    adresse_dest.sin_addr.s_addr = inet_addr(ADRESSE_IP);

    printf("Client %d connecté au groupe '%s'\n", identifiant_client, nom_groupe);

    // Boucle d'envoi
    while (1)
    {
        printf("Veuillez saisir un message :\n");

        // Lecture de la saisie utilisateur
        if (fgets(buffer_saisie, BUFSIZE, stdin) == NULL)
        {
            break;
        }

        // Suppression du '\n' final
        size_t len = strlen(buffer_saisie);
        if (len > 0 && buffer_saisie[len - 1] == '\n')
        {
            buffer_saisie[len - 1] = '\0';
        }

        // Vérification : message non vide
        if (strlen(buffer_saisie) == 0)
        {
            fprintf(stderr, "Erreur : message vide\n");
            continue;
        }

        // Construction du message
        memset(&message, 0, sizeof(struct_message));
        SetNumClient(identifiant_client, &message);
        SetMessageClient(buffer_saisie, &message);
        SetGroupeMessage(nom_groupe, &message);

        // Envoi du message
        ssize_t nb_octets = sendto(socket, &message, sizeof(struct_message), 0,
                                   (struct sockaddr *)&adresse_dest,
                                   sizeof(adresse_dest));

        if (nb_octets < 0)
        {
            perror("Erreur envoi");
            continue;
        }

        printf("Envoi du Message \"%s\" dans le groupe '%s'\n",
               buffer_saisie, nom_groupe);
    }
}

int main(int argc, char **argv)
{
    int num_client = 1;
    char nom_groupe[MAX_GROUPE_NAME] = "general";

    // Récupération du numéro de client depuis les arguments
    if (argc >= 2)
    {
        num_client = atoi(argv[1]);
        if (num_client <= 0)
        {
            fprintf(stderr, "Erreur : le numero de client doit etre > 0\n");
            exit(EXIT_FAILURE);
        }
    }

    // Récupération optionnelle du nom de groupe
    if (argc >= 3)
    {
        strncpy(nom_groupe, argv[2], MAX_GROUPE_NAME - 1);
        nom_groupe[MAX_GROUPE_NAME - 1] = '\0';
    }

    // Configuration signal
    configurer_signal_handler();

    // Création socket
    sockfd = creer_socket_envoi();

    // Boucle d'envoi
    envoyer_message(sockfd, num_client, nom_groupe);

    return 0;
}