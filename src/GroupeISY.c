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

/* Flag pour arrêt gracieux (async-signal-safe) */
static volatile sig_atomic_t g_shutdown_requested = 0;

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

/*
 * Vérifie si un nom de membre (sans suffixe _Vue) est sur la liste noire.
 * Cette fonction est utilisée pour empêcher un membre banni de réintégrer
 * le groupe avec une nouvelle adresse. Elle ignore la casse et le suffixe
 * "_Vue" afin de traiter uniformément les noms des clients et des
 * processus d'affichage.
 */
static int est_banni_par_nom(const char *nom)
{
    if (!nom || nom[0] == '\0') {
        return 0;
    }
    /* Extraire le nom de base sans le suffixe _Vue */
    char nomBase[ISY_TAILLE_NOM];
    strncpy(nomBase, nom, ISY_TAILLE_NOM - 1);
    nomBase[ISY_TAILLE_NOM - 1] = '\0';
    char *suf = strstr(nomBase, "_Vue");
    if (suf) *suf = '\0';
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
        if (g_membres[i].banni && strcasecmp(g_membres[i].nom, nomBase) == 0) {
            return 1;
        }
    }
    return 0;
}

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

/*
 * Retourne l'indice d'un slot libre pour un nouveau membre.
 * On ne réutilise pas les entrées marquées comme bannies afin de conserver
 * une liste noire persistante. Une entrée est considérée libre seulement
 * si elle est inactive et non bannie. Si aucune entrée valable n'est trouvée,
 * renvoie -1.
 */
