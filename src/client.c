#include "commun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ========================================================================
   VARIABLES GLOBALES
   ======================================================================== */

static int sockfd = -1;
static struct sockaddr_in adresse_serveur;
static int id_client_local = 0;
static char pseudo_utilisateur[TAILLE_PSEUDO] = {0};
static int groupe_actuel = ID_GROUPE_AUCUN;             // Groupe actuellement rejoint
static char nom_groupe_actuel[TAILLE_NOM_GROUPE] = {0}; // Nom du groupe actuel

/* ========================================================================
   AFFICHAGE DU MENU
   ======================================================================== */

void afficher_menu(void)
{
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║       CLIENT ISY - Menu Principal      ║\n");
    printf("╠════════════════════════════════════════╣\n");

    if (pseudo_utilisateur[0] == '\0')
    {
        printf("║ ⚠  Vous devez vous connecter d'abord   ║\n");
        printf("╠════════════════════════════════════════╣\n");
    }
    else
    {
        printf("║ 👤 Connecté : %-24s ║\n", pseudo_utilisateur);
        if (groupe_actuel >= 0 && nom_groupe_actuel[0] != '\0')
        {
            printf("║ 📁 Groupe : %-26s ║\n", nom_groupe_actuel);
        }
        printf("╠════════════════════════════════════════╣\n");
    }

    printf("║ 1. Se connecter                        ║\n");
    printf("║ 2. Créer un groupe                     ║\n");
    printf("║ 3. Lister les groupes                  ║\n");
    printf("║ 4. Rejoindre un groupe                 ║\n");
    printf("║ 5. Quitter un groupe                   ║\n");
    printf("║ 6. Envoyer un message au groupe        ║\n");
    printf("║ 7. Se déconnecter                      ║\n");
    printf("║ 8. Quitter                             ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("Choix : ");
}

/* ========================================================================
   RÉCEPTION DE RÉPONSES DU SERVEUR
   ======================================================================== */

int recevoir_reponse(Message *reponse, int timeout_sec)
{
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);

    if (ret < 0)
    {
        perror("[ERREUR] select()");
        return -1;
    }
    else if (ret == 0)
    {
        printf("[TIMEOUT] Pas de réponse du serveur\n");
        return 0;
    }

    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    ssize_t nb = recvfrom(sockfd, reponse, sizeof(*reponse), 0,
                          (struct sockaddr *)&from, &fromlen);

    if (nb < 0)
    {
        perror("[ERREUR] recvfrom()");
        return -1;
    }

    return 1;
}

/* ========================================================================
   CONNEXION AU SERVEUR
   ======================================================================== */

