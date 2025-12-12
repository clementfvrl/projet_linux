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

/* ==== Fonctions utilitaires client ==== */
static void vider_stdin(void);

/* --- MODIFICATION : Gestionnaire pour détecter la fermeture d'AffichageISY --- */
static void handler_sigchld(int sig)
{
    (void)sig;
    int status;
    pid_t pid;
    
    /* On utilise waitpid avec WNOHANG pour ne pas bloquer si aucun fils n'est mort.
       Cela permet de "nettoyer" le processus zombie immédiatement. */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (pid == g_pidAffichage)
        {
            g_pidAffichage = 0; /* On marque que l'affichage est fermé */
        }
    }
}

/* Prototypes des actions */
static void action_creer_groupe(void);
static void action_lister_groupes(void);
static void action_rejoindre_groupe(void);
static void action_dialoguer_groupe(void);
static void action_quitter_groupe(void);
static void action_supprimer_groupe(void);
static void action_fusion_groupes(void);

/* Prototypes des fonctions d'affichage (définies plus bas) */
static void lancer_affichage(int portGroupe, const char *nomClient);
static void arreter_affichage(void);

static void envoyer_message_serveur(const MessageISY *msgReq,
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
static void lancer_affichage(int portGroupe, const char *nomClient)
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

        /* Modification de la commande Shell pour inclure le nomClient */
        char shell_command[1024 + 128];
        snprintf(shell_command, sizeof(shell_command),
                 "cd %s && ./bin/AffichageISY %s \"%s\"",
                 cwd, portStr, nomClient);

        /* Lancement via xterm */
        execlp("xterm", "xterm",
               "-T", "AffichageISY",
               "-e", "/bin/bash", "-c", shell_command,
               (char *)NULL);

        perror("execlp");
        exit(EXIT_FAILURE);
    }

    g_pidAffichage = pid;
}

