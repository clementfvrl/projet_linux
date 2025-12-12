/**********************************************
 * FICHIER : ClientISY.c
 * Rôle   : Client ISY (menu + logique)
 *********************************************/

#include "commun.h"

#include <ctype.h> /* pour tolower dans le mode commande */

static int sock_client = -1;
static struct sockaddr_in addrServeur;

/* Groupe courant (on simplifie à un seul groupe à la fois) */
static char g_nomGroupeActif[ISY_TAILLE_TEXTE] = "";
static int g_portGroupeActif = 0;
static pid_t g_pidAffichage = 0;
static char g_nomUtilisateur[ISY_TAILLE_NOM] = "user";

/*
 * Prototypes internes supplémentaires.
 * Ces déclarations anticipées évitent les déclarations implicites lors de
 * l'utilisation des fonctions avant leur définition. Elles sont marquées
 * 'static' pour conserver la visibilité limitée au fichier.
 */
void envoyer_message_serveur(const MessageISY *msgReq, MessageISY *msgRep);
void lancer_affichage(int portGroupe);
static void lancer_affichage_ex(int portGroupe, const char *nomClient);
void arreter_affichage(void);
static void rejoindre_groupe_auto(const char *nomGroupe, int portSuggestion);

/* ==== Fonctions utilitaires client ==== */
/* Vider le buffer stdin pour éviter les caractères résiduels */
static void vider_stdin(void);

/* Prototypes des actions */
static void action_creer_groupe(void);
static void action_lister_groupes(void);
static void action_rejoindre_groupe(void);
static void action_dialoguer_groupe(void);
static void action_quitter_groupe(void);
static void action_supprimer_groupe(void);
static void action_fusion_groupes(void);

void envoyer_message_serveur(const MessageISY *msgReq,
                                    MessageISY *msgRep)
{
    socklen_t lenServ = sizeof(addrServeur);
    ssize_t n = sendto(sock_client, msgReq, sizeof(*msgReq), 0,
                       (struct sockaddr *)&addrServeur, lenServ);
    if (n < 0)
    {
        perror("sendto Client->Serveur");
        return;
    }

    n = recvfrom(sock_client, msgRep, sizeof(*msgRep), 0,
                 (struct sockaddr *)&addrServeur, &lenServ);
    if (n < 0)
    {
        perror("recvfrom Serveur->Client");
        return;
    }

    msgRep->Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
    msgRep->Emetteur[ISY_TAILLE_NOM - 1] = '\0';
    msgRep->Texte[ISY_TAILLE_TEXTE - 1] = '\0';
}

/* Lance AffichageISY sur le port du groupe */
/* Notez l'ajout de 'const char *nomClient' dans les paramètres */
/*
 * Lancement d'une fenêtre d'affichage pour un groupe.
 * Cette fonction interne prend explicitement le nom du client à passer à
 * AffichageISY. L'ancienne fonction lancer_affichage(int) est conservée en
 * wrapper pour maintenir la compatibilité avec les prototypes éventuels
 * définis dans les en-têtes. Les paramètres sont :
 *  - portGroupe : le port UDP du groupe à écouter
 *  - nomClient  : le nom d'utilisateur à afficher dans la fenêtre
 */
static void lancer_affichage_ex(int portGroupe, const char *nomClient)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork AffichageISY");
        return;
    }

    if (pid == 0)
    {
        /* Processus fils */
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", portGroupe);

        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            exit(EXIT_FAILURE);
        }

        /* Change to working directory first */
        if (chdir(cwd) < 0)
        {
            perror("chdir");
            exit(EXIT_FAILURE);
        }

        /* Close inherited socket before exec to prevent fd leak */
        extern int sock_client;
        if (sock_client >= 0)
        {
            close(sock_client);
        }

        /* Lancement via xterm avec arguments directs (pas de shell, pas d'injection) */
        execlp("xterm", "xterm",
               "-T", "AffichageISY",
               "-e", "./bin/AffichageISY", portStr, nomClient,
               (char *)NULL);

        perror("execlp");
        exit(EXIT_FAILURE);
    }

    g_pidAffichage = pid;
}

