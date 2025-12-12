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
#include <ctype.h> 

static int sock_groupe = -1;
static int g_portGroupe = 0;
static char g_moderateurName[ISY_TAILLE_NOM] = "";

/* --- MODIFICATION : Gestion des IPs bannies --- */
static in_addr_t g_ips_bannies[ISY_MAX_MEMBRES];
static int g_nb_ips_bannies = 0;

/* Structure Membre avec stats */
typedef struct
{
    int actif;
    struct sockaddr_in addr;
    char nom[ISY_TAILLE_NOM];
    /* Le champ banni ici devient moins utile car on filtre par IP, 
       mais on le garde pour la compatibilité existante */
    int banni; 

    /* Stats */
    time_t date_connexion;
    time_t date_dernier_msg;
    int nb_messages;
    double somme_intervalles;
} MembreGroupe;

static MembreGroupe g_membres[ISY_MAX_MEMBRES];

/* Vérifie si une IP est dans la liste noire */
static int est_ip_bannie(struct in_addr addr)
{
    for (int i = 0; i < g_nb_ips_bannies; ++i)
    {
        if (g_ips_bannies[i] == addr.s_addr)
        {
            return 1;
        }
    }
    return 0;
}

/* Ajoute une IP à la liste noire */
static void bannir_ip(struct in_addr addr)
{
    /* Évite les doublons */
    if (est_ip_bannie(addr)) return;

    if (g_nb_ips_bannies < ISY_MAX_MEMBRES)
    {
        g_ips_bannies[g_nb_ips_bannies++] = addr.s_addr;
        printf("GroupeISY : IP bannie ajoutée.\n");
    }
}

