/**********************************************
 * FICHIER : GroupeISY.c
 * Rôle   : Processus de groupe ISY
 *********************************************/

#include "commun.h"
#include <strings.h>
#include <string.h> /* Nécessaire pour strstr */
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

static int sock_groupe = -1;
static int g_portGroupe = 0;

static char g_moderateurName[ISY_TAILLE_NOM] = "";

/* Structure Membre avec stats */
typedef struct
{
    int actif;
    struct sockaddr_in addr;
    char nom[ISY_TAILLE_NOM];
    int banni;

    /* Stats */
    time_t date_connexion;
    time_t date_dernier_msg;
    int nb_messages;
    double somme_intervalles;
} MembreGroupe;

static MembreGroupe g_membres[ISY_MAX_MEMBRES];

static void init_membres(void)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        g_membres[i].actif = 0;
        g_membres[i].banni = 0;
        g_membres[i].nom[0] = '\0';
        g_membres[i].nb_messages = 0;
        g_membres[i].somme_intervalles = 0.0;
        g_membres[i].date_connexion = 0;
        g_membres[i].date_dernier_msg = 0;
    }
}

static int trouver_slot_membre(void)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        if (!g_membres[i].actif)
            return i;
    }
    return -1;
}

static int adresse_deja_connue(const struct sockaddr_in *addr)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        if (g_membres[i].actif &&
            g_membres[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            g_membres[i].addr.sin_port == addr->sin_port)
        {
            return 1;
        }
    }
    return 0;
}

static int trouver_index_membre(const struct sockaddr_in *addr)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        if (g_membres[i].actif &&
            g_membres[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            g_membres[i].addr.sin_port == addr->sin_port)
        {
            return i;
        }
    }
    return -1;
}

static void ajouter_membre(const struct sockaddr_in *addr, const char *nom)
{
    /* 1. Adresse exacte déjà connue ? */
    if (adresse_deja_connue(addr))
        return;

    /* 2. Nom déjà connu ? (Mise à jour port) */
    if (nom && nom[0] != '\0')
    {
        for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
        {
            if (g_membres[i].actif && strcasecmp(g_membres[i].nom, nom) == 0)
            {
                g_membres[i].addr = *addr;
                return;
            }
        }
    }

    /* 3. Nouveau membre */
    int idx = trouver_slot_membre();
    if (idx < 0)
        return;

    g_membres[idx].actif = 1;
    g_membres[idx].addr = *addr;
    if (nom && nom[0] != '\0')
    {
        strncpy(g_membres[idx].nom, nom, ISY_TAILLE_NOM - 1);
        g_membres[idx].nom[ISY_TAILLE_NOM - 1] = '\0';
    }
    g_membres[idx].banni = 0;

    /* Init stats */
    g_membres[idx].date_connexion = time(NULL);
    g_membres[idx].date_dernier_msg = 0;
    g_membres[idx].nb_messages = 0;
    g_membres[idx].somme_intervalles = 0.0;
}

static void redistribuer_message(const MessageISY *msg,
                                 const struct sockaddr_in *addrEmetteur)
{
    printf("--- Diffusion de %s ---\n", msg->Emetteur);
    int envoyes = 0;

    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        if (g_membres[i].actif)
        {
            /* SI C'EST L'EMETTEUR (Console), ON PASSE */
            if (g_membres[i].addr.sin_addr.s_addr == addrEmetteur->sin_addr.s_addr &&
                g_membres[i].addr.sin_port == addrEmetteur->sin_port)
            {
                continue;
            }

            if (sendto(sock_groupe, msg, sizeof(*msg), 0,
                       (struct sockaddr *)&g_membres[i].addr,
                       sizeof(g_membres[i].addr)) < 0)
            {
                perror("sendto GroupeISY");
            }
            else
            {
                envoyes++;
            }
        }
    }
    if (envoyes == 0)
        printf("⚠️ Personne n'a reçu le message (Avez-vous lancé l'Affichage ?)\n");
}

static void arret_groupe(int sig)
{
    (void)sig;
    MessageISY msgFin;
    memset(&msgFin, 0, sizeof(msgFin));
    strncpy(msgFin.Ordre, "FIN", ISY_TAILLE_ORDRE - 1);
    strncpy(msgFin.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
    strncpy(msgFin.Texte, "Groupe dissous", ISY_TAILLE_TEXTE - 1);

    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        if (g_membres[i].actif)
        {
            sendto(sock_groupe, &msgFin, sizeof(msgFin), 0,
                   (struct sockaddr *)&g_membres[i].addr,
                   sizeof(g_membres[i].addr));
        }
    }
    // usleep(100000);
    sleep(0.1);
    fermer_socket_udp(sock_groupe);
    exit(0);
}