/* Wrapper public qui conserve la signature originale (un seul paramètre).
 * Il utilise le nom d'utilisateur global pour lancer l'affichage. Si des
 * prototypes externes existent avec cette signature, ils resteront
 * compatibles. Les nouvelles fonctionnalités internes devraient appeler
 * lancer_affichage_ex().
 */
void lancer_affichage(int portGroupe)
{
    lancer_affichage_ex(portGroupe, g_nomUtilisateur);
}

void arreter_affichage(void)
{
    if (g_pidAffichage > 0)
    {
        /* Avec xterm, SIGTERM suffit généralement à fermer la fenêtre proprement */
        kill(g_pidAffichage, SIGTERM);

        /* On attend la fin du processus pour éviter les zombies */
        waitpid(g_pidAffichage, NULL, 0);

        g_pidAffichage = 0;
    }
}

/*
 * rejoins le groupe indiqué automatiquement après une fusion.
 * 'nomGroupe' est le nom du nouveau groupe à rejoindre et
 * 'portSuggestion' est un numéro de port fourni dans le message REP. Si ce
 * numéro est positif, il est utilisé pour lancer l'affichage sans attendre
 * la réponse du serveur ; sinon on utilise le port renvoyé par le serveur.
 */
static void rejoindre_groupe_auto(const char *nomGroupe, int portSuggestion)
{
    if (!nomGroupe || nomGroupe[0] == '\0') {
        printf("Fusion : nom de groupe invalide, impossibilité de rejoindre.\n");
        return;
    }

    /* Préparer et envoyer la requête de join au serveur */
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));
    strncpy(req.Ordre, "JNG", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);
    strncpy(req.Texte, nomGroupe, ISY_TAILLE_TEXTE - 1);

    envoyer_message_serveur(&req, &rep);

    int newPort = -1;
    if (strcmp(rep.Ordre, "ACK") == 0) {
        /* La réponse doit être du type OK <port> */
        if (sscanf(rep.Texte, "OK %d", &newPort) == 1 && newPort > 0 && newPort <= 65535) {
            /* port valide obtenu depuis le serveur */
        }
    }
    /* Si on n'a pas obtenu de port depuis le serveur, on utilise la suggestion */
    if (newPort <= 0 && portSuggestion > 0) {
        newPort = portSuggestion;
    }
    if (newPort <= 0) {
        printf("Impossible de déterminer le port pour rejoindre le groupe '%s'.\n", nomGroupe);
        return;
    }

    /* Fermer l'affichage actuel s'il existe */
    arreter_affichage();

    /* Mettre à jour les variables globales pour refléter le nouveau groupe actif */
    g_portGroupeActif = newPort;
    strncpy(g_nomGroupeActif, nomGroupe, ISY_TAILLE_TEXTE - 1);
    g_nomGroupeActif[ISY_TAILLE_TEXTE - 1] = '\0';

    /* Lancer la nouvelle fenêtre d'affichage avec le port et le nom utilisateur */
    printf("Fusion : connexion au nouveau groupe '%s' sur le port %d...\n", g_nomGroupeActif, g_portGroupeActif);
    /* Utiliser la fonction étendue pour préciser le nom utilisateur */
    lancer_affichage_ex(g_portGroupeActif, g_nomUtilisateur);
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
    if (fgets(req.Texte, ISY_TAILLE_TEXTE, stdin) == NULL)
        return;
    /* Vider le buffer si l'utilisateur a tapé plus que prévu */
    int was_truncated = (strchr(req.Texte, '\n') == NULL);
    if (was_truncated)
    {
        vider_stdin();
        printf(">> Attention: Le nom du groupe a ete tronque.\n");
    }
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
    if (fgets(req.Texte, ISY_TAILLE_TEXTE, stdin) == NULL)
        return;
    /* Vider le buffer si l'utilisateur a tapé plus que prévu */
    if (strchr(req.Texte, '\n') == NULL)
        vider_stdin();
    req.Texte[strcspn(req.Texte, "\n")] = '\0';

    /* Copie temporaire du nom demandé */
    char nomDemande[ISY_TAILLE_TEXTE];
    strncpy(nomDemande, req.Texte, ISY_TAILLE_TEXTE - 1);
    nomDemande[ISY_TAILLE_TEXTE - 1] = '\0';

    envoyer_message_serveur(&req, &rep);

    if (strcmp(rep.Ordre, "ACK") == 0)
    {
        /* rep.Texte = "OK <port>" */
        int port = -1;
        if (sscanf(rep.Texte, "OK %d", &port) == 1 && port > 0 && port <= 65535)
        {
            /* Avant de rejoindre définitivement, testons si l'utilisateur est banni sur ce groupe.
             * On envoie une commande 'CMD list' au processus de groupe et on attend une
             * réponse. Si un message BAN est reçu, le client est considéré comme banni et
             * la tentative de rejoindre est annulée. */
            int banni = 0;
            int sockTest = creer_socket_udp();
            if (sockTest >= 0)
            {
                /* Adresse du groupe */
                struct sockaddr_in addrG;
                init_sockaddr(&addrG, ISY_IP_SERVEUR, port);

                /* Construire une commande list minimaliste */
                MessageISY testMsg;
                memset(&testMsg, 0, sizeof(testMsg));
                strncpy(testMsg.Ordre, "CMD", ISY_TAILLE_ORDRE - 1);
                strncpy(testMsg.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);
                strncpy(testMsg.Texte, "list", ISY_TAILLE_TEXTE - 1);

                /* Envoyer la commande */
                sendto(sockTest, &testMsg, sizeof(testMsg), 0,
                       (struct sockaddr *)&addrG, sizeof(addrG));

                /* Attendre une réponse RSP ou BAN (timeout ~300ms) */
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 300000; /* 300 ms */
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(sockTest, &rfds);
                int sel = select(sockTest + 1, &rfds, NULL, NULL, &tv);
                if (sel > 0 && FD_ISSET(sockTest, &rfds))
                {
                    MessageISY resp;
                    struct sockaddr_in addrResp;
                    socklen_t lenResp = sizeof(addrResp);
                    ssize_t n = recvfrom(sockTest, &resp, sizeof(resp), 0,
                                        (struct sockaddr *)&addrResp, &lenResp);
                    if (n > 0)
                    {
                        resp.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
                        resp.Texte[ISY_TAILLE_TEXTE - 1] = '\0';
                        /* Un message BAN indique que l'utilisateur est banni */
                        if (strcmp(resp.Ordre, "BAN") == 0)
                        {
                            banni = 1;
                        }
                    }
                }
                fermer_socket_udp(sockTest);
            }

            if (banni)
            {
                printf("Vous êtes banni de ce groupe. Rejoindre est impossible.\n");
                return;
            }

            /* 1. Fermer l'ancienne fenêtre si elle existe */
            arreter_affichage();

            /* 2. Mettre à jour les variables globales */
            g_portGroupeActif = port;
            strncpy(g_nomGroupeActif, nomDemande, ISY_TAILLE_TEXTE - 1);
            g_nomGroupeActif[ISY_TAILLE_TEXTE - 1] = '\0';

            printf("Succès : Groupe '%s' rejoint sur le port %d\n", g_nomGroupeActif, g_portGroupeActif);

            /* 3. Lancer la fenêtre d'affichage avec le port et le nom utilisateur */
            lancer_affichage_ex(g_portGroupeActif, g_nomUtilisateur);
        }
        else
        {
            printf("Reponse ACK mal formee : %s\n", rep.Texte);
        }
    }
    else
    {
        printf("Erreur join : %s\n", rep.Texte);
    }
}