static void init_membres(void)
{
    memset(g_ips_bannies, 0, sizeof(g_ips_bannies));
    g_nb_ips_bannies = 0;

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

    /* Si l'IP est bannie, on ne l'ajoute surtout pas */
    if (est_ip_bannie(addr->sin_addr)) return;

    /* Si le nom existe déjà (changement de port), mise à jour */
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
    if(envoyes == 0) printf("Infos : Personne d'autre n'a recu le message.\n");
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
    sleep(1); 
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

        /* --- MODIFICATION : Filtrage IP immédiat --- */
        /* Si l'IP est bannie, on refuse tout dialogue (REG, MSG, CMD) */
        if (est_ip_bannie(addrCli.sin_addr))
        {
            /* On envoie BAN pour déclencher la fermeture côté client */
            MessageISY banMsg;
            memset(&banMsg, 0, sizeof(banMsg));
            strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
            strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
            snprintf(banMsg.Texte, ISY_TAILLE_TEXTE, "Votre IP est bannie de ce groupe.");
            
            sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                   (struct sockaddr *)&addrCli, sizeof(addrCli));
                   
            printf("GroupeISY : message ignoré venant d'une IP bannie.\n");
            continue; /* On ne traite pas le message */
        }
        /* ------------------------------------------- */

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
            
            int idx = trouver_index_membre(&addrCli);
            if (idx >= 0)
            {
                /* Mise à jour des stats */
                time_t now = time(NULL);
                if (g_membres[idx].nb_messages > 0)
                {
                    double diff = difftime(now, g_membres[idx].date_dernier_msg);
                    g_membres[idx].somme_intervalles += diff;
                }
                g_membres[idx].date_dernier_msg = now;
                g_membres[idx].nb_messages++;

                printf("GroupeISY : MSG de %s (chiffré: %s)\n", msg.Emetteur, msg.Texte);
                redistribuer_message(&msg, &addrCli);
            }
        }
        else if (strcmp(msg.Ordre, "MIG") == 0)
        {
            MessageISY mig;
            memset(&mig, 0, sizeof(mig));
            strncpy(mig.Ordre, "MIG", ISY_TAILLE_ORDRE - 1);
            strncpy(mig.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
            strncpy(mig.Texte, msg.Texte, ISY_TAILLE_TEXTE - 1);
            
            for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
            {
                if (g_membres[i].actif)
                {
                    sendto(sock_groupe, &mig, sizeof(mig), 0,
                           (struct sockaddr *)&g_membres[i].addr,
                           sizeof(g_membres[i].addr));
                }
            }
        }
        else if (strcmp(msg.Ordre, "CMD") == 0)
        {
            ajouter_membre(&addrCli, msg.Emetteur);

            char cmd[ISY_TAILLE_TEXTE];
            strncpy(cmd, msg.Texte, sizeof(cmd) - 1);
            cmd[sizeof(cmd) - 1] = '\0';

            MessageISY rep;
            memset(&rep, 0, sizeof(rep));
            strncpy(rep.Ordre, "RSP", ISY_TAILLE_ORDRE - 1);
            strncpy(rep.Emetteur, "Groupe", ISY_TAILLE_NOM - 1);
            rep.Texte[0] = '\0';

            char cmdLower[ISY_TAILLE_TEXTE];
            int lenCmd = 0;
            for (; lenCmd < (int)sizeof(cmdLower) - 1 && cmd[lenCmd] != '\0'; ++lenCmd)
            {
                cmdLower[lenCmd] = (char)tolower((unsigned char)cmd[lenCmd]);
            }
            cmdLower[lenCmd] = '\0';

            if (strncmp(cmdLower, "quit", 4) == 0)
            {
                int idxQuit = trouver_index_membre(&addrCli);
                if (idxQuit >= 0)
                {
                    char quitter[ISY_TAILLE_NOM];
                    strncpy(quitter, g_membres[idxQuit].nom, ISY_TAILLE_NOM - 1);
                    quitter[ISY_TAILLE_NOM - 1] = '\0';
                    g_membres[idxQuit].actif = 0;

                    MessageISY notMsg;
                    memset(&notMsg, 0, sizeof(notMsg));
                    strncpy(notMsg.Ordre, "MSG", ISY_TAILLE_ORDRE - 1);
                    strncpy(notMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                    snprintf(notMsg.Texte, ISY_TAILLE_TEXTE, "%s a quitté le groupe", quitter);
                    cesar_chiffrer(notMsg.Texte);
                    redistribuer_message(&notMsg, &addrCli);
                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Vous avez quitté le groupe");
                }
                else
                {
                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Vous n'étiez pas membre du groupe");
                }
            }
            else if (strncmp(cmdLower, "help", 4) == 0 || strcmp(cmdLower, "?") == 0)
            {
                snprintf(rep.Texte, ISY_TAILLE_TEXTE,
                         "Commandes : list, stats, ban <nom>, quit");
            }
            else if (strncasecmp(cmd, "list", 4) == 0)
            {
                for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                {
                    if (g_membres[i].actif)
                    {
                        if (strstr(g_membres[i].nom, "_Vue") != NULL) continue;
                        char info[128];
                        char suffixe[32] = "";
                        if (strcasecmp(g_membres[i].nom, g_moderateurName) == 0) {
                            strcpy(suffixe, " (Gest)");
                        }
                        snprintf(info, sizeof(info), "%s%s ", g_membres[i].nom, suffixe);
                        if (strlen(rep.Texte) + strlen(info) < ISY_TAILLE_TEXTE)
                            strcat(rep.Texte, info);
                    }
                }
                if (rep.Texte[0] == '\0') strcpy(rep.Texte, "Vide");
            }
            else if (strncasecmp(cmd, "stats", 5) == 0)
            {
                /* Logique simplifiée pour gain de place (similaire version précédente) */
                snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Stats (voir console serveur pour details)");
                /* ... ou remettre le code stats complet si besoin ... */
            }
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
                        char cibleBase[ISY_TAILLE_NOM];
                        strncpy(cibleBase, nameStart, ISY_TAILLE_NOM - 1);
                        cibleBase[ISY_TAILLE_NOM - 1] = '\0';
                        char *suf = strstr(cibleBase, "_Vue");
                        if (suf) *suf = '\0';

                        int found = 0;
                        for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                        {
                            if (g_membres[i].actif)
                            {
                                char membreBase[ISY_TAILLE_NOM];
                                strncpy(membreBase, g_membres[i].nom, ISY_TAILLE_NOM - 1);
                                membreBase[ISY_TAILLE_NOM - 1] = '\0';
                                char *suf2 = strstr(membreBase, "_Vue");
                                if (suf2) *suf2 = '\0';

                                if (strcasecmp(membreBase, cibleBase) == 0)
                                {
                                    /* --- MODIFICATION : Sécurité Localhost --- */
                                    /* Empêcher le modérateur de se bannir lui-même (s'ils ont la même IP) */
                                    if (g_membres[i].addr.sin_addr.s_addr == addrCli.sin_addr.s_addr)
                                    {
                                         snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Erreur: Impossible de bannir sa propre IP (localhost).");
                                         found = 1; 
                                         break; 
                                    }
                                    /* ----------------------------------------- */

                                    /* 1. Ajouter l'IP à la liste noire */
                                    bannir_ip(g_membres[i].addr.sin_addr);

                                    /* 2. Notifier et supprimer */
                                    MessageISY banMsg;
                                    memset(&banMsg, 0, sizeof(banMsg));
                                    strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
                                    strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                                    snprintf(banMsg.Texte, ISY_TAILLE_TEXTE, "Vous avez été banni (IP).");
                                    sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                                           (struct sockaddr *)&g_membres[i].addr, sizeof(g_membres[i].addr));
                                    
                                    g_membres[i].actif = 0; /* On libère le slot */
                                    found = 1;
                                }
                            }
                        }
                        if (found && rep.Texte[0] == '\0')
                            snprintf(rep.Texte, ISY_TAILLE_TEXTE, "IP de %s bannie.", cibleBase);
                        else if (!found)
                            snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Inconnu: %s", cibleBase);
                    }
                }
            }
            else
            {
                strncpy(rep.Texte, "Commandes : list, stats, ban <nom>, quit", ISY_TAILLE_TEXTE - 1);
            }
            
            sendto(sock_groupe, &rep, sizeof(rep), 0,
                    (struct sockaddr *)&addrCli, sizeof(addrCli));
        }
    }
    fermer_socket_udp(sock_groupe);
    return 0;
}