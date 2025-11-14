/**********************************************
 * FICHIER : ClientISY.c
 * Rôle   : Client ISY (menu + logique)
 *********************************************/

#include "Commun.h"

static int sock_client = -1;
static struct sockaddr_in addrServeur;

/* Groupe courant (on simplifie à un seul groupe à la fois) */
static int  g_portGroupeActif = 0;
static pid_t g_pidAffichage   = 0;
static char g_nomUtilisateur[ISY_TAILLE_NOM] = "user";

/* ==== Fonctions utilitaires client ==== */

static void envoyer_message_serveur(const MessageISY *msgReq,
                                    MessageISY *msgRep)
{
    socklen_t lenServ = sizeof(addrServeur);
    ssize_t n = sendto(sock_client, msgReq, sizeof(*msgReq), 0,
                       (struct sockaddr *)&addrServeur, lenServ);
    if (n < 0) {
        perror("sendto Client->Serveur");
        return;
    }

    n = recvfrom(sock_client, msgRep, sizeof(*msgRep), 0,
                 (struct sockaddr *)&addrServeur, &lenServ);
    if (n < 0) {
        perror("recvfrom Serveur->Client");
        return;
    }

    msgRep->Ordre[ISY_TAILLE_ORDRE - 1]  = '\0';
    msgRep->Emetteur[ISY_TAILLE_NOM - 1] = '\0';
    msgRep->Texte[ISY_TAILLE_TEXTE - 1]  = '\0';
}

/* Lance AffichageISY sur le port du groupe */
static void lancer_affichage(int portGroupe)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork AffichageISY");
        return;
    }
    if (pid == 0) {
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", portGroupe);
        execl("./AffichageISY", "AffichageISY", portStr, (char *)NULL);
        perror("execl AffichageISY");
        exit(EXIT_FAILURE);
    }
    g_pidAffichage = pid;
}

static void arreter_affichage(void)
{
    if (g_pidAffichage > 0) {
        kill(g_pidAffichage, SIGINT);
        /* on ne fait pas de waitpid ici, simplification */
        g_pidAffichage = 0;
    }
}

/* ==== Actions du menu ==== */

static void action_creer_groupe(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "CRG", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    printf("Nom du groupe a creer : ");
    if (fgets(req.Texte, ISY_TAILLE_TEXTE, stdin) == NULL) return;
    req.Texte[strcspn(req.Texte, "\n")] = '\0';

    envoyer_message_serveur(&req, &rep);

    printf("Reponse serveur : [%s] %s\n", rep.Ordre, rep.Texte);
}

static void action_lister_groupes(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "LST", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    envoyer_message_serveur(&req, &rep);

    printf("Reponse serveur : [%s]\n%s\n", rep.Ordre, rep.Texte);
}

static void action_rejoindre_groupe(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "JNG", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    printf("Nom du groupe a rejoindre : ");
    if (fgets(req.Texte, ISY_TAILLE_TEXTE, stdin) == NULL) return;
    req.Texte[strcspn(req.Texte, "\n")] = '\0';

    envoyer_message_serveur(&req, &rep);

    if (strcmp(rep.Ordre, "ACK") == 0) {
        /* rep.Texte = "OK <port>" */
        int port = 0;
        if (sscanf(rep.Texte, "OK %d", &port) == 1) {
            g_portGroupeActif = port;
            printf("Rejoint groupe sur port %d\n", g_portGroupeActif);
            lancer_affichage(g_portGroupeActif);
        } else {
            printf("Reponse ACK mal formee : %s\n", rep.Texte);
        }
    } else {
        printf("Erreur join : %s\n", rep.Texte);
    }
}

static void action_dialoguer_groupe(void)
{
    if (g_portGroupeActif == 0) {
        printf("Aucun groupe actif. Rejoindre un groupe d'abord.\n");
        return;
    }

    int sockG = creer_socket_udp();
    if (sockG < 0) return;

    struct sockaddr_in addrG;
    init_sockaddr(&addrG, ISY_IP_SERVEUR, g_portGroupeActif);

    printf("Tapez vos messages (ligne vide pour revenir au menu)...\n");

    char buffer[ISY_TAILLE_TEXTE];
    while (1) {
        printf("Message : ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;
        buffer[strcspn(buffer, "\n")] = '\0';
        if (buffer[0] == '\0') break;

        MessageISY msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.Ordre, "MSG", ISY_TAILLE_ORDRE - 1);
        strncpy(msg.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);
        strncpy(msg.Texte, buffer, ISY_TAILLE_TEXTE - 1);

        if (sendto(sockG, &msg, sizeof(msg), 0,
                   (struct sockaddr *)&addrG, sizeof(addrG)) < 0) {
            perror("sendto Client->Groupe");
        }
    }

    fermer_socket_udp(sockG);
}

static void action_quitter_groupe(void)
{
    if (g_portGroupeActif == 0) {
        printf("Aucun groupe actif.\n");
        return;
    }
    arreter_affichage();
    printf("Groupe sur port %d quitte (cote client seulement).\n",
           g_portGroupeActif);
    g_portGroupeActif = 0;
}

/* ==== MAIN + MENU ==== */

static void afficher_menu(void)
{
    printf("\n=== MENU ISY ===\n");
    printf("0. Quitter\n");
    printf("1. Creer un groupe\n");
    printf("2. Lister les groupes\n");
    printf("3. Rejoindre un groupe\n");
    printf("4. Dialoguer sur le groupe actif\n");
    printf("5. Quitter le groupe actif\n");
    printf("Votre choix : ");
}

int main(int argc, char *argv[])
{
    if (argc >= 2) {
        strncpy(g_nomUtilisateur, argv[1], ISY_TAILLE_NOM - 1);
        g_nomUtilisateur[ISY_TAILLE_NOM - 1] = '\0';
    }

    sock_client = creer_socket_udp();
    if (sock_client < 0) {
        exit(EXIT_FAILURE);
    }

    init_sockaddr(&addrServeur, ISY_IP_SERVEUR, ISY_PORT_SERVEUR);
    printf("ClientISY lance en tant que '%s'\n", g_nomUtilisateur);

    int choix = -1;
    char ligne[16];

    while (1) {
        afficher_menu();
        if (fgets(ligne, sizeof(ligne), stdin) == NULL) break;
        if (sscanf(ligne, "%d", &choix) != 1) continue;

        switch (choix) {
            case 0:
                printf("Fin du client.\n");
                arreter_affichage();
                fermer_socket_udp(sock_client);
                return 0;
            case 1: action_creer_groupe();      break;
            case 2: action_lister_groupes();    break;
            case 3: action_rejoindre_groupe();  break;
            case 4: action_dialoguer_groupe();  break;
            case 5: action_quitter_groupe();    break;
            default:
                printf("Choix invalide.\n");
        }
    }

    arreter_affichage();
    fermer_socket_udp(sock_client);
    return 0;
}