/* ==== MAIN ==== */
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: GroupeISY <portGroupe> <moderateurName>\n");
        exit(EXIT_FAILURE);
    }

    g_portGroupe = atoi(argv[1]);
    strncpy(g_moderateurName, argv[2], ISY_TAILLE_NOM - 1);
    g_moderateurName[ISY_TAILLE_NOM - 1] = '\0';
    printf("GroupeISY : lancement sur port %d (moderateur %s)\n", g_portGroupe, g_moderateurName);

    signal(SIGINT, arret_groupe);
    init_membres();

    sock_groupe = creer_socket_udp();
    if (sock_groupe < 0)
        exit(EXIT_FAILURE);

    struct sockaddr_in addrG;
    init_sockaddr(&addrG, ISY_IP_SERVEUR, g_portGroupe);
    if (bind(sock_groupe, (struct sockaddr *)&addrG, sizeof(addrG)) < 0)
    {
        perror("bind GroupeISY");
        fermer_socket_udp(sock_groupe);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addrCli;
    socklen_t lenCli = sizeof(addrCli);
    MessageISY msg;

    while (1)
    {
        ssize_t n = recvfrom(sock_groupe, &msg, sizeof(msg), 0,
                             (struct sockaddr *)&addrCli, &lenCli);
        if (n < 0)
            continue;

        msg.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msg.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msg.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        if (strcmp(msg.Ordre, "REG") == 0)
        {
            /* On ajoute _Vue pour distinguer l'affichage technique */
            char nomVue[ISY_TAILLE_NOM];
            snprintf(nomVue, ISY_TAILLE_NOM, "%s_Vue", msg.Emetteur);

            ajouter_membre(&addrCli, nomVue);
            printf("GroupeISY(port %d) : client affichage inscrit (%s)\n", g_portGroupe, nomVue);
        }
        else if (strcmp(msg.Ordre, "MSG") == 0)
        {
            ajouter_membre(&addrCli, msg.Emetteur);

            /* Stats */
            int idx = trouver_index_membre(&addrCli);
            int banned = 0;
            if (idx >= 0)
            {
                banned = g_membres[idx].banni;
                if (!banned)
                {
                    time_t now = time(NULL);
                    if (g_membres[idx].nb_messages > 0)
                    {
                        double diff = difftime(now, g_membres[idx].date_dernier_msg);
                        g_membres[idx].somme_intervalles += diff;
                    }
                    g_membres[idx].date_dernier_msg = now;
                    g_membres[idx].nb_messages++;
                }
            }

            if (!banned)
            {
                printf("GroupeISY : MSG de %s\n", msg.Emetteur);
                redistribuer_message(&msg, &addrCli);
            }
        }
        else if (strcmp(msg.Ordre, "CMD") == 0)
        {
            if (strncmp(msg.Emetteur, g_moderateurName, ISY_TAILLE_NOM) == 0)
            {
                char cmd[ISY_TAILLE_TEXTE];
                strncpy(cmd, msg.Texte, sizeof(cmd) - 1);
                cmd[sizeof(cmd) - 1] = '\0';

                MessageISY rep;
                memset(&rep, 0, sizeof(rep));
                strncpy(rep.Ordre, "RSP", ISY_TAILLE_ORDRE - 1);
                strncpy(rep.Emetteur, "Groupe", ISY_TAILLE_NOM - 1);
                rep.Texte[0] = '\0';

                if (strncasecmp(cmd, "list", 4) == 0)
                {
                    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                    {
                        if (g_membres[i].actif && !g_membres[i].banni)
                        {
                            /* --- FILTRAGE ---
                             * Si le nom contient "_Vue", c'est une fenêtre d'affichage, on cache.
                             */
                            if (strstr(g_membres[i].nom, "_Vue") != NULL)
                            {
                                continue;
                            }

                            char info[64];
                            snprintf(info, sizeof(info), "%s\n", g_membres[i].nom);
                            if (strlen(rep.Texte) + strlen(info) < ISY_TAILLE_TEXTE)
                                strcat(rep.Texte, info);
                        }
                    }
                    if (rep.Texte[0] == '\0')
                        strcpy(rep.Texte, "Vide (ou que des Vues)\n");
                }
                else if (strncasecmp(cmd, "stats", 5) == 0)
                {
                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "%-10s %-3s %-4s %-4s\n", "NOM", "NB", "TPS", "INT");
                    time_t now = time(NULL);

                    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                    {
                        if (g_membres[i].actif && !g_membres[i].banni)
                        {
                            /* --- FILTRAGE AUSSI POUR STATS --- */
                            if (strstr(g_membres[i].nom, "_Vue") != NULL)
                            {
                                continue;
                            }

                            double duree = difftime(now, g_membres[i].date_connexion);
                            double moy = 0.0;
                            if (g_membres[i].nb_messages > 1)
                                moy = g_membres[i].somme_intervalles / (g_membres[i].nb_messages - 1);

                            char line[128];
                            snprintf(line, sizeof(line), "%-10s %-3d %-4.0f %-4.1f\n",
                                     g_membres[i].nom, g_membres[i].nb_messages, duree, moy);

                            if (strlen(rep.Texte) + strlen(line) < ISY_TAILLE_TEXTE)
                                strcat(rep.Texte, line);
                        }
                    }
                }
                else if (strncasecmp(cmd, "delete ", 7) == 0 || strncasecmp(cmd, "ban ", 4) == 0)
                {
                    char *nameStart = strchr(cmd, ' ');
                    if (nameStart)
                    {
                        nameStart++;
                        int found = 0;
                        for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                        {
                            /* Pour le ban, on peut vouloir bannir tout le monde, donc on ne filtre pas "_Vue"
                               mais on compare le début du nom */
                            if (g_membres[i].actif && !g_membres[i].banni &&
                                strncasecmp(g_membres[i].nom, nameStart, strlen(nameStart)) == 0)
                            {
                                g_membres[i].banni = 1;
                                g_membres[i].actif = 0;
                                found = 1;
                            }
                        }
                        if (found)
                            snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Banni: %s", nameStart);
                        else
                            snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Inconnu: %s", nameStart);
                    }
                }

                sendto(sock_groupe, &rep, sizeof(rep), 0,
                       (struct sockaddr *)&addrCli, sizeof(addrCli));
            }
        }
    }
    fermer_socket_udp(sock_groupe);
    return 0;
}