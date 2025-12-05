/**********************************************
 * FICHIER : ClientISY.c
 * Rôle   : Client ISY (menu + logique)
 *********************************************/

#include "commun.h"

static int sock_client = -1;
static struct sockaddr_in addrServeur;

/* Groupe courant (on simplifie à un seul groupe à la fois) */
static char g_nomGroupeActif[ISY_TAILLE_TEXTE] = "";
static int g_portGroupeActif = 0;
static pid_t g_pidAffichage = 0;
static char g_nomUtilisateur[ISY_TAILLE_NOM] = "user";

/* ==== Fonctions utilitaires client ==== */
/* Prototypes des actions */
static void action_creer_groupe(void);
static void action_lister_groupes(void);
static void action_rejoindre_groupe(void);
static void action_dialoguer_groupe(void);
static void action_quitter_groupe(void);
static void action_supprimer_groupe(void);
static void action_fusion_groupes(void);

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
/* Notez l'ajout de 'const char *nomClient' dans les paramètres */
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

        /* * Modification de la commande Shell pour inclure le nomClient
         * Exemple resultat : cd /home/user/projet && ./bin/AffichageISY 12345 "Paul"
         */
        char shell_command[1024 + 128];
        snprintf(shell_command, sizeof(shell_command),
                 "cd %s && ./bin/AffichageISY %s \"%s\"",
                 cwd, portStr, nomClient);

        /* Lancement via xterm (ou gnome-terminal selon votre choix précédent) */
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
        /* Avec xterm, SIGTERM suffit généralement à fermer la fenêtre proprement */
        kill(g_pidAffichage, SIGTERM);

        /* On attend la fin du processus pour éviter les zombies */
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
    req.Texte[strcspn(req.Texte, "\n")] = '\0';

    /* Copie temporaire du nom demandé */
    char nomDemande[ISY_TAILLE_TEXTE];
    strncpy(nomDemande, req.Texte, ISY_TAILLE_TEXTE - 1);
    nomDemande[ISY_TAILLE_TEXTE - 1] = '\0';

    envoyer_message_serveur(&req, &rep);

    if (strcmp(rep.Ordre, "ACK") == 0)
    {
        /* rep.Texte = "OK <port>" */
        int port = 0;
        if (sscanf(rep.Texte, "OK %d", &port) == 1)
        {

            /* 1. Fermer l'ancienne fenêtre si elle existe */
            arreter_affichage();

            /* 2. Mettre à jour les variables globales */
            g_portGroupeActif = port;
            strncpy(g_nomGroupeActif, nomDemande, ISY_TAILLE_TEXTE - 1);
            g_nomGroupeActif[ISY_TAILLE_TEXTE - 1] = '\0';

            printf("Succès : Groupe '%s' rejoint sur le port %d\n", g_nomGroupeActif, g_portGroupeActif);

            /* 3. Lancer la fenêtre avec le port ET le nom utilisateur */
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

    int sockG = creer_socket_udp();
    if (sockG < 0)
        return;

    struct sockaddr_in addrG;
    init_sockaddr(&addrG, ISY_IP_SERVEUR, g_portGroupeActif);

    printf("Tapez vos messages (ligne vide pour revenir au menu)...\n");

    char buffer[ISY_TAILLE_TEXTE];
    while (1)
    {
        printf("Message : ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            break;
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
                printf("Commande : ");
                if (fgets(cmdBuf, sizeof(cmdBuf), stdin) == NULL)
                    break;
                cmdBuf[strcspn(cmdBuf, "\n")] = '\0';
                if (strcmp(cmdBuf, "msg") == 0)
                {
                    /* retour au chat */
                    break;
                }
                else if (strcmp(cmdBuf, "quit") == 0)
                {
                    /* quitter complètement le groupe */
                    /* arrêter l'affichage et revenir au menu principal */
                    fermer_socket_udp(sockG);
                    action_quitter_groupe();
                    return;
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
                /* Attendre une réponse */
                MessageISY rep;
                struct sockaddr_in addrR;
                socklen_t lenR = sizeof(addrR);
                ssize_t nrep = recvfrom(sockG, &rep, sizeof(rep), 0,
                                        (struct sockaddr *)&addrR, &lenR);
                if (nrep > 0)
                {
                    rep.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
                    rep.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
                    rep.Texte[ISY_TAILLE_TEXTE - 1] = '\0';
                    printf("\n%s\n", rep.Texte);
                }
                else
                {
                    perror("recvfrom CMD response");
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
    req.Texte[strcspn(req.Texte, "\n")] = '\0';
    if (req.Texte[0] == '\0')
        return;

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
    g1[strcspn(g1, "\n")] = '\0';
    if (g1[0] == '\0')
        return;
    printf("Nom du second groupe : ");
    if (fgets(g2, sizeof(g2), stdin) == NULL)
        return;
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

static void pause_console(void)
{
    printf("\nAppuyez sur [Entree] pour continuer...");
    char buf[16];
    fgets(buf, sizeof(buf), stdin);
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

        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0')
        {
            printf(">> Le nom ne peut pas etre vide.\n");
            continue;
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