static void action_dialoguer_groupe(void)
{
    if (g_portGroupeActif == 0)
    {
        printf("Aucun groupe actif. Rejoindre un groupe d'abord.\n");
        return;
    }

    int sockG = creer_socket_udp();
    if (sockG < 0)
        return;

    struct sockaddr_in addrG;
    init_sockaddr(&addrG, ISY_IP_SERVEUR, g_portGroupeActif);

    printf("Tapez vos messages (ligne vide pour revenir au menu)...\n");

    char buffer[ISY_TAILLE_TEXTE];
    while (1)
    {
        /* Vérifier d'abord si on a reçu un message BAN ou REP (mode non-bloquant) */
        struct timeval tv_check = {0, 0}; /* Non-bloquant */
        fd_set readfds_check;
        FD_ZERO(&readfds_check);
        FD_SET(sockG, &readfds_check);

        if (select(sockG + 1, &readfds_check, NULL, NULL, &tv_check) > 0)
        {
            MessageISY banCheck;
            struct sockaddr_in addrTmp;
            socklen_t lenTmp = sizeof(addrTmp);
            ssize_t n = recvfrom(sockG, &banCheck, sizeof(banCheck), 0,
                                (struct sockaddr *)&addrTmp, &lenTmp);
            if (n > 0)
            {
                banCheck.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
                banCheck.Texte[ISY_TAILLE_TEXTE - 1] = '\0';
                /* Bannissement : retour immédiat au menu principal */
                if (strcmp(banCheck.Ordre, "BAN") == 0)
                {
                    printf("\n\n--- VOUS AVEZ ÉTÉ BANNI ---\n");
                    printf("%s\n", banCheck.Texte);
                    printf("Retour au menu principal...\n");
                    printf("----------------------------\n\n");
                    fermer_socket_udp(sockG);
                    g_portGroupeActif = 0;
                    memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
                    arreter_affichage();
                    return;
                }
                /* Message REP provenant du serveur : fusion. On parse le nouveau nom et le port suggéré et on rejoint automatiquement */
                else if (strcmp(banCheck.Ordre, "REP") == 0)
                {
                    char nouveauNom[ISY_TAILLE_TEXTE] = "";
                    int portSug = -1;
                    /* Attendu : "Fusion : Rejoignez '<nom>' (port <n>)" */
                    if (sscanf(banCheck.Texte, "Fusion : Rejoignez '%[^']' (port %d)", nouveauNom, &portSug) == 2)
                    {
                        printf("\n\n--- FUSION EN COURS ---\n");
                        printf("%s\n", banCheck.Texte);
                        printf("Connexion au nouveau groupe...\n");
                        printf("------------------------\n\n");
                        /* Fermeture de la socket du groupe actuel et retour au menu principal */
                        fermer_socket_udp(sockG);
                        g_portGroupeActif = 0;
                        memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
                        /* Rejoindre le nouveau groupe automatiquement */
                        rejoindre_groupe_auto(nouveauNom, portSug);
                        return;
                    }
                }
            }
        }

        printf("Message : ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            break;
        /* Vider le buffer si l'utilisateur a tapé plus que prévu */
        if (strchr(buffer, '\n') == NULL)
            vider_stdin();
        buffer[strcspn(buffer, "\n")] = '\0';
        if (buffer[0] == '\0')
            break;

        /* Si l'utilisateur souhaite entrer en mode commande, tape "cmd" */
        if (strcmp(buffer, "cmd") == 0)
        {
            /* Boucle de commande jusqu'à ce que l'utilisateur tape "msg" */
            while (1)
            {
                char cmdBuf[ISY_TAILLE_TEXTE];
                printf("Commande (tapez 'help' pour lister les commandes): ");
                if (fgets(cmdBuf, sizeof(cmdBuf), stdin) == NULL)
                    break;
                /* Vider le buffer si l'utilisateur a tapé plus que prévu */
                if (strchr(cmdBuf, '\n') == NULL)
                    vider_stdin();
                cmdBuf[strcspn(cmdBuf, "\n")] = '\0';
                /* Passage en minuscules pour comparaison insensible à la casse */
                char cmdLower[ISY_TAILLE_TEXTE];
                int i;
                for (i = 0; cmdBuf[i] && i < (int)sizeof(cmdLower) - 1; ++i)
                    cmdLower[i] = (char)tolower((unsigned char)cmdBuf[i]);
                cmdLower[i] = '\0';

                /* 'msg' : retour en mode chat */
                if (strcmp(cmdLower, "msg") == 0)
                {
                    break;
                }
                /* 'quit' : quitter le groupe (et revenir au menu) */
                else if (strcmp(cmdLower, "quit") == 0)
                {
                    fermer_socket_udp(sockG);
                    action_quitter_groupe();
                    return;
                }
                /* 'help' ou '?' : affiche les commandes disponibles */
                else if (strcmp(cmdLower, "help") == 0 || strcmp(cmdLower, "?") == 0)
                {
                    printf("\nCommandes disponibles :\n");
                    printf("  list         : lister les membres du groupe\n");
                    printf("  stats        : afficher les statistiques\n");
                    printf("  ban <nom>    : bannir un membre (gestionnaire)\n");
                    printf("  quit         : quitter le groupe\n");
                    printf("  msg          : revenir au mode chat\n");
                    printf("  help, ?      : afficher cette aide\n\n");
                    continue;
                }

                /* Envoyer la commande au groupe */
                MessageISY cmdMsg;
                memset(&cmdMsg, 0, sizeof(cmdMsg));
                strncpy(cmdMsg.Ordre, "CMD", ISY_TAILLE_ORDRE - 1);
                strncpy(cmdMsg.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);
                strncpy(cmdMsg.Texte, cmdBuf, ISY_TAILLE_TEXTE - 1);
                if (sendto(sockG, &cmdMsg, sizeof(cmdMsg), 0,
                           (struct sockaddr *)&addrG, sizeof(addrG)) < 0)
                {
                    perror("sendto CMD Client->Groupe");
                }
                /* Attendre une réponse RSP (ignorer les MSG broadcasts) */
                MessageISY rep;
                struct sockaddr_in addrR;
                socklen_t lenR = sizeof(addrR);
                int received_response = 0;

                /* Boucle pour ignorer les messages non-RSP */
                while (!received_response)
                {
                    ssize_t nrep = recvfrom(sockG, &rep, sizeof(rep), 0,
                                            (struct sockaddr *)&addrR, &lenR);
                    if (nrep <= 0)
                    {
                        perror("recvfrom CMD response");
                        break;
                    }

                    rep.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
                    rep.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
                    rep.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

                    /* Ne traiter que les réponses RSP */
                    if (strcmp(rep.Ordre, "RSP") == 0)
                    {
                        printf("\n%s\n", rep.Texte);
                        received_response = 1;
                    }
                    /* Traiter les messages de bannissement */
                    else if (strcmp(rep.Ordre, "BAN") == 0)
                    {
                        printf("\n--- VOUS AVEZ ÉTÉ BANNI ---\n");
                        printf("%s\n", rep.Texte);
                        printf("Retour au menu principal...\n");
                        printf("----------------------------\n\n");
                        /* Fermer et revenir au menu */
                        fermer_socket_udp(sockG);
                        g_portGroupeActif = 0;
                        memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
                        /* Arrêter l'affichage aussi */
                        arreter_affichage();
                        return;
                    }
                    /* Traiter les messages de redirection (REP) issus d'une fusion */
                    else if (strcmp(rep.Ordre, "REP") == 0)
                    {
                        char nouveauNom[ISY_TAILLE_TEXTE] = "";
                        int portSug = -1;
                        if (sscanf(rep.Texte, "Fusion : Rejoignez '%[^']' (port %d)", nouveauNom, &portSug) == 2)
                        {
                            printf("\n--- FUSION EN COURS ---\n");
                            printf("%s\n", rep.Texte);
                            printf("Connexion au nouveau groupe...\n");
                            printf("------------------------\n\n");
                            fermer_socket_udp(sockG);
                            g_portGroupeActif = 0;
                            memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
                            rejoindre_groupe_auto(nouveauNom, portSug);
                            return;
                        }
                        /* Si le format est inattendu, on ignore */
                    }
                    /* Ignorer silencieusement les autres types de messages (MSG, etc.) */
                }
            }
            continue;
        }

        MessageISY msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.Ordre, "MSG", ISY_TAILLE_ORDRE - 1);
        strncpy(msg.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);
        strncpy(msg.Texte, buffer, ISY_TAILLE_TEXTE - 1);
        msg.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        /* Chiffrement avant envoi */
        cesar_chiffrer(msg.Texte);

        if (sendto(sockG, &msg, sizeof(msg), 0,
                   (struct sockaddr *)&addrG, sizeof(addrG)) < 0)
        {
            perror("sendto Client->Groupe");
        }

        /* Vérifier si on a reçu un message BAN ou REP du serveur (mode non-bloquant) */
        struct timeval tv = {0, 50000}; /* 50ms timeout */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockG, &readfds);

        if (select(sockG + 1, &readfds, NULL, NULL, &tv) > 0)
        {
            MessageISY banCheck;
            struct sockaddr_in addrTmp;
            socklen_t lenTmp = sizeof(addrTmp);
            ssize_t n = recvfrom(sockG, &banCheck, sizeof(banCheck), 0,
                                (struct sockaddr *)&addrTmp, &lenTmp);
            if (n > 0)
            {
                banCheck.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
                banCheck.Texte[ISY_TAILLE_TEXTE - 1] = '\0';
                if (strcmp(banCheck.Ordre, "BAN") == 0)
                {
                    printf("\n\n--- VOUS AVEZ ÉTÉ BANNI ---\n");
                    printf("%s\n", banCheck.Texte);
                    printf("Retour au menu principal...\n");
                    printf("----------------------------\n\n");
                    fermer_socket_udp(sockG);
                    g_portGroupeActif = 0;
                    memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
                    arreter_affichage();
                    return;
                }
                else if (strcmp(banCheck.Ordre, "REP") == 0)
                {
                    char nouveauNom[ISY_TAILLE_TEXTE] = "";
                    int portSug = -1;
                    if (sscanf(banCheck.Texte, "Fusion : Rejoignez '%[^']' (port %d)", nouveauNom, &portSug) == 2)
                    {
                        printf("\n\n--- FUSION EN COURS ---\n");
                        printf("%s\n", banCheck.Texte);
                        printf("Connexion au nouveau groupe...\n");
                        printf("------------------------\n\n");
                        fermer_socket_udp(sockG);
                        g_portGroupeActif = 0;
                        memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
                        rejoindre_groupe_auto(nouveauNom, portSug);
                        return;
                    }
                }
            }
        }
    }

    fermer_socket_udp(sockG);
}

