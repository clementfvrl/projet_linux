/**********************************************
 * FICHIER : GroupeISY.c
 * Rôle   : Processus de groupe ISY
 *********************************************/

#include "commun.h"
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

static int sock_groupe = -1;
static int g_portGroupe = 0;

/* Nom du modérateur/gestionnaire du groupe */
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

/* * CORRECTION ICI : Gestion des doublons par Nom 
 */
static void ajouter_membre(const struct sockaddr_in *addr, const char *nom)
{
    /* 1. Si l'adresse exacte (IP + Port) est déjà connue, on ne fait rien */
    if (adresse_deja_connue(addr))
        return;

    /* 2. CORRECTION : Vérifier si le NOM existe déjà (changement de port du client ?) */
    if (nom && nom[0] != '\0') {
        for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
            /* On compare les noms (insensible à la casse) */
            if (g_membres[i].actif && strcasecmp(g_membres[i].nom, nom) == 0) {
                /* Le nom existe déjà ! C'est le même utilisateur avec un nouveau port.
                   On met à jour son adresse pour éviter le doublon. */
                g_membres[i].addr = *addr;
                
                /* Optionnel : On peut réinitialiser le statut 'banni' si besoin, 
                   ou le laisser tel quel. Ici on le laisse banni si il l'était. */
                if (g_membres[i].banni) {
                     printf("GroupeISY: Tentative de reconnexion d'un membre banni (%s)\n", nom);
                }
                return; /* On quitte, mise à jour faite */
            }
        }
    }

    /* 3. Sinon, c'est vraiment un nouveau membre, on cherche un slot vide */
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

    /* Initialisation stats */
    g_membres[idx].date_connexion = time(NULL);
    g_membres[idx].date_dernier_msg = 0;
    g_membres[idx].nb_messages = 0;
    g_membres[idx].somme_intervalles = 0.0;
}

/* Fonction corrigée pour éviter l'écho à l'expéditeur */
static void redistribuer_message(const MessageISY *msg,
                                 const struct sockaddr_in *addrEmetteur)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        if (g_membres[i].actif)
        {
            /* SI C'EST L'AUTEUR, ON PASSE (pas d'écho) */
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
        }
    }
}

static void arret_groupe(int sig)
{
    (void)sig;
    MessageISY msgFin;
    memset(&msgFin, 0, sizeof(msgFin));
    strncpy(msgFin.Ordre, "FIN", ISY_TAILLE_ORDRE - 1);
    strncpy(msgFin.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
    strncpy(msgFin.Texte, "Groupe dissous par le moderateur", ISY_TAILLE_TEXTE - 1);

    printf("\nGroupeISY : Fermeture demandée. Envoi de FIN aux membres...\n");

    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        if (g_membres[i].actif)
        {
            sendto(sock_groupe, &msgFin, sizeof(msgFin), 0,
                   (struct sockaddr *)&g_membres[i].addr,
                   sizeof(g_membres[i].addr));
        }
    }
    sleep(0.1); 
    fermer_socket_udp(sock_groupe);
    printf("GroupeISY : Arret terminé.\n");
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
        {
            perror("recvfrom GroupeISY");
            continue;
        }

        msg.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msg.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msg.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        if (strcmp(msg.Ordre, "REG") == 0)
        {
            ajouter_membre(&addrCli, msg.Emetteur);
            printf("GroupeISY(port %d) : nouveau client d'affichage inscrit\n", g_portGroupe);
        }
        else if (strcmp(msg.Ordre, "MSG") == 0)
        {
            ajouter_membre(&addrCli, msg.Emetteur);

            /* Mise à jour Stats */
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
                printf("GroupeISY(port %d) message recu : ", g_portGroupe);
                afficher_message_debug("Groupe", &msg);
                /* Cette fonction contient maintenant le correctif anti-écho */
                redistribuer_message(&msg, &addrCli);
            }
        }
        else if (strcmp(msg.Ordre, "CMD") == 0)
        {
            if (strncmp(msg.Emetteur, g_moderateurName, ISY_TAILLE_NOM) != 0)
            {
                /* Ignorer non-modérateurs */
            }
            else
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
                            if (strlen(rep.Texte) + strlen(g_membres[i].nom) + 2 < ISY_TAILLE_TEXTE)
                            {
                                strcat(rep.Texte, g_membres[i].nom);
                                strcat(rep.Texte, "\n");
                            }
                        }
                    }
                    if (rep.Texte[0] == '\0')
                        strcpy(rep.Texte, "Aucun membre\n");
                }
                else if (strncasecmp(cmd, "stats", 5) == 0)
                {
                    /* --- TABLEAU GLOBAL DES STATS --- */
                    /* En-tête du tableau */
                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "%-10s %-3s %-4s %-4s\n", "NOM", "NB", "TPS", "INT");

                    time_t now = time(NULL);
                    int count = 0;

                    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                    {
                        if (g_membres[i].actif && !g_membres[i].banni)
                        {
                            count++;
                            /* Calculs */
                            double duree = difftime(now, g_membres[i].date_connexion);
                            double moy = 0.0;
                            if (g_membres[i].nb_messages > 1)
                                moy = g_membres[i].somme_intervalles / (g_membres[i].nb_messages - 1);

                            /* Formatage ligne : Nom | Nb Msg | Temps (s) | Interv Moy (s) */
                            char line[128];
                            snprintf(line, sizeof(line), "%-10s %-3d %-4.0f %-4.1f\n",
                                     g_membres[i].nom,
                                     g_membres[i].nb_messages,
                                     duree,
                                     moy);

                            if (strlen(rep.Texte) + strlen(line) < ISY_TAILLE_TEXTE)
                            {
                                strcat(rep.Texte, line);
                            }
                        }
                    }
                    if (count == 0)
                        strcat(rep.Texte, "Aucun membre actif.\n");
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
                            if (g_membres[i].actif && !g_membres[i].banni &&
                                strncasecmp(g_membres[i].nom, nameStart, ISY_TAILLE_NOM) == 0)
                            {
                                g_membres[i].banni = 1;
                                g_membres[i].actif = 0;
                                snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Membre %s exclu", g_membres[i].nom);
                                found = 1;
                                break;
                            }
                        }
                        if (!found)
                            snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Membre %s introuvable", nameStart);
                    }
                    else
                    {
                        strncpy(rep.Texte, "Nom manquant", ISY_TAILLE_TEXTE - 1);
                    }
                }
                else
                {
                    strncpy(rep.Texte, "Cmd inconnue: list, stats, ban <nom>", ISY_TAILLE_TEXTE - 1);
                }

                /* Envoi de la réponse (CMD) */
                sendto(sock_groupe, &rep, sizeof(rep), 0,
                       (struct sockaddr *)&addrCli, sizeof(addrCli));
            }
        }
    }
    fermer_socket_udp(sock_groupe);
    return 0;
}