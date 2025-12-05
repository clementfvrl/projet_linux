/**********************************************
 * FICHIER : GroupeISY.c
 * Rôle   : Processus de groupe ISY
 *********************************************/

#include "commun.h"
#include <strings.h>
#include <string.h> 
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include <ctype.h> /* pour tolower dans l'analyse des commandes */

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
        if (!g_membres[i].actif) return i;
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
    if (adresse_deja_connue(addr)) return;

    /* Avant d'ajouter, on vérifie si le nom est sur liste noire (fiche 2.0) */
    if (nom && nom[0] != '\0')
    {
        /* On récupère le nom de base sans suffixe _Vue */
        char nomBase[ISY_TAILLE_NOM];
        strncpy(nomBase, nom, ISY_TAILLE_NOM - 1);
        nomBase[ISY_TAILLE_NOM - 1] = '\0';
        char *suf = strstr(nomBase, "_Vue");
        if (suf) *suf = '\0';

        for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
        {
            if (g_membres[i].banni && strcasecmp(g_membres[i].nom, nomBase) == 0)
            {
                /* Ce nom a été banni : on envoie un message 'BAN' au client */
                MessageISY banMsg;
                memset(&banMsg, 0, sizeof(banMsg));
                strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
                strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                strncpy(banMsg.Texte, "Vous êtes banni de ce groupe", ISY_TAILLE_TEXTE - 1);
                sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                       (const struct sockaddr *)addr, sizeof(*addr));
                return;
            }
        }
    }

    /* Si le nom existe déjà, on met à jour le port (cas du redémarrage client) */
    if (nom && nom[0] != '\0') {
        for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
            if (g_membres[i].actif && strcasecmp(g_membres[i].nom, nom) == 0) {
                g_membres[i].addr = *addr; 
                return; 
            }
        }
    }

    int idx = trouver_slot_membre();
    if (idx < 0) return;

    g_membres[idx].actif = 1;
    g_membres[idx].addr = *addr;
    if (nom && nom[0] != '\0')
    {
        strncpy(g_membres[idx].nom, nom, ISY_TAILLE_NOM - 1);
        g_membres[idx].nom[ISY_TAILLE_NOM - 1] = '\0';
    }
    /* Si le membre était banni précédemment, on conserve la valeur (ne pas remettre à 0). */
    /* Le champ banni est laissé tel quel pour permettre la reconduction du bannissement. */

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
            /* On n'envoie PAS à l'émetteur (Anti-Echo) */
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
            } else {
                envoyes++;
            }
        }
    }
    if(envoyes == 0) printf("⚠️ Personne n'a reçu le message (Avez-vous lancé l'Affichage ?)\n");
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
    sleep(1); /* Un petit délai pour s'assurer que les FIN partent */
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
    if (sock_groupe < 0) exit(EXIT_FAILURE);

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
        if (n < 0) continue;

        msg.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msg.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msg.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        if (strcmp(msg.Ordre, "REG") == 0)
        {
            char nomVue[ISY_TAILLE_NOM];
            snprintf(nomVue, ISY_TAILLE_NOM, "%s_Vue", msg.Emetteur);
            ajouter_membre(&addrCli, nomVue);
            printf("GroupeISY(port %d) : client affichage inscrit (%s)\n", g_portGroupe, nomVue);
        }
        else if (strcmp(msg.Ordre, "MSG") == 0)
        {
            ajouter_membre(&addrCli, msg.Emetteur);
            
            /* Mise à jour stats + anti-ban */
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
            /* === AJOUT POUR SECURISER LA CONNEXION === */
            /* On s'assure que celui qui envoie la commande est bien enregistré avec son port actuel */
            ajouter_membre(&addrCli, msg.Emetteur);

            char cmd[ISY_TAILLE_TEXTE];
            strncpy(cmd, msg.Texte, sizeof(cmd) - 1);
            cmd[sizeof(cmd) - 1] = '\0';

            MessageISY rep;
            memset(&rep, 0, sizeof(rep));
            strncpy(rep.Ordre, "RSP", ISY_TAILLE_ORDRE - 1);
            strncpy(rep.Emetteur, "Groupe", ISY_TAILLE_NOM - 1);
            rep.Texte[0] = '\0';

            /* Convertir la commande en minuscules pour comparaison insensible à la casse */
            char cmdLower[ISY_TAILLE_TEXTE];
            int lenCmd = 0;
            for (; lenCmd < (int)sizeof(cmdLower) - 1 && cmd[lenCmd] != '\0'; ++lenCmd)
            {
                cmdLower[lenCmd] = (char)tolower((unsigned char)cmd[lenCmd]);
            }
            cmdLower[lenCmd] = '\0';

            /* Commande QUIT : un membre souhaite quitter le groupe */
            if (strncmp(cmdLower, "quit", 4) == 0)
            {
                int idxQuit = trouver_index_membre(&addrCli);
                if (idxQuit >= 0)
                {
                    char quitter[ISY_TAILLE_NOM];
                    strncpy(quitter, g_membres[idxQuit].nom, ISY_TAILLE_NOM - 1);
                    quitter[ISY_TAILLE_NOM - 1] = '\0';
                    g_membres[idxQuit].actif = 0;
                    /* Diffuse le départ aux autres membres */
                    MessageISY notMsg;
                    memset(&notMsg, 0, sizeof(notMsg));
                    strncpy(notMsg.Ordre, "MSG", ISY_TAILLE_ORDRE - 1);
                    strncpy(notMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                    snprintf(notMsg.Texte, ISY_TAILLE_TEXTE, "%s a quitté le groupe", quitter);
                    /* Chiffrement du message SYSTEM avant diffusion */
                    cesar_chiffrer(notMsg.Texte);
                    redistribuer_message(&notMsg, &addrCli);
                    /* Réponse au membre qui quitte */
                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Vous avez quitté le groupe");
                }
                else
                {
                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Vous n'étiez pas membre du groupe");
                }
            }
            /* Commande HELP */
            else if (strncmp(cmdLower, "help", 4) == 0 || strcmp(cmdLower, "?") == 0)
            {
                snprintf(rep.Texte, ISY_TAILLE_TEXTE,
                         "Commandes disponibles :\n"
                         "  list          - lister les membres du groupe\n"
                         "  stats         - statistiques d'activité\n"
                         "  ban <nom>     - bannir un membre (alias delete)\n"
                         "  quit          - quitter le groupe\n"
                         "  help/?        - afficher cette aide");
            }
            /* Commande LIST */
            else if (strncasecmp(cmd, "list", 4) == 0)
            {
                for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                {
                    if (g_membres[i].actif && !g_membres[i].banni)
                    {
                        if (strstr(g_membres[i].nom, "_Vue") != NULL) continue;

                        char info[128];
                        char suffixe[32] = "";
                        if (strcasecmp(g_membres[i].nom, g_moderateurName) == 0) {
                            strcpy(suffixe, " (Gestionnaire)");
                        }
                        snprintf(info, sizeof(info), "%s%s\n", g_membres[i].nom, suffixe);
                        
                        if (strlen(rep.Texte) + strlen(info) < ISY_TAILLE_TEXTE)
                            strcat(rep.Texte, info);
                    }
                }
                if (rep.Texte[0] == '\0') strcpy(rep.Texte, "Vide\n");
            }
            /* Commande STATS */
            else if (strncasecmp(cmd, "stats", 5) == 0)
            {
                /* Prépare un tableau temporaire pour trier les membres */
                typedef struct {
                    char nom[ISY_TAILLE_NOM + 5];
                    int messages;
                    double duree;
                    double intervalle;
                } StatsEntry;
                StatsEntry entries[ISY_MAX_MEMBRES];
                int count = 0;
                time_t now = time(NULL);
                for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                {
                    if (g_membres[i].actif && !g_membres[i].banni)
                    {
                        if (strstr(g_membres[i].nom, "_Vue") != NULL) continue;
                        double duree = difftime(now, g_membres[i].date_connexion);
                        double moy = 0.0;
                        if (g_membres[i].nb_messages > 1)
                            moy = g_membres[i].somme_intervalles / (g_membres[i].nb_messages - 1);
                        StatsEntry e;
                        strcpy(e.nom, g_membres[i].nom);
                        if (strcasecmp(g_membres[i].nom, g_moderateurName) == 0) strcat(e.nom, "*");
                        e.messages = g_membres[i].nb_messages;
                        e.duree = duree;
                        e.intervalle = moy;
                        entries[count++] = e;
                    }
                }
                /* Tri croissant par nombre de messages puis par durée de connexion */
                for (int a = 0; a < count; ++a)
                {
                    for (int b = a + 1; b < count; ++b)
                    {
                        if (entries[b].messages < entries[a].messages ||
                            (entries[b].messages == entries[a].messages && entries[b].duree < entries[a].duree))
                        {
                            StatsEntry tmp = entries[a];
                            entries[a] = entries[b];
                            entries[b] = tmp;
                        }
                    }
                }
                snprintf(rep.Texte, ISY_TAILLE_TEXTE, "%-10s %-5s %-7s %-7s\n", "Nom", "Msgs", "Conn(s)", "Int(s)");
                for (int i = 0; i < count; ++i)
                {
                    char line[128];
                    snprintf(line, sizeof(line), "%-10s %-5d %-7.0f %-7.1f\n",
                                entries[i].nom, entries[i].messages, entries[i].duree, entries[i].intervalle);
                    if (strlen(rep.Texte) + strlen(line) < ISY_TAILLE_TEXTE)
                        strcat(rep.Texte, line);
                }
                strcat(rep.Texte, "(* = Gestionnaire)");
            }
            /* Commandes ADMIN : ban/delete */
            else if (strncasecmp(cmd, "delete ", 7) == 0 || strncasecmp(cmd, "ban ", 4) == 0)
            {
                if (strcasecmp(msg.Emetteur, g_moderateurName) != 0) {
                    strncpy(rep.Texte, "Erreur: Seul le gestionnaire peut exclure.", ISY_TAILLE_TEXTE - 1);
                }
                else {
                    char *nameStart = strchr(cmd, ' ');
                    if (nameStart)
                    {
                        nameStart++;
                        int found = 0;
                        for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                        {
                            if (g_membres[i].actif && !g_membres[i].banni &&
                                strncasecmp(g_membres[i].nom, nameStart, strlen(nameStart)) == 0)
                            {
                                if (strcasecmp(g_membres[i].nom, g_moderateurName) == 0) {
                                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Impossible d'exclure le gestionnaire !");
                                    found = 1; break;
                                }
                                /* Marquer comme banni et désactiver */
                                g_membres[i].banni = 1;
                                g_membres[i].actif = 0;
                                /* Notifier le membre banni */
                                MessageISY banMsg;
                                memset(&banMsg, 0, sizeof(banMsg));
                                strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
                                strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                                snprintf(banMsg.Texte, ISY_TAILLE_TEXTE, "Vous avez été banni du groupe");
                                sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                                       (struct sockaddr *)&g_membres[i].addr, sizeof(g_membres[i].addr));
                                found = 1;
                            }
                        }
                        if(found && rep.Texte[0] == '\0') snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Banni: %s", nameStart);
                        else if (!found) snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Inconnu: %s", nameStart);
                    }
                }
            }
            else
            {
                /* Commande inconnue : on renvoie l'aide rapide */
                strncpy(rep.Texte, "Commandes : list, stats, ban <nom>, quit, help", ISY_TAILLE_TEXTE - 1);
            }
            
            /* Envoi de la réponse uniquement à celui qui a fait la demande (Pas d'écho aux autres) */
            sendto(sock_groupe, &rep, sizeof(rep), 0,
                    (struct sockaddr *)&addrCli, sizeof(addrCli));
        }
    }
    fermer_socket_udp(sock_groupe);
    return 0;
}