static void action_quitter_groupe(void)
{
    if (g_portGroupeActif == 0)
    {
        printf("Aucun groupe actif.\n");
        return;
    }

    /* Avant de quitter, notifier le groupe pour que les autres membres soient informés */
    /* On envoie une commande 'quit' au processus GroupeISY */
    {
        int sockTmp = creer_socket_udp();
        if (sockTmp >= 0)
        {
            struct sockaddr_in addrG;
            init_sockaddr(&addrG, ISY_IP_SERVEUR, g_portGroupeActif);
            MessageISY msg;
            memset(&msg, 0, sizeof(msg));
            strncpy(msg.Ordre, "CMD", ISY_TAILLE_ORDRE - 1);
            strncpy(msg.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);
            strncpy(msg.Texte, "quit", ISY_TAILLE_TEXTE - 1);
            /* Envoi sans attendre de réponse car on ferme ensuite */
            if (sendto(sockTmp, &msg, sizeof(msg), 0,
                       (struct sockaddr *)&addrG, sizeof(addrG)) < 0)
            {
                perror("sendto quit notification");
            }
            fermer_socket_udp(sockTmp);
        }
    }

    arreter_affichage();
    printf("Groupe sur port %d quitte (cote client seulement).\n",
           g_portGroupeActif);
    g_portGroupeActif = 0;
}