void se_connecter(void)
{
    if (pseudo_utilisateur[0] != '\0')
    {
        printf("[INFO] Déjà connecté en tant que '%s'\n", pseudo_utilisateur);
        return;
    }

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

    if (strlen(msg.pseudo) == 0)
    {
        printf("[ERREUR] Pseudo vide\n");
        return;
    }

    snprintf(msg.texte, TAILLE_TEXTE, "Demande de connexion");

    printf("[ENVOI] Connexion au serveur...\n");

    if (sendto(sockfd, &msg, sizeof(msg), 0,
               (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    // Attendre réponse
    Message reponse;
    if (recevoir_reponse(&reponse, 2) > 0)
    {
        if (reponse.type == MSG_CONNEXION)
        {
            strncpy(pseudo_utilisateur, msg.pseudo, TAILLE_PSEUDO - 1);
            pseudo_utilisateur[TAILLE_PSEUDO - 1] = '\0';
            printf("[OK] %s\n", reponse.texte);
        }
        else if (reponse.type == MSG_ERREUR)
        {
            printf("[ERREUR] %s\n", reponse.texte);
        }
    }
}

/* ========================================================================
   CRÉATION DE GROUPE
   ======================================================================== */

void creer_groupe(void)
{
    if (pseudo_utilisateur[0] == '\0')
    {
        printf("[ERREUR] Connectez-vous d'abord (option 1)\n");
        return;
    }

    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_CREER_GROUPE;
    msg.id_client = id_client_local;
    msg.id_groupe = ID_GROUPE_AUCUN;
    strncpy(msg.pseudo, pseudo_utilisateur, TAILLE_PSEUDO - 1);

    printf("\nNom du groupe à créer : ");
    if (fgets(msg.texte, TAILLE_TEXTE, stdin) == NULL)
    {
        printf("[ERREUR] Lecture nom échouée\n");
        return;
    }
    supprimer_retour_ligne(msg.texte);

    if (strlen(msg.texte) == 0)
    {
        printf("[ERREUR] Nom de groupe vide\n");
        return;
    }

    printf("[ENVOI] Création du groupe '%s'...\n", msg.texte);

    if (sendto(sockfd, &msg, sizeof(msg), 0,
               (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    // Attendre réponse
    Message reponse;
    if (recevoir_reponse(&reponse, 2) > 0)
    {
        if (reponse.type == MSG_CREER_GROUPE)
        {
            printf("[SERVEUR] %s\n", reponse.texte);
            if (reponse.id_groupe >= 0)
            {
                printf("[INFO] Groupe ID: %d\n", reponse.id_groupe);
            }
        }
        else if (reponse.type == MSG_ERREUR)
        {
            printf("[ERREUR] %s\n", reponse.texte);
        }
    }
}

/* ========================================================================
   LISTER LES GROUPES
   ======================================================================== */

void lister_groupes(void)
{
    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_LISTE_GROUPES;
    msg.id_client = id_client_local;

    if (sendto(sockfd, &msg, sizeof(msg), 0,
               (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    // Attendre réponse
    Message reponse;
    if (recevoir_reponse(&reponse, 2) > 0)
    {
        if (reponse.type == MSG_LISTE_GROUPES)
        {
            printf("\n╔════════════════════════════════════════╗\n");
            printf("║          GROUPES DISPONIBLES           ║\n");
            printf("╚════════════════════════════════════════╝\n");
            printf("%s\n", reponse.texte);
        }
    }
}

/* ========================================================================
   REJOINDRE UN GROUPE
   ======================================================================== */

void rejoindre_groupe(void)
{
    if (pseudo_utilisateur[0] == '\0')
    {
        printf("[ERREUR] Connectez-vous d'abord (option 1)\n");
        return;
    }

    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_REJOINDRE_GROUPE;
    msg.id_client = id_client_local;
    msg.id_groupe = ID_GROUPE_AUCUN;
    strncpy(msg.pseudo, pseudo_utilisateur, TAILLE_PSEUDO - 1);

    printf("\nNom du groupe à rejoindre : ");
    if (fgets(msg.texte, TAILLE_TEXTE, stdin) == NULL)
    {
        printf("[ERREUR] Lecture nom échouée\n");
        return;
    }
    supprimer_retour_ligne(msg.texte);

    if (strlen(msg.texte) == 0)
    {
        printf("[ERREUR] Nom de groupe vide\n");
        return;
    }

    if (sendto(sockfd, &msg, sizeof(msg), 0,
               (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    // Attendre réponse
    Message reponse;
    if (recevoir_reponse(&reponse, 2) > 0)
    {
        if (reponse.type == MSG_REJOINDRE_GROUPE)
        {
            printf("[SERVEUR] %s\n", reponse.texte);
            if (reponse.id_groupe >= 0)
            {
                groupe_actuel = reponse.id_groupe;
                // Sauvegarder le nom du groupe
                strncpy(nom_groupe_actuel, msg.texte, TAILLE_NOM_GROUPE - 1);
                nom_groupe_actuel[TAILLE_NOM_GROUPE - 1] = '\0';
                printf("[INFO] Vous êtes maintenant dans le groupe: %s\n", nom_groupe_actuel);
            }
        }
    }
}

/* ========================================================================
   QUITTER UN GROUPE
   ======================================================================== */

void quitter_groupe(void)
{
    if (groupe_actuel < 0)
    {
        printf("[ERREUR] Vous n'êtes dans aucun groupe\n");
        return;
    }

    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_QUITTER_GROUPE;
    msg.id_client = id_client_local;
    msg.id_groupe = groupe_actuel;
    strncpy(msg.pseudo, pseudo_utilisateur, TAILLE_PSEUDO - 1);

    if (sendto(sockfd, &msg, sizeof(msg), 0,
               (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    // Attendre réponse
    Message reponse;
    if (recevoir_reponse(&reponse, 2) > 0)
    {
        if (reponse.type == MSG_QUITTER_GROUPE)
        {
            printf("[SERVEUR] %s\n", reponse.texte);
            if (reponse.id_groupe >= 0)
            {
                groupe_actuel = ID_GROUPE_AUCUN;
                memset(nom_groupe_actuel, 0, TAILLE_NOM_GROUPE);
            }
        }
    }
}

/* ========================================================================
   ENVOYER UN MESSAGE DANS LE GROUPE
   ======================================================================== */

void envoyer_message_groupe(void)
{
    if (pseudo_utilisateur[0] == '\0')
    {
        printf("[ERREUR] Connectez-vous d'abord (option 1)\n");
        return;
    }

    if (groupe_actuel < 0)
    {
        printf("[ERREUR] Rejoignez un groupe d'abord (option 4)\n");
        return;
    }

    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_ENVOI_MESSAGE;
    msg.id_client = id_client_local;
    msg.id_groupe = groupe_actuel;
    strncpy(msg.pseudo, pseudo_utilisateur, TAILLE_PSEUDO - 1);

    printf("\nVotre message : ");
    if (fgets(msg.texte, TAILLE_TEXTE, stdin) == NULL)
    {
        printf("[ERREUR] Lecture message échouée\n");
        return;
    }
    supprimer_retour_ligne(msg.texte);

    if (strlen(msg.texte) == 0)
    {
        printf("[ERREUR] Message vide\n");
        return;
    }

    if (sendto(sockfd, &msg, sizeof(msg), 0,
               (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        perror("[ERREUR] sendto()");
        return;
    }

    printf("[OK] Message envoyé au groupe '%s'\n", nom_groupe_actuel);
}

/* ========================================================================
   DÉCONNEXION
   ======================================================================== */

void se_deconnecter(void)
{
    if (pseudo_utilisateur[0] == '\0')
    {
        printf("[INFO] Vous n'êtes pas connecté\n");
        return;
    }

    Message msg;
    initialiser_message(&msg);

    msg.type = MSG_DECONNEXION;
    msg.id_client = id_client_local;
    msg.id_groupe = ID_GROUPE_AUCUN;
    strncpy(msg.pseudo, pseudo_utilisateur, TAILLE_PSEUDO - 1);
    snprintf(msg.texte, TAILLE_TEXTE, "Déconnexion");

    sendto(sockfd, &msg, sizeof(msg), 0,
           (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur));

    printf("[OK] Déconnexion envoyée (pseudo: %s)\n", msg.pseudo);

    // Réinitialiser
    memset(pseudo_utilisateur, 0, TAILLE_PSEUDO);
    groupe_actuel = ID_GROUPE_AUCUN;
    memset(nom_groupe_actuel, 0, TAILLE_NOM_GROUPE);
}

/* ========================================================================
   FONCTION PRINCIPALE
   ======================================================================== */

int main(void)
{
    printf("╔════════════════════════════════════════╗\n");
    printf("║      CLIENT ISY - PHASE 2              ║\n");
    printf("║      Gestion des groupes (CRUD)        ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    // Génération ID client (toujours < NB_MAX_CLIENTS)
    id_client_local = getpid() % NB_MAX_CLIENTS;
    printf("[INFO] ID Client local : %d\n", id_client_local);

    // Socket UDP
    sockfd = creer_socket_udp();
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
            while (getchar() != '\n')
                ;
            printf("⚠ Saisie invalide\n");
            continue;
        }
        while (getchar() != '\n')
            ;

        switch (choix)
        {
        case 1:
            se_connecter();
            break;
        case 2:
            creer_groupe();
            break;
        case 3:
            lister_groupes();
            break;
        case 4:
            rejoindre_groupe();
            break;
        case 5:
            quitter_groupe();
            break;
        case 6:
            envoyer_message_groupe();
            break;
        case 7:
            se_deconnecter();
            break;
        case 8:
            printf("\n[INFO] Fermeture du client...\n");
            quitter = 1;
            break;
        default:
            printf("⚠ Choix invalide (1-8)\n");
        }
    }

    close(sockfd);
    printf("[OK] Client arrêté\n");

    return EXIT_SUCCESS;
}