static int trouver_slot_membre(void)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
    {
        /* Un slot est disponible si inactif et non bannie */
        if (!g_membres[i].actif && !g_membres[i].banni)
        {
            return i;
        }
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

    /* Ne pas ajouter d'entrée pour l'utilisateur SYSTEM. Les notifications
     * système (messages envoyés par le serveur) utilisent l'émetteur
     * "SYSTEM". Dans ces cas, il ne faut pas considérer SYSTEM comme un
     * membre du groupe. Cela évite d'ajouter des entrées parasites
     * lorsque des messages système sont diffusés (par exemple lors
     * d'une fusion ou d'une notification). */
    if (nom && strcmp(nom, "SYSTEM") == 0) {
        return;
    }

    if (adresse_deja_connue(addr)) return;

    /* Avant toute opération, on vérifie si le nom est banni (fiche 2.0) */
    if (nom && nom[0] != '\0')
    {
        /* On récupère le nom de base sans suffixe _Vue */
        char nomBase[ISY_TAILLE_NOM];
        strncpy(nomBase, nom, ISY_TAILLE_NOM - 1);
        nomBase[ISY_TAILLE_NOM - 1] = '\0';
        char *suf = strstr(nomBase, "_Vue");
        if (suf) *suf = '\0';

        /* Vérification de bannissement AVANT toute autre opération */
        for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
        {
            if (g_membres[i].banni && strcasecmp(g_membres[i].nom, nomBase) == 0)
            {
                /* Ce nom a été banni : on envoie un message 'BAN' et on refuse la connexion */
                MessageISY banMsg;
                memset(&banMsg, 0, sizeof(banMsg));
                strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
                strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                strncpy(banMsg.Texte, "Vous êtes banni de ce groupe", ISY_TAILLE_TEXTE - 1);
                sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                       (const struct sockaddr *)addr, sizeof(*addr));
                printf("GroupeISY : Tentative de reconnexion refusée pour l'utilisateur banni '%s'\n", nomBase);
                return; /* ARRÊT COMPLET - aucune mise à jour effectuée */
            }
        }
    }

    /* Si le nom existe déjà ET n'est pas banni, on met à jour l'adresse (cas du redémarrage client) */
    if (nom && nom[0] != '\0') {
        for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
            if (g_membres[i].actif && !g_membres[i].banni && strcasecmp(g_membres[i].nom, nom) == 0) {
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
    /* Utilise uniquement des opérations async-signal-safe */
    g_shutdown_requested = 1;
}

/* ==== MAIN ==== */
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: GroupeISY <portGroupe> <moderateurName>\n");
        exit(EXIT_FAILURE);
    }

    /* Validation du port avec strtol au lieu de atoi */
    char *endptr;
    long port_long = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || port_long < 1024 || port_long > 65535) {
        fprintf(stderr, "Erreur: Port invalide '%s' (doit etre entre 1024 et 65535)\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    g_portGroupe = (int)port_long;

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
        fprintf(stderr, "Impossible de se lier au port %d\n", g_portGroupe);
        fermer_socket_udp(sock_groupe);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addrCli;
    socklen_t lenCli = sizeof(addrCli);
    MessageISY msg;

    while (!g_shutdown_requested)
    {
        ssize_t n = recvfrom(sock_groupe, &msg, sizeof(msg), 0,
                             (struct sockaddr *)&addrCli, &lenCli);
        if (n < 0)
        {
            if (errno == EINTR && g_shutdown_requested) break;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Timeout atteint, pas d'erreur, on continue */
                continue;
            }
            perror("recvfrom GroupeISY");
            continue;
        }

        /* Validation de la taille du message reçu */
        if ((size_t)n != sizeof(msg)) {
            fprintf(stderr, "GroupeISY: Message incomplet recu (%zd octets au lieu de %zu), ignore\n",
                    n, sizeof(msg));
            continue;
        }

        msg.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msg.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msg.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        /* Validation du champ Ordre
         * L'ordre 'REP' (réponse de fusion envoyée par le serveur) n'est pas
         * reconnu par valider_ordre(). On le traite ici afin de propager
         * l'information aux membres. Les autres ordres invalides sont ignorés.
         */
        if (!valider_ordre(msg.Ordre)) {
            if (strcmp(msg.Ordre, "REP") == 0) {
                /* Diffusion du message de redirection à tous les membres actifs. */
                printf("GroupeISY(port %d) : message de fusion reçu, diffusion de la redirection\n", g_portGroupe);
                redistribuer_message(&msg, &addrCli);
                continue;
            }
            fprintf(stderr, "GroupeISY: Ordre invalide recu: '%s', ignore\n", msg.Ordre);
            continue;
        }

        if (strcmp(msg.Ordre, "REG") == 0)
        {
            /* Vérifier si l'utilisateur de base est banni avant d'accepter l'inscription d'affichage */
            int is_banned = 0;
            if (msg.Emetteur[0] != '\0')
            {
                for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                {
                    if (g_membres[i].banni && strcasecmp(g_membres[i].nom, msg.Emetteur) == 0)
                    {
                        is_banned = 1;
                        printf("GroupeISY(port %d) : REG refusé pour l'utilisateur banni '%s'\n", g_portGroupe, msg.Emetteur);
                        /* Envoyer un message BAN à l'affichage aussi */
                        MessageISY banMsg;
                        memset(&banMsg, 0, sizeof(banMsg));
                        strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
                        strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                        strncpy(banMsg.Texte, "Vous êtes banni de ce groupe", ISY_TAILLE_TEXTE - 1);
                        sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                               (const struct sockaddr *)&addrCli, sizeof(addrCli));
                        break;
                    }
                }
            }

            if (!is_banned)
            {
                char nomVue[ISY_TAILLE_NOM];
                snprintf(nomVue, ISY_TAILLE_NOM, "%s_Vue", msg.Emetteur);
                ajouter_membre(&addrCli, nomVue);
                printf("GroupeISY(port %d) : client affichage inscrit (%s)\n", g_portGroupe, nomVue);
            }
        }
        else if (strcmp(msg.Ordre, "MSG") == 0)
        {
            /* Un membre envoie un message. On l'ajoute si nécessaire. */
            ajouter_membre(&addrCli, msg.Emetteur);

            /*
             * Détermination du statut de bannissement :
             * - si l'adresse est connue, on utilise le champ banni associé ;
             * - sinon, on vérifie si son nom figure déjà sur la liste noire.
             * Cela évite qu'un utilisateur banni réutilise une nouvelle adresse
             * pour contourner l'exclusion.
             */
            int idx = trouver_index_membre(&addrCli);
            int banned = 0;
            if (idx >= 0)
            {
                banned = g_membres[idx].banni;
                if (!banned)
                {
                    /* Mise à jour des statistiques uniquement pour les membres non bannis */
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
            else
            {
                /* L'adresse n'est pas enregistrée : vérifier si le nom est banni */
                if (est_banni_par_nom(msg.Emetteur)) {
                    banned = 1;
                }
            }

            if (!banned)
            {
                /* Affichage du message chiffré pour démonstration */
                printf("GroupeISY : MSG de %s (chiffré: %s)\n", msg.Emetteur, msg.Texte);
                redistribuer_message(&msg, &addrCli);
            }
            else
            {
                /* Réponse explicite au membre banni pour lui rappeler son exclusion */
                MessageISY banMsg;
                memset(&banMsg, 0, sizeof(banMsg));
                strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
                strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                snprintf(banMsg.Texte, ISY_TAILLE_TEXTE, "Vous avez été banni du groupe");
                sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                       (struct sockaddr *)&addrCli, sizeof(addrCli));
            }
        }
        else if (strcmp(msg.Ordre, "REP") == 0)
        {
            /*
             * Message de redirection suite à une fusion (envoyé par le serveur).
             * On diffuse ce message à tous les membres actifs du groupe afin qu'ils
             * puissent réagir côté client. L'émetteur (serveur) n'est pas dans
             * g_membres, donc il ne sera pas exclu par redistribuer_message.
             */
            printf("GroupeISY(port %d) : message de fusion reçu, diffusion de la redirection\n", g_portGroupe);
            redistribuer_message(&msg, &addrCli);
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
                size_t current_len = 0;
                /*
                 * Génère la liste des membres en consolidant les entrées
                 * client et affichage (suffixe _Vue) sous le même nom de base.
                 * On ignore les membres bannis et on ne liste chaque nom qu'une seule
                 * fois. Cela permet de faire apparaître immédiatement les
                 * membres provenant d'un groupe fusionné, même si seul leur
                 * affichage est connecté (nom avec suffixe _Vue). Les noms
                 * système ne sont jamais listés comme membres.
                 */
                char addedBases[ISY_MAX_MEMBRES][ISY_TAILLE_NOM];
                int addedCount = 0;
                for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                {
                    if (g_membres[i].actif && !g_membres[i].banni)
                    {
                        /* Exclure l'entrée si le nom est SYSTEM (notifications) */
                        if (strcasecmp(g_membres[i].nom, "SYSTEM") == 0) {
                            continue;
                        }
                        /* Extraire le nom de base en retirant le suffixe _Vue s'il existe */
                        char baseName[ISY_TAILLE_NOM];
                        strncpy(baseName, g_membres[i].nom, ISY_TAILLE_NOM - 1);
                        baseName[ISY_TAILLE_NOM - 1] = '\0';
                        char *suf = strstr(baseName, "_Vue");
                        if (suf) *suf = '\0';
                        /* Vérifier si cette base a déjà été ajoutée */
                        int already = 0;
                        for (int j = 0; j < addedCount; ++j)
                        {
                            if (strcasecmp(addedBases[j], baseName) == 0)
                            {
                                already = 1;
                                break;
                            }
                        }
                        if (already) continue;

                        /* Ajouter le nom de base à la liste des ajoutés */
                        strncpy(addedBases[addedCount], baseName, ISY_TAILLE_NOM - 1);
                        addedBases[addedCount][ISY_TAILLE_NOM - 1] = '\0';
                        addedCount++;

                        /* Déterminer le suffixe si c'est le gestionnaire */
                        char suffixe[32] = "";
                        if (strcasecmp(baseName, g_moderateurName) == 0) {
                            strcpy(suffixe, " (Gestionnaire)");
                        }
                        char info[128];
                        snprintf(info, sizeof(info), "%s%s\n", baseName, suffixe);
                        size_t info_len = strlen(info);
                        if (current_len + info_len + 1 <= ISY_TAILLE_TEXTE) {
                            strcpy(rep.Texte + current_len, info);
                            current_len += info_len;
                        } else {
                            break; /* buffer plein */
                        }
                    }
                }
                if (current_len == 0) strcpy(rep.Texte, "Vide\n");
            }
            /* Commande STATS */
            else if (strncasecmp(cmd, "stats", 5) == 0)
            {
                /*
                 * Prépare un tableau pour consolider les statistiques des membres
                 * en fusionnant les entrées client et affichage. Chaque baseName
                 * apparaît une seule fois. Les compteurs de messages et
                 * durées sont cumulés pour refléter l'activité totale d'un
                 * membre quelle que soit l'adresse utilisée. Les entrées SYSTEM
                 * sont ignorées.
                 */
                typedef struct {
                    char nom[ISY_TAILLE_NOM + 5];
                    int messages;
                    time_t date_connexion_min;
                    time_t somme_dernier_msg;
                    int count_dernier_msg;
                    double somme_intervalles;
                    int total_messages;
                } StatsEntry;
                StatsEntry entries[ISY_MAX_MEMBRES];
                int count = 0;
                time_t now = time(NULL);
                for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                {
                    if (g_membres[i].banni) continue;
  if (g_membres[i].nom[0] == '\0') continue;
if (strcasecmp(g_membres[i].nom, "SYSTEM") == 0) continue;
if (g_membres[i].banni) continue;
                    if (strcasecmp(g_membres[i].nom, "SYSTEM") == 0) continue;
                    /* Extraire le nom de base (sans _Vue) */
                    char baseName[ISY_TAILLE_NOM];
                    strncpy(baseName, g_membres[i].nom, ISY_TAILLE_NOM - 1);
                    baseName[ISY_TAILLE_NOM - 1] = '\0';
                    char *suf = strstr(baseName, "_Vue");
                    if (suf) *suf = '\0';

                    /* Chercher si ce baseName existe déjà dans entries */
                    int idx = -1;
                    for (int j = 0; j < count; ++j)
                    {
                        if (strcasecmp(entries[j].nom, baseName) == 0) {
                            idx = j;
                            break;
                        }
                    }
                    if (idx < 0)
                    {
                        /* Nouvelle entrée */
                        idx = count++;
                        strncpy(entries[idx].nom, baseName, ISY_TAILLE_NOM - 1);
                        entries[idx].nom[ISY_TAILLE_NOM - 1] = '\0';
                        entries[idx].messages = 0;
                        entries[idx].date_connexion_min = now;
                        entries[idx].somme_dernier_msg = 0;
                        entries[idx].count_dernier_msg = 0;
                        entries[idx].somme_intervalles = 0.0;
                        entries[idx].total_messages = 0;
                    }
                    /* Cumuler les stats de ce membre */
                    entries[idx].total_messages += g_membres[i].nb_messages;
                    /* date_connexion_min : on prend le plus petit pour la durée de connexion */
                    if (g_membres[i].date_connexion != 0 && g_membres[i].date_connexion < entries[idx].date_connexion_min)
                        entries[idx].date_connexion_min = g_membres[i].date_connexion;
                    /* somme_dernier_msg sert à calculer l'intervalle moyen plus tard */
                    if (g_membres[i].nb_messages > 1)
                    {
                        entries[idx].somme_intervalles += g_membres[i].somme_intervalles;
                        entries[idx].count_dernier_msg += (g_membres[i].nb_messages - 1);
                    }
                }
                /* Construire une table triée par nombre de messages et durée */
                /* Calculer les champs messages/duree/intervalle */
                typedef struct {
                    char nom[ISY_TAILLE_NOM + 5];
                    int messages;
                    double duree;
                    double intervalle;
                } DisplayEntry;
                DisplayEntry disp[ISY_MAX_MEMBRES];
                int dispCount = 0;
                for (int i = 0; i < count; ++i)
                {
                    DisplayEntry e;
                    /* Ajoute un astérisque pour le gestionnaire */
                    strcpy(e.nom, entries[i].nom);
                    if (strcasecmp(entries[i].nom, g_moderateurName) == 0) strcat(e.nom, "*");
                    e.messages = entries[i].total_messages;
                    e.duree = difftime(now, entries[i].date_connexion_min);
                    /* Moyenne des intervalles si au moins un intervalle */
                    if (entries[i].count_dernier_msg > 0)
                        e.intervalle = entries[i].somme_intervalles / entries[i].count_dernier_msg;
                    else
                        e.intervalle = 0.0;
                    disp[dispCount++] = e;
                }
                /* Tri croissant par nombre de messages puis par durée de connexion */
                for (int a = 0; a < dispCount; ++a)
                {
                    for (int b = a + 1; b < dispCount; ++b)
                    {
                        if (disp[b].messages < disp[a].messages ||
                            (disp[b].messages == disp[a].messages && disp[b].duree < disp[a].duree))
                        {
                            DisplayEntry tmp = disp[a];
                            disp[a] = disp[b];
                            disp[b] = tmp;
                        }
                    }
                }
                size_t current_len = snprintf(rep.Texte, ISY_TAILLE_TEXTE, "%-10s %-5s %-7s %-7s\n", "Nom", "Msgs", "Conn(s)", "Int(s)");
                for (int i = 0; i < dispCount; ++i)
                {
                    char line[128];
                    snprintf(line, sizeof(line), "%-10s %-5d %-7.0f %-7.1f\n",
                                disp[i].nom, disp[i].messages, disp[i].duree, disp[i].intervalle);
                    size_t line_len = strlen(line);
                    if (current_len + line_len + 1 <= ISY_TAILLE_TEXTE) {
                        strcpy(rep.Texte + current_len, line);
                        current_len += line_len;
                    } else {
                        break; /* buffer plein */
                    }
                }
                /* Footer si place disponible */
                const char *footer = "(* = Gestionnaire)";
                if (current_len + strlen(footer) + 1 <= ISY_TAILLE_TEXTE) {
                    strcpy(rep.Texte + current_len, footer);
                }
            }
            /* Commandes ADMIN : ban/delete */
            else if (strncasecmp(cmd, "delete ", 7) == 0 || strncasecmp(cmd, "ban ", 4) == 0)
            {
                if (strcasecmp(msg.Emetteur, g_moderateurName) != 0) {
                    strncpy(rep.Texte, "Erreur: Seul le gestionnaire peut exclure.", ISY_TAILLE_TEXTE - 1);
                }
                else {
                /*
                 * Lors d'un bannissement, on doit identifier précisément le membre
                 * visé par son nom complet (sans suffixe _Vue) et non par un
                 * simple préfixe. Cela permet d'éviter de bannir des membres
                 * dont le nom commence de la même façon. On retire donc le
                 * suffixe _Vue du paramètre, puis on parcourt les membres
                 * actifs pour comparer les noms de base en ignorant la casse.
                 */
                /*
                 * La commande ban/delete prend un nom en argument. On extrait ce nom, on
                 * retire éventuellement le suffixe _Vue et on bannit toutes les
                 * occurrences correspondantes (client et affichage). Le
                 * gestionnaire ne peut pas être exclu. On ne s'arrête pas au
                 * premier trouvé afin de notifier toutes les adresses du membre.
                 */
                char *nameStart = strchr(cmd, ' ');
                if (nameStart)
                {
                    nameStart++;
                    /* Récupérer le nom de base sans suffixe _Vue */
                    char cibleBase[ISY_TAILLE_NOM];
                    strncpy(cibleBase, nameStart, ISY_TAILLE_NOM - 1);
                    cibleBase[ISY_TAILLE_NOM - 1] = '\0';
                    char *suf = strstr(cibleBase, "_Vue");
                    if (suf) *suf = '\0';

                    int found = 0;
                    for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
                    {
                        /* Ignorer déjà bannis pour éviter de renvoyer plusieurs fois BAN */
                        if (!g_membres[i].banni)
                        {
                            /* Comparer le nom de base (sans _Vue) de l'entrée courante */
                            char membreBase[ISY_TAILLE_NOM];
                            strncpy(membreBase, g_membres[i].nom, ISY_TAILLE_NOM - 1);
                            membreBase[ISY_TAILLE_NOM - 1] = '\0';
                            char *suf2 = strstr(membreBase, "_Vue");
                            if (suf2) *suf2 = '\0';
                            if (strcasecmp(membreBase, cibleBase) == 0)
                            {
                                /* Ne pas bannir le gestionnaire */
                                if (strcasecmp(membreBase, g_moderateurName) == 0)
                                {
                                    /* Message d'erreur si on essaie de bannir le modérateur */
                                    snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Impossible d'exclure le gestionnaire !");
                                    /* Ne pas marquer comme trouvé ici pour continuer à traiter les autres occurrences */
                                    continue;
                                }
                                /* Bannir l'entrée */
                                g_membres[i].banni = 1;
                                g_membres[i].actif = 0;
                                /* Remplacer son nom par la base pour une détection future */
                                strncpy(g_membres[i].nom, membreBase, ISY_TAILLE_NOM - 1);
                                g_membres[i].nom[ISY_TAILLE_NOM - 1] = '\0';
                                /* Notifier le membre (client ou affichage) si une adresse existe */
                                if (g_membres[i].addr.sin_port != 0)
                                {
                                    MessageISY banMsg;
                                    memset(&banMsg, 0, sizeof(banMsg));
                                    strncpy(banMsg.Ordre, "BAN", ISY_TAILLE_ORDRE - 1);
                                    strncpy(banMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                                    snprintf(banMsg.Texte, ISY_TAILLE_TEXTE, "Vous avez été banni du groupe");
                                    sendto(sock_groupe, &banMsg, sizeof(banMsg), 0,
                                           (struct sockaddr *)&g_membres[i].addr, sizeof(g_membres[i].addr));
                                }
                                found = 1;
                            }
                        }
                    }
                    if (found && rep.Texte[0] == '\0')
                    {
                        /* Confirmation pour le gestionnaire */
                        snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Banni: %s", cibleBase);
                    }
                    else if (!found)
                    {
                        snprintf(rep.Texte, ISY_TAILLE_TEXTE, "Inconnu: %s", cibleBase);
                    }
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

    /* Nettoyage gracieux après arrêt demandé */
    if (g_shutdown_requested)
    {
        MessageISY msgFin;
        memset(&msgFin, 0, sizeof(msgFin));
        strncpy(msgFin.Ordre, "FIN", ISY_TAILLE_ORDRE - 1);
        strncpy(msgFin.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
        strncpy(msgFin.Texte, "Groupe dissous", ISY_TAILLE_TEXTE - 1);

        for (int i = 0; i < ISY_MAX_MEMBRES; ++i)
        {
            if (g_membres[i].actif)
            {
                if (sendto(sock_groupe, &msgFin, sizeof(msgFin), 0,
                       (struct sockaddr *)&g_membres[i].addr,
                       sizeof(g_membres[i].addr)) < 0) {
                    perror("sendto FIN");
                }
            }
        }
        /* Petit délai pour s'assurer que les FIN partent */
        struct timespec delay = {0, 100000000}; /* 100ms */
        nanosleep(&delay, NULL);
    }

    fermer_socket_udp(sock_groupe);
    return 0;
}