/* Supprimer un groupe (demande envoyée au serveur). Seul le modérateur peut le faire. */
static void action_supprimer_groupe(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "DEL", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    printf("Nom du groupe a supprimer : ");
    if (fgets(req.Texte, ISY_TAILLE_TEXTE, stdin) == NULL)
        return;
    /* Vider le buffer si l'utilisateur a tapé plus que prévu */
    if (strchr(req.Texte, '\n') == NULL)
        vider_stdin();
    req.Texte[strcspn(req.Texte, "\n")] = '\0';
    if (req.Texte[0] == '\0')
        return;

    /* Demande de confirmation avant suppression (fiche de version 2.0) */
    char confirmation[8];
    printf("Êtes‑vous sûr de vouloir supprimer le groupe '%s'? (o/N) : ", req.Texte);
    if (fgets(confirmation, sizeof(confirmation), stdin) == NULL)
        return;
    /* Vider le buffer si l'utilisateur a tapé plus que prévu */
    if (strchr(confirmation, '\n') == NULL)
        vider_stdin();
    /* On ne supprime que si l'utilisateur confirme par 'o' ou 'O' */
    if (!(confirmation[0] == 'o' || confirmation[0] == 'O'))
    {
        printf("Suppression annulée.\n");
        return;
    }

    /* On sauvegarde le nom visé pour le comparer plus tard */
    char groupeVise[ISY_TAILLE_TEXTE];
    strncpy(groupeVise, req.Texte, ISY_TAILLE_TEXTE - 1);
    groupeVise[ISY_TAILLE_TEXTE - 1] = '\0';

    envoyer_message_serveur(&req, &rep);

    printf("Reponse serveur : [%s] %s\n", rep.Ordre, rep.Texte);

    /* --- MODIFICATION ICI --- */

    /* 1. On vérifie si le serveur a validé la suppression (Code "OK") */
    if (strcmp(rep.Ordre, "OK") == 0)
    {

        /* 2. On vérifie si le groupe supprimé est celui actuellement ouvert */
        /* Note : g_nomGroupeActuel doit être mis à jour dans votre fonction 'rejoindre_groupe' */
        if (strcmp(groupeVise, g_nomGroupeActif) == 0)
        {

            printf("Le groupe courant a été supprimé. Fermeture de l'affichage...\n");

            /* On appelle la fonction d'arrêt qu'on a codée précédemment */
            arreter_affichage();

            /* On nettoie la variable globale pour dire qu'on n'est plus nulle part */
            memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
        }
    }
}

