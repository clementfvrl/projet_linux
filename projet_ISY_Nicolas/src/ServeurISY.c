/**********************************************
 * FICHIER : ServeurISY.c
 * Rôle   : Serveur principal ISY
 *********************************************/

#include "Commun.h"

static int sock_serveur = -1;
static GroupeServeur g_groupes[ISY_MAX_GROUPES];

static void init_groupes(void)
{
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        g_groupes[i].actif = 0;
        g_groupes[i].id    = i;
        g_groupes[i].port  = 0;
        g_groupes[i].nom[0] = '\0';
    }
}

/* Trouve un slot libre pour un nouveau groupe, renvoie index ou -1 */
static int trouver_slot_groupe(void)
{
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        if (!g_groupes[i].actif) return i;
    }
    return -1;
}

/* Trouve un groupe par son nom, renvoie index ou -1 */
static int trouver_groupe_par_nom(const char *nom)
{
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        if (g_groupes[i].actif && strcmp(g_groupes[i].nom, nom) == 0) {
            return i;
        }
    }
    return -1;
}

/* Lance un processus GroupeISY pour ce groupe (port dédié) */
static int lancer_groupe_process(int idGroupe)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork GroupeISY");
        return -1;
    }
    if (pid == 0) {
        /* Processus fils : execl GroupeISY */
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", g_groupes[idGroupe].port);
        execl("./GroupeISY", "GroupeISY", portStr, (char *)NULL);
        perror("execl GroupeISY");
        exit(EXIT_FAILURE);
    }
    /* Père : on ne garde pas spécialement le pid ici */
    return 0;
}

/* Gère la création de groupe (ordre "CRG") */
static void traiter_creation_groupe(const MessageISY *msgReq,
                                    MessageISY *msgRep)
{
    /* msgReq->Texte = nom groupe */
    const char *nomG = msgReq->Texte;

    if (trouver_groupe_par_nom(nomG) >= 0) {
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Groupe '%s' existe deja", nomG);
        return;
    }

    int idx = trouver_slot_groupe();
    if (idx < 0) {
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Nombre max de groupes atteint");
        return;
    }

    g_groupes[idx].actif = 1;
    g_groupes[idx].port  = 8100 + idx;     /* ex: 8100, 8101, ... */
    strncpy(g_groupes[idx].nom, nomG, sizeof(g_groupes[idx].nom) - 1);
    g_groupes[idx].nom[sizeof(g_groupes[idx].nom) - 1] = '\0';

    if (lancer_groupe_process(idx) < 0) {
        g_groupes[idx].actif = 0;
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Echec lancement GroupeISY");
        return;
    }

    snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ACK");
    snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
             "Groupe '%s' cree sur port %d", nomG, g_groupes[idx].port);
}

/* Gère la demande de liste ("LST") */
static void traiter_liste_groupes(MessageISY *msgRep)
{
    snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ACK");
    msgRep->Texte[0] = '\0';

    char line[64];
    int first = 1;
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        if (g_groupes[i].actif) {
            snprintf(line, sizeof(line), "%s (port %d)\n",
                     g_groupes[i].nom, g_groupes[i].port);
            if (strlen(msgRep->Texte) + strlen(line) < ISY_TAILLE_TEXTE) {
                strcat(msgRep->Texte, line);
            } else {
                break; /* buffer plein, on coupe */
            }
            first = 0;
        }
    }
    if (first) {
        strncpy(msgRep->Texte, "Aucun groupe\n", ISY_TAILLE_TEXTE - 1);
        msgRep->Texte[ISY_TAILLE_TEXTE - 1] = '\0';
    }
}

/* Gère la demande de join ("JNG") :
 * - msgReq->Texte = nom groupe
 * - réponse : ACK + texte = "OK <port>" ou ERR
 */
static void traiter_join_groupe(const MessageISY *msgReq,
                                MessageISY *msgRep)
{
    const char *nomG = msgReq->Texte;
    int idx = trouver_groupe_par_nom(nomG);
    if (idx < 0) {
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Groupe '%s' introuvable", nomG);
        return;
    }

    snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ACK");
    snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
             "OK %d", g_groupes[idx].port);
}

/* Handler SIGINT pour arrêter proprement */
static volatile sig_atomic_t g_stop = 0;
static void sigint_handler(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* ==== MAIN ==== */
int main(void)
{
    struct sockaddr_in addrServ, addrCli;
    socklen_t lenCli = sizeof(addrCli);

    signal(SIGINT, sigint_handler);

    init_groupes();

    sock_serveur = creer_socket_udp();
    if (sock_serveur < 0) {
        exit(EXIT_FAILURE);
    }

    init_sockaddr(&addrServ, ISY_IP_SERVEUR, ISY_PORT_SERVEUR);
    if (bind(sock_serveur, (struct sockaddr *)&addrServ, sizeof(addrServ)) < 0) {
        perror("bind ServeurISY");
        close(sock_serveur);
        exit(EXIT_FAILURE);
    }

    printf("ServeurISY : en ecoute sur %s:%d\n",
           ISY_IP_SERVEUR, ISY_PORT_SERVEUR);

    while (!g_stop) {
        MessageISY msgReq, msgRep;
        ssize_t n = recvfrom(sock_serveur, &msgReq, sizeof(msgReq), 0,
                             (struct sockaddr *)&addrCli, &lenCli);
        if (n < 0) {
            if (errno == EINTR && g_stop) break;
            perror("recvfrom ServeurISY");
            continue;
        }

        msgReq.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msgReq.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msgReq.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        printf("\n--- Recu du client ---\n");
        afficher_message_debug("Serveur", &msgReq);

        memset(&msgRep, 0, sizeof(msgRep));
        strncpy(msgRep.Emetteur, "Serveur", ISY_TAILLE_NOM - 1);

        if (strcmp(msgReq.Ordre, "CRG") == 0) {
            traiter_creation_groupe(&msgReq, &msgRep);
        } else if (strcmp(msgReq.Ordre, "LST") == 0) {
            traiter_liste_groupes(&msgRep);
        } else if (strcmp(msgReq.Ordre, "JNG") == 0) {
            traiter_join_groupe(&msgReq, &msgRep);
        } else {
            snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ERR");
            snprintf(msgRep.Texte, ISY_TAILLE_TEXTE,
                     "Ordre '%s' non supporte", msgReq.Ordre);
        }

        printf("--- Envoi reponse ---\n");
        afficher_message_debug("Serveur", &msgRep);

        if (sendto(sock_serveur, &msgRep, sizeof(msgRep), 0,
                   (struct sockaddr *)&addrCli, lenCli) < 0) {
            perror("sendto ServeurISY");
        }
    }

    printf("ServeurISY : arret\n");
    fermer_socket_udp(sock_serveur);
    return 0;
}