static void arreter_affichage(void)
{
    if (g_pidAffichage > 0)
    {
        kill(g_pidAffichage, SIGTERM);
        waitpid(g_pidAffichage, NULL, 0);
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
    if (fgets(req.Texte, ISY_TAILLE_TEXTE, stdin) == NULL)
        return;
    if (strchr(req.Texte, '\n') == NULL) vider_stdin();
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
    if (strchr(req.Texte, '\n') == NULL) vider_stdin();
    req.Texte[strcspn(req.Texte, "\n")] = '\0';

    char nomDemande[ISY_TAILLE_TEXTE];
    strncpy(nomDemande, req.Texte, ISY_TAILLE_TEXTE - 1);
    nomDemande[ISY_TAILLE_TEXTE - 1] = '\0';

    envoyer_message_serveur(&req, &rep);

    if (strcmp(rep.Ordre, "ACK") == 0)
    {
        int port = 0;
        if (sscanf(rep.Texte, "OK %d", &port) == 1)
        {
            arreter_affichage();
            g_portGroupeActif = port;
            strncpy(g_nomGroupeActif, nomDemande, ISY_TAILLE_TEXTE - 1);
            g_nomGroupeActif[ISY_TAILLE_TEXTE - 1] = '\0';
            printf("Succès : Groupe '%s' rejoint sur le port %d\n", g_nomGroupeActif, g_portGroupeActif);
            lancer_affichage(g_portGroupeActif, g_nomUtilisateur);
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

    /* --- MODIFICATION : Vérification de sécurité --- */
    if (g_pidAffichage == 0) {
        printf("Erreur: La fenêtre d'affichage n'est pas lancée.\n");
        return;
    }

    int sockG = creer_socket_udp();
    if (sockG < 0) return;

    struct sockaddr_in addrG;
    init_sockaddr(&addrG, ISY_IP_SERVEUR, g_portGroupeActif);

    printf("Tapez vos messages (ligne vide pour revenir au menu)...\n");

    char buffer[ISY_TAILLE_TEXTE];
    while (1)
    {
        /* --- MODIFICATION : Vérifier si l'affichage est toujours vivant (ex: BAN) --- */
        if (g_pidAffichage == 0)
        {
            printf("\n>> La fenêtre de discussion a été fermée (ou vous avez été banni).\n");
            printf(">> Retour au menu principal.\n");
            break; 
        }

        printf("Message : ");
        
        /* --- MODIFICATION : Gestion de l'interruption par signal --- */
        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            if (errno == EINTR) {
                clearerr(stdin); /* Interruption par le signal SIGCHLD, on continue */
                continue; 
            }
            break; /* Vraie erreur ou EOF */
        }

        if (strchr(buffer, '\n') == NULL) vider_stdin();
        buffer[strcspn(buffer, "\n")] = '\0';
        if (buffer[0] == '\0') break;

        /* Si l'utilisateur souhaite entrer en mode commande */
        if (strcmp(buffer, "cmd") == 0)
        {
            printf("Tapez 'help' pour avoir toutes la liste des cmd\n");

            while (1)
            {
                /* Vérif Affichage dans la sous-boucle aussi */
                if (g_pidAffichage == 0) {
                   break; /* Sortira de la boucle cmd puis de la boucle principale au prochain tour */
                }

                char cmdBuf[ISY_TAILLE_TEXTE];
                printf("Commande : ");
                
                if (fgets(cmdBuf, sizeof(cmdBuf), stdin) == NULL) {
                     if (errno == EINTR) {
                        clearerr(stdin);
                        continue;
                    }
                    break;
                }

                if (strchr(cmdBuf, '\n') == NULL) vider_stdin();
                cmdBuf[strcspn(cmdBuf, "\n")] = '\0';

                char cmdLower[ISY_TAILLE_TEXTE];
                int i;
                for (i = 0; cmdBuf[i] && i < (int)sizeof(cmdLower) - 1; ++i)
                    cmdLower[i] = (char)tolower((unsigned char)cmdBuf[i]);
                cmdLower[i] = '\0';

                if (strcmp(cmdLower, "msg") == 0)
                {
                    break;
                }
                else if (strcmp(cmdLower, "quit") == 0)
                {
                    fermer_socket_udp(sockG);
                    action_quitter_groupe();
                    return;
                }
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
                
                MessageISY rep;
                struct sockaddr_in addrR;
                socklen_t lenR = sizeof(addrR);
                int received_response = 0;

                while (!received_response)
                {
                    ssize_t nrep = recvfrom(sockG, &rep, sizeof(rep), 0,
                                            (struct sockaddr *)&addrR, &lenR);
                    if (nrep <= 0) break;

                    rep.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
                    rep.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
                    rep.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

                    if (strcmp(rep.Ordre, "RSP") == 0)
                    {
                        printf("\n%s\n", rep.Texte);
                        received_response = 1;
                    }
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
        cesar_chiffrer(msg.Texte);

        if (sendto(sockG, &msg, sizeof(msg), 0,
                   (struct sockaddr *)&addrG, sizeof(addrG)) < 0)
        {
            perror("sendto Client->Groupe");
        }
    }

    fermer_socket_udp(sockG);
    
    /* --- MODIFICATION : Nettoyage si fermeture forcée --- */
    if (g_pidAffichage == 0) {
        g_portGroupeActif = 0;
        g_nomGroupeActif[0] = '\0';
    }
}

static void action_quitter_groupe(void)
{
    if (g_portGroupeActif == 0)
    {
        printf("Aucun groupe actif.\n");
        return;
    }

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
            sendto(sockTmp, &msg, sizeof(msg), 0,
                   (struct sockaddr *)&addrG, sizeof(addrG));
            fermer_socket_udp(sockTmp);
        }
    }

    arreter_affichage();
    printf("Groupe sur port %d quitte (cote client seulement).\n",
           g_portGroupeActif);
    g_portGroupeActif = 0;
}

static void action_supprimer_groupe(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "DEL", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    printf("Nom du groupe a supprimer : ");
    if (fgets(req.Texte, ISY_TAILLE_TEXTE, stdin) == NULL) return;
    if (strchr(req.Texte, '\n') == NULL) vider_stdin();
    req.Texte[strcspn(req.Texte, "\n")] = '\0';
    if (req.Texte[0] == '\0') return;

    char confirmation[8];
    printf("Êtes‑vous sûr de vouloir supprimer le groupe '%s'? (o/N) : ", req.Texte);
    if (fgets(confirmation, sizeof(confirmation), stdin) == NULL) return;
    if (strchr(confirmation, '\n') == NULL) vider_stdin();
    if (!(confirmation[0] == 'o' || confirmation[0] == 'O'))
    {
        printf("Suppression annulée.\n");
        return;
    }

    char groupeVise[ISY_TAILLE_TEXTE];
    strncpy(groupeVise, req.Texte, ISY_TAILLE_TEXTE - 1);
    groupeVise[ISY_TAILLE_TEXTE - 1] = '\0';

    envoyer_message_serveur(&req, &rep);

    printf("Reponse serveur : [%s] %s\n", rep.Ordre, rep.Texte);

    if (strcmp(rep.Ordre, "OK") == 0)
    {
        if (strcmp(groupeVise, g_nomGroupeActif) == 0)
        {
            printf("Le groupe courant a été supprimé. Fermeture de l'affichage...\n");
            arreter_affichage();
            memset(g_nomGroupeActif, 0, sizeof(g_nomGroupeActif));
        }
    }
}

static void action_fusion_groupes(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "FUS", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    char g1[64], g2[64];
    printf("Nom du premier groupe : ");
    if (fgets(g1, sizeof(g1), stdin) == NULL) return;
    if (strchr(g1, '\n') == NULL) vider_stdin();
    g1[strcspn(g1, "\n")] = '\0';
    if (g1[0] == '\0') return;
    
    printf("Nom du second groupe : ");
    if (fgets(g2, sizeof(g2), stdin) == NULL) return;
    if (strchr(g2, '\n') == NULL) vider_stdin();
    g2[strcspn(g2, "\n")] = '\0';
    if (g2[0] == '\0') return;
    
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

static int login_au_serveur(void)
{
    MessageISY req, rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));

    strncpy(req.Ordre, "CON", ISY_TAILLE_ORDRE - 1);
    strncpy(req.Emetteur, g_nomUtilisateur, ISY_TAILLE_NOM - 1);

    envoyer_message_serveur(&req, &rep);

    if (strcmp(rep.Texte, "OK") == 0)
    {
        printf("Connexion reussie ! Bienvenue %s.\n", g_nomUtilisateur);
        return 1;
    }
    else if (strcmp(rep.Texte, "KO") == 0)
    {
        printf("Erreur : Le pseudo '%s' est deja utilise.\n", g_nomUtilisateur);
        return 0;
    }
    else if (strcmp(rep.Texte, "FULL") == 0)
    {
        printf("Erreur : Le serveur est complet.\n");
        return 0;
    }
    else
    {
        printf("Erreur inconnue lors de la connexion : %s\n", rep.Texte);
        return 0;
    }
}

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

static void nettoyer_ecran(void)
{
    printf("\033[H\033[J");
}

static void vider_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

static void pause_console(void)
{
    printf("\nAppuyez sur [Entree] pour continuer...");
    vider_stdin();
}

int main(void)
{
    nettoyer_ecran();

    /* --- MODIFICATION : Installation du signal SIGCHLD --- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* Pas de SA_RESTART pour interrompre fgets */
    sigaction(SIGCHLD, &sa, NULL);

    sock_client = creer_socket_udp();
    if (sock_client < 0) exit(EXIT_FAILURE);

    init_sockaddr(&addrServeur, ISY_IP_SERVEUR, ISY_PORT_SERVEUR);

    int connecte = 0;
    char input[ISY_TAILLE_NOM];

    printf("=== BIENVENUE SUR ISY ===\n\n");

    while (!connecte)
    {
        printf("Entrez votre nom d'utilisateur : ");
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            fermer_socket_udp(sock_client);
            return 0;
        }
        if (strchr(input, '\n') == NULL) vider_stdin();
        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0') {
            printf(">> Le nom ne peut pas etre vide.\n");
            continue;
        }

        strncpy(g_nomUtilisateur, input, ISY_TAILLE_NOM - 1);
        g_nomUtilisateur[ISY_TAILLE_NOM - 1] = '\0';

        if (login_au_serveur()) {
            connecte = 1;
            pause_console();
        } else {
            printf(">> Veuillez réessayer.\n\n");
            pause_console();
            nettoyer_ecran();
            printf("=== BIENVENUE SUR ISY ===\n\n");
        }
    }

    int choix = -1;
    char ligne[16];

    while (1)
    {
        nettoyer_ecran();
        printf("Utilisateur : %s\n", g_nomUtilisateur);
        afficher_menu();

        if (fgets(ligne, sizeof(ligne), stdin) == NULL) break;
        if (strchr(ligne, '\n') == NULL) vider_stdin();
        if (sscanf(ligne, "%d", &choix) != 1) continue;

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
            pause_console();
            break;
        case 2:
            action_lister_groupes();
            pause_console();
            break;
        case 3:
            action_rejoindre_groupe();
            pause_console();
            break;
        case 4:
            nettoyer_ecran();
            printf("--- MODE CHAT (Tapez 'cmd' pour options, ligne vide pour quitter) ---\n");
            action_dialoguer_groupe();
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