/* Fusionner deux groupes : saisir nom1 et nom2 et envoyer au serveur */
static void action_fusion_groupes(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "FUS", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    char g1[64], g2[64];
    printf("Nom du premier groupe : ");
    if (fgets(g1, sizeof(g1), stdin) == NULL)
        return;
    /* Vider le buffer si l'utilisateur a tapé plus que prévu */
    if (strchr(g1, '\n') == NULL)
        vider_stdin();
    g1[strcspn(g1, "\n")] = '\0';
    if (g1[0] == '\0')
        return;
    printf("Nom du second groupe : ");
    if (fgets(g2, sizeof(g2), stdin) == NULL)
        return;
    /* Vider le buffer si l'utilisateur a tapé plus que prévu */
    if (strchr(g2, '\n') == NULL)
        vider_stdin();
    g2[strcspn(g2, "\n")] = '\0';
    if (g2[0] == '\0')
        return;
    snprintf(req.Texte, ISY_TAILLE_TEXTE, "%s %s", g1, g2);

    envoyer_message_serveur(&req, &rep);
    printf("Reponse serveur : [%s] %s\n", rep.Ordre, rep.Texte);
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
    printf("6. Supprimer un groupe (moderateur)\n");
    printf("7. Fusionner deux groupes (moderateur)\n");
    printf("Votre choix : ");
}

/* --- AJOUT : Gestion de la connexion au démarrage --- */
static int login_au_serveur(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "CON", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    // On utilise la fonction existante pour l'échange
    envoyer_message_serveur(&req, &rep);

    // Analyse de la réponse
    if (strcmp(rep.Texte, "OK") == 0)
    {
        printf("Connexion reussie ! Bienvenue %s.\n", g_nomUtilisateur);
        return 1; // Succès
    }
    else if (strcmp(rep.Texte, "KO") == 0)
    {
        printf("Erreur : Le pseudo '%s' est deja utilise.\n", g_nomUtilisateur);
        return 0; // Échec
    }
    else if (strcmp(rep.Texte, "FULL") == 0)
    {
        printf("Erreur : Le serveur est complet.\n");
        return 0; // Échec
    }
    else if (strcmp(rep.Texte, "INVALID") == 0)
    {
        printf("Erreur : Le pseudo contient des caracteres invalides.\n");
        printf("Utilisez uniquement : lettres, chiffres, _, -, .\n");
        return 0; // Échec
    }
    else
    {
        printf("Erreur inconnue lors de la connexion : %s\n", rep.Texte);
        return 0;
    }
}

/* --- AJOUT : Gestion de la déconnexion à la fermeture --- */
static void logout_du_serveur(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "DEC", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    envoyer_message_serveur(&req, &rep);
    printf("Deconnexion : %s\n", rep.Texte);
}

/* --- AJOUT : Fonctions esthétiques --- */
static void nettoyer_ecran(void)
{
    /* Séquence ANSI pour effacer l'écran et remettre le curseur en haut à gauche */
    printf("\033[H\033[J");
}

/* Vider le buffer stdin pour éviter les caractères résiduels */
static void vider_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

static void pause_console(void)
{
    printf("\nAppuyez sur [Entree] pour continuer...");
    vider_stdin();
}

int main(void)
{
    /* Nettoyage au démarrage */
    nettoyer_ecran();

    sock_client = creer_socket_udp();
    if (sock_client < 0)
    {
        exit(EXIT_FAILURE);
    }

    init_sockaddr(&addrServeur, ISY_IP_SERVEUR, ISY_PORT_SERVEUR);

    int connecte = 0;
    char input[ISY_TAILLE_NOM];

    printf("=== BIENVENUE SUR ISY ===\n\n");

    /* Boucle de connexion */
    while (!connecte)
    {
        printf("Entrez votre nom d'utilisateur : ");

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            printf("\nArret demande.\n");
            fermer_socket_udp(sock_client);
            return 0;
        }

        /* Vider le buffer si l'utilisateur a tapé plus que prévu */
        if (strchr(input, '\n') == NULL)
            vider_stdin();

        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0')
        {
            printf(">> Le nom ne peut pas etre vide.\n");
            continue;
        }

        /* Validation du nom d'utilisateur */
        if (!valider_nom(input))
        {
            printf(">> Nom invalide. Utilisez uniquement lettres, chiffres, _, -, .\n");
            printf(">> Le nom doit faire moins de %d caracteres.\n", ISY_TAILLE_NOM);
            continue;
        }

        /* Avertir si le nom sera tronqué */
        size_t input_len = strlen(input);
        if (input_len >= ISY_TAILLE_NOM)
        {
            printf(">> Attention: Le nom sera tronque a %d caracteres.\n", ISY_TAILLE_NOM - 1);
        }

        strncpy(g_nomUtilisateur, input, ISY_TAILLE_NOM - 1);
        g_nomUtilisateur[ISY_TAILLE_NOM - 1] = '\0';

        if (login_au_serveur())
        {
            connecte = 1;
            /* Petit délai ou pause pour voir le message "Connexion réussie" */
            pause_console();
        }
        else
        {
            printf(">> Veuillez réessayer.\n\n");
            /* On attend que l'utilisateur lise l'erreur avant de nettoyer */
            pause_console();
            nettoyer_ecran();
            printf("=== BIENVENUE SUR ISY ===\n\n");
        }
    }

    int choix = -1;
    char ligne[16];

    /* Boucle du Menu Principal */
    while (1)
    {
        /* On nettoie l'écran à chaque retour au menu pour avoir un affichage propre */
        nettoyer_ecran();

        printf("Utilisateur : %s\n", g_nomUtilisateur);
        afficher_menu(); // Affiche la liste des choix

        if (fgets(ligne, sizeof(ligne), stdin) == NULL)
            break;
        /* Vider le buffer si l'utilisateur a tapé plus que prévu */
        if (strchr(ligne, '\n') == NULL)
            vider_stdin();
        if (sscanf(ligne, "%d", &choix) != 1)
            continue;

        /* Traitement du choix */
        /* Pour les actions (1, 2, 6, 7), on fait l'action PUIS on pause pour laisser lire */
        switch (choix)
        {
        case 0:
            logout_du_serveur();
            fermer_socket_udp(sock_client);
            arreter_affichage();
            printf("Fin du programme.\n");
            return 0;

        case 1:
            action_creer_groupe();
            pause_console(); // Attendre pour lire "Groupe créé"
            break;

        case 2:
            action_lister_groupes();
            pause_console(); // Attendre pour lire la liste
            break;

        case 3:
            action_rejoindre_groupe();
            pause_console();
            break;

        case 4:
            /* Le dialogue a sa propre logique d'affichage, on nettoie avant d'entrer */
            nettoyer_ecran();
            printf("--- MODE CHAT (Tapez 'cmd' pour options, ligne vide pour quitter) ---\n");
            action_dialoguer_groupe();
            /* Pas besoin de pause ici, quand on quitte le chat, on veut revenir au menu direct */
            break;

        case 5:
            action_quitter_groupe();
            pause_console();
            break;

        case 6:
            action_supprimer_groupe();
            pause_console();
            break;

        case 7:
            action_fusion_groupes();
            pause_console();
            break;

        default:
            printf("Choix invalide.\n");
            pause_console();
        }
    }

    arreter_affichage();
    logout_du_serveur();
    fermer_socket_udp(sock_client);
    return 0;
}