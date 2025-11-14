#include "commun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

/* ========================================================================
   STRUCTURES DE DONNÉES
   ======================================================================== */

// Structure représentant un client connecté
typedef struct
{
    int id;
    char pseudo[TAILLE_PSEUDO];
    struct sockaddr_in adresse;
    int actif; // 1 si connecté, 0 sinon
} Client;

// Structure représentant un groupe
typedef struct
{
    int id;
    char nom[TAILLE_NOM_GROUPE];
    int moderateur_id;           // ID du créateur du groupe
    int membres[NB_MAX_CLIENTS]; // IDs des membres
    int nb_membres;
    int actif; // 1 si groupe existe, 0 si supprimé
} Groupe;

/* ========================================================================
   VARIABLES GLOBALES
   ======================================================================== */

static volatile int serveur_actif = 1;
static Client clients[NB_MAX_CLIENTS];
static Groupe groupes[NB_MAX_GROUPES];
static int nb_groupes = 0;

/* ========================================================================
   GESTION DES SIGNAUX
   ======================================================================== */

void gestionnaire_sigint(int sig)
{
    (void)sig;
    printf("\n[INFO] Arrêt du serveur demandé...\n");
    serveur_actif = 0;
}

/* ========================================================================
   INITIALISATION
   ======================================================================== */

void initialiser_clients(void)
{
    for (int i = 0; i < NB_MAX_CLIENTS; i++)
    {
        clients[i].id = i;
        clients[i].actif = 0;
        memset(clients[i].pseudo, 0, TAILLE_PSEUDO);
    }
}

void initialiser_groupes(void)
{
    for (int i = 0; i < NB_MAX_GROUPES; i++)
    {
        groupes[i].id = i;
        groupes[i].actif = 0;
        groupes[i].nb_membres = 0;
        memset(groupes[i].nom, 0, TAILLE_NOM_GROUPE);
        for (int j = 0; j < NB_MAX_CLIENTS; j++)
        {
            groupes[i].membres[j] = -1;
        }
    }
}

/* ========================================================================
   GESTION DES CLIENTS
   ======================================================================== */

int trouver_client_libre(void)
{
    for (int i = 0; i < NB_MAX_CLIENTS; i++)
    {
        if (!clients[i].actif)
            return i;
    }
    return -1;
}

int trouver_client_par_id(int id)
{
    if (id >= 0 && id < NB_MAX_CLIENTS && clients[id].actif)
        return id;
    return -1;
}

int pseudo_existe(const char *pseudo)
{
    for (int i = 0; i < NB_MAX_CLIENTS; i++)
    {
        if (clients[i].actif && strcmp(clients[i].pseudo, pseudo) == 0)
            return i; // Retourne l'ID du client qui utilise ce pseudo
    }
    return -1; // Pseudo libre
}

void connecter_client(int id, const char *pseudo, struct sockaddr_in *addr)
{
    if (id < 0 || id >= NB_MAX_CLIENTS)
        return;

    clients[id].actif = 1;
    strncpy(clients[id].pseudo, pseudo, TAILLE_PSEUDO - 1);
    clients[id].pseudo[TAILLE_PSEUDO - 1] = '\0';
    memcpy(&clients[id].adresse, addr, sizeof(struct sockaddr_in));

    printf("[CLIENT] %s connecté (ID: %d)\n", pseudo, id);
}

void deconnecter_client(int id)
{
    if (id < 0 || id >= NB_MAX_CLIENTS)
        return;

    printf("[CLIENT] %s déconnecté (ID: %d)\n", clients[id].pseudo, id);
    clients[id].actif = 0;
}

/* ========================================================================
   GESTION DES GROUPES
   ======================================================================== */

int trouver_groupe_par_nom(const char *nom)
{
    for (int i = 0; i < NB_MAX_GROUPES; i++)
    {
        if (groupes[i].actif && strcmp(groupes[i].nom, nom) == 0)
            return i;
    }
    return -1;
}

int creer_groupe(const char *nom, int moderateur_id)
{
    // Vérifier si le nom existe déjà
    if (trouver_groupe_par_nom(nom) >= 0)
    {
        printf("[GROUPE] Échec création '%s' : nom déjà existant\n", nom);
        return -1;
    }

    // Trouver un slot libre
    if (nb_groupes >= NB_MAX_GROUPES)
    {
        printf("[GROUPE] Échec création '%s' : limite atteinte\n", nom);
        return -1;
    }

    int idx = -1;
    for (int i = 0; i < NB_MAX_GROUPES; i++)
    {
        if (!groupes[i].actif)
        {
            idx = i;
            break;
        }
    }

    if (idx < 0)
        return -1;

    // Créer le groupe
    groupes[idx].actif = 1;
    strncpy(groupes[idx].nom, nom, TAILLE_NOM_GROUPE - 1);
    groupes[idx].nom[TAILLE_NOM_GROUPE - 1] = '\0';
    groupes[idx].moderateur_id = moderateur_id;
    groupes[idx].nb_membres = 0;

    nb_groupes++;
    printf("[GROUPE] Groupe '%s' créé (ID: %d, modérateur: %d)\n", nom, idx, moderateur_id);

    return idx;
}

int ajouter_membre_groupe(int groupe_id, int client_id)
{
    if (groupe_id < 0 || groupe_id >= NB_MAX_GROUPES || !groupes[groupe_id].actif)
        return -1;

    // Vérifier si déjà membre
    for (int i = 0; i < groupes[groupe_id].nb_membres; i++)
    {
        if (groupes[groupe_id].membres[i] == client_id)
        {
            printf("[GROUPE] Client %d déjà dans '%s'\n", client_id, groupes[groupe_id].nom);
            return 0; // Déjà membre, pas une erreur
        }
    }

    // Vérifier capacité
    if (groupes[groupe_id].nb_membres >= NB_MAX_CLIENTS)
    {
        printf("[GROUPE] Groupe '%s' plein\n", groupes[groupe_id].nom);
        return -1;
    }

    // Ajouter
    groupes[groupe_id].membres[groupes[groupe_id].nb_membres] = client_id;
    groupes[groupe_id].nb_membres++;

    printf("[GROUPE] Client %d (%s) a rejoint '%s'\n",
           client_id, clients[client_id].pseudo, groupes[groupe_id].nom);

    return 1;
}

int retirer_membre_groupe(int groupe_id, int client_id)
{
    if (groupe_id < 0 || groupe_id >= NB_MAX_GROUPES || !groupes[groupe_id].actif)
        return -1;

    // Trouver et retirer
    int trouve = 0;
    for (int i = 0; i < groupes[groupe_id].nb_membres; i++)
    {
        if (groupes[groupe_id].membres[i] == client_id)
        {
            trouve = 1;
            // Décaler les membres suivants
            for (int j = i; j < groupes[groupe_id].nb_membres - 1; j++)
            {
                groupes[groupe_id].membres[j] = groupes[groupe_id].membres[j + 1];
            }
            groupes[groupe_id].membres[groupes[groupe_id].nb_membres - 1] = -1;
            groupes[groupe_id].nb_membres--;
            break;
        }
    }

    if (trouve)
    {
        printf("[GROUPE] Client %d a quitté '%s'\n", client_id, groupes[groupe_id].nom);
        return 1;
    }

    return 0;
}

/* ========================================================================
   TRAITEMENT DES MESSAGES
   ======================================================================== */

void traiter_connexion(Message *msg, struct sockaddr_in *addr, int sockfd)
{
    int client_id = msg->id_client;

    // Vérifier validité de l'ID
    if (client_id < 0 || client_id >= NB_MAX_CLIENTS)
    {
        printf("[ERREUR] ID client invalide: %d\n", client_id);

        Message reponse;
        initialiser_message(&reponse);
        reponse.type = MSG_ERREUR;
        reponse.id_client = client_id;
        snprintf(reponse.texte, TAILLE_TEXTE, "ID client invalide");

        sendto(sockfd, &reponse, sizeof(reponse), 0,
               (struct sockaddr *)addr, sizeof(*addr));
        return;
    }

    // Si l'ID est déjà utilisé, déconnecter l'ancien client d'abord
    if (clients[client_id].actif)
    {
        printf("[WARN] ID %d déjà utilisé par '%s', remplacement...\n",
               client_id, clients[client_id].pseudo);

        // Retirer l'ancien client de tous ses groupes
        for (int i = 0; i < NB_MAX_GROUPES; i++)
        {
            if (groupes[i].actif)
            {
                retirer_membre_groupe(i, client_id);
            }
        }
    }

    // Vérifier unicité du pseudo (mais autoriser la reconnexion avec le même ID)
    int pseudo_utilisateur_id = pseudo_existe(msg->pseudo);
    if (pseudo_utilisateur_id >= 0 && pseudo_utilisateur_id != client_id)
    {
        printf("[ERREUR] Pseudo '%s' déjà utilisé par client %d\n", msg->pseudo, pseudo_utilisateur_id);

        Message reponse;
        initialiser_message(&reponse);
        reponse.type = MSG_ERREUR;
        reponse.id_client = client_id;
        snprintf(reponse.texte, TAILLE_TEXTE, "Pseudo '%.20s' déjà utilisé", msg->pseudo);

        sendto(sockfd, &reponse, sizeof(reponse), 0,
               (struct sockaddr *)addr, sizeof(*addr));
        return;
    }

    connecter_client(client_id, msg->pseudo, addr);

    // Confirmer la connexion
    Message reponse;
    initialiser_message(&reponse);
    reponse.type = MSG_CONNEXION;
    reponse.id_client = client_id;
    snprintf(reponse.texte, TAILLE_TEXTE, "Connexion acceptée");

    sendto(sockfd, &reponse, sizeof(reponse), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

void traiter_deconnexion(Message *msg)
{
    int client_id = msg->id_client;

    if (client_id < 0 || client_id >= NB_MAX_CLIENTS)
        return;

    // Retirer le client de tous les groupes
    for (int i = 0; i < NB_MAX_GROUPES; i++)
    {
        if (groupes[i].actif)
        {
            retirer_membre_groupe(i, client_id);
        }
    }

    deconnecter_client(client_id);
}

void traiter_creation_groupe(Message *msg, struct sockaddr_in *addr, int sockfd)
{
    printf("[DEBUG] Tentative création groupe '%s' par client %d\n", msg->texte, msg->id_client);

    int groupe_id = creer_groupe(msg->texte, msg->id_client);

    Message reponse;
    initialiser_message(&reponse);
    reponse.id_client = msg->id_client;

    if (groupe_id >= 0)
    {
        reponse.type = MSG_CREER_GROUPE;
        reponse.id_groupe = groupe_id;
        snprintf(reponse.texte, TAILLE_TEXTE, "Groupe '%.30s' créé (ID: %d)",
                 msg->texte, groupe_id);
        printf("[REPONSE] Succès création groupe ID=%d\n", groupe_id);
    }
    else
    {
        // Vérifier si c'est parce que le nom existe déjà
        if (trouver_groupe_par_nom(msg->texte) >= 0)
        {
            reponse.type = MSG_ERREUR;
            reponse.id_groupe = -1;
            snprintf(reponse.texte, TAILLE_TEXTE, "Groupe '%.30s' existe déjà", msg->texte);
            printf("[REPONSE] Erreur: groupe existe déjà\n");
        }
        else
        {
            reponse.type = MSG_ERREUR;
            reponse.id_groupe = -1;
            snprintf(reponse.texte, TAILLE_TEXTE, "Échec création groupe (limite atteinte)");
            printf("[REPONSE] Erreur: limite atteinte\n");
        }
    }

    printf("[DEBUG] Envoi réponse type=%d texte='%s'\n", reponse.type, reponse.texte);

    ssize_t sent = sendto(sockfd, &reponse, sizeof(reponse), 0,
                          (struct sockaddr *)addr, sizeof(*addr));

    if (sent < 0)
    {
        perror("[ERREUR] sendto() dans traiter_creation_groupe");
    }
    else
    {
        printf("[DEBUG] %ld octets envoyés au client\n", (long)sent);
    }
}

void traiter_liste_groupes(Message *msg, struct sockaddr_in *addr, int sockfd)
{
    Message reponse;
    initialiser_message(&reponse);
    reponse.type = MSG_LISTE_GROUPES;
    reponse.id_client = msg->id_client;

    char liste[TAILLE_TEXTE] = {0};
    int count = 0;

    for (int i = 0; i < NB_MAX_GROUPES; i++)
    {
        if (groupes[i].actif)
        {
            char ligne[64];
            snprintf(ligne, sizeof(ligne), "[%d] %s (%d membres)\n",
                     i, groupes[i].nom, groupes[i].nb_membres);

            if (strlen(liste) + strlen(ligne) < TAILLE_TEXTE - 1)
            {
                strcat(liste, ligne);
                count++;
            }
        }
    }

    if (count == 0)
    {
        snprintf(reponse.texte, TAILLE_TEXTE, "Aucun groupe disponible");
    }
    else
    {
        snprintf(reponse.texte, TAILLE_TEXTE, "Groupes (%d):\n%.230s", count, liste);
    }

    sendto(sockfd, &reponse, sizeof(reponse), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

void traiter_rejoindre_groupe(Message *msg, struct sockaddr_in *addr, int sockfd)
{
    int groupe_id = trouver_groupe_par_nom(msg->texte);

    Message reponse;
    initialiser_message(&reponse);
    reponse.type = MSG_REJOINDRE_GROUPE;
    reponse.id_client = msg->id_client;

    if (groupe_id >= 0)
    {
        int resultat = ajouter_membre_groupe(groupe_id, msg->id_client);
        if (resultat > 0)
        {
            reponse.id_groupe = groupe_id;
            snprintf(reponse.texte, TAILLE_TEXTE, "Vous avez rejoint '%.30s'", msg->texte);
        }
        else
        {
            reponse.id_groupe = -1;
            snprintf(reponse.texte, TAILLE_TEXTE, "Impossible de rejoindre '%.30s'", msg->texte);
        }
    }
    else
    {
        reponse.id_groupe = -1;
        snprintf(reponse.texte, TAILLE_TEXTE, "Groupe '%.30s' introuvable", msg->texte);
    }

    sendto(sockfd, &reponse, sizeof(reponse), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

void traiter_quitter_groupe(Message *msg, struct sockaddr_in *addr, int sockfd)
{
    int groupe_id = msg->id_groupe;

    Message reponse;
    initialiser_message(&reponse);
    reponse.type = MSG_QUITTER_GROUPE;
    reponse.id_client = msg->id_client;

    if (groupe_id >= 0 && groupe_id < NB_MAX_GROUPES && groupes[groupe_id].actif)
    {
        int resultat = retirer_membre_groupe(groupe_id, msg->id_client);
        if (resultat > 0)
        {
            reponse.id_groupe = groupe_id;
            snprintf(reponse.texte, TAILLE_TEXTE, "Vous avez quitté '%.30s'",
                     groupes[groupe_id].nom);
        }
        else
        {
            reponse.id_groupe = -1;
            snprintf(reponse.texte, TAILLE_TEXTE, "Vous n'étiez pas dans ce groupe");
        }
    }
    else
    {
        reponse.id_groupe = -1;
        snprintf(reponse.texte, TAILLE_TEXTE, "Groupe invalide");
    }

    sendto(sockfd, &reponse, sizeof(reponse), 0,
           (struct sockaddr *)addr, sizeof(*addr));
}

void traiter_message_groupe(Message *msg, int sockfd)
{
    int groupe_id = msg->id_groupe;

    if (groupe_id < 0 || groupe_id >= NB_MAX_GROUPES || !groupes[groupe_id].actif)
    {
        printf("[ERREUR] Message pour groupe invalide: %d\n", groupe_id);
        return;
    }

    printf("[MESSAGE] %s dans '%s': %s\n",
           msg->pseudo, groupes[groupe_id].nom, msg->texte);

    // Broadcast à tous les membres du groupe
    Message diffusion;
    memcpy(&diffusion, msg, sizeof(Message));
    diffusion.type = MSG_MESSAGE_GROUPE;

    for (int i = 0; i < groupes[groupe_id].nb_membres; i++)
    {
        int membre_id = groupes[groupe_id].membres[i];
        if (membre_id >= 0 && membre_id < NB_MAX_CLIENTS && clients[membre_id].actif)
        {
            sendto(sockfd, &diffusion, sizeof(diffusion), 0,
                   (struct sockaddr *)&clients[membre_id].adresse,
                   sizeof(clients[membre_id].adresse));
        }
    }
}

/* ========================================================================
   FONCTION PRINCIPALE
   ======================================================================== */

int main(void)
{
    printf("╔════════════════════════════════════════╗\n");
    printf("║     SERVEUR ISY - PHASE 2              ║\n");
    printf("║     Gestion des groupes (CRUD)         ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    // Initialisation
    initialiser_clients();
    initialiser_groupes();
    signal(SIGINT, gestionnaire_sigint);

    // Socket UDP
    int sockfd = creer_socket_udp();
    struct sockaddr_in adresse_serveur;
    initialiser_adresse(&adresse_serveur, IP_SERVEUR, PORT_SERVEUR);

    if (bind(sockfd, (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0)
    {
        close(sockfd);
        erreur_fatale("Erreur bind()");
    }

    printf("[OK] Serveur en écoute sur %s:%d\n", IP_SERVEUR, PORT_SERVEUR);
    printf("[INFO] Capacité : %d clients, %d groupes\n", NB_MAX_CLIENTS, NB_MAX_GROUPES);
    printf("[INFO] Appuyez sur Ctrl+C pour arrêter\n");
    printf("─────────────────────────────────────────\n\n");

    // Boucle principale
    Message msg_recu;
    struct sockaddr_in adresse_client;
    socklen_t taille_adresse = sizeof(adresse_client);

    while (serveur_actif)
    {
        memset(&msg_recu, 0, sizeof(msg_recu));

        ssize_t nb_octets = recvfrom(sockfd, &msg_recu, sizeof(msg_recu), 0,
                                     (struct sockaddr *)&adresse_client, &taille_adresse);

        if (nb_octets < 0)
        {
            if (serveur_actif)
                perror("[ERREUR] recvfrom()");
            continue;
        }

        // Traitement selon le type de message
        switch (msg_recu.type)
        {
        case MSG_CONNEXION:
            traiter_connexion(&msg_recu, &adresse_client, sockfd);
            break;

        case MSG_DECONNEXION:
            traiter_deconnexion(&msg_recu);
            break;

        case MSG_CREER_GROUPE:
            traiter_creation_groupe(&msg_recu, &adresse_client, sockfd);
            break;

        case MSG_LISTE_GROUPES:
            traiter_liste_groupes(&msg_recu, &adresse_client, sockfd);
            break;

        case MSG_REJOINDRE_GROUPE:
            traiter_rejoindre_groupe(&msg_recu, &adresse_client, sockfd);
            break;

        case MSG_QUITTER_GROUPE:
            traiter_quitter_groupe(&msg_recu, &adresse_client, sockfd);
            break;

        case MSG_ENVOI_MESSAGE:
            traiter_message_groupe(&msg_recu, sockfd);
            break;

        default:
            printf("[WARN] Type de message inconnu: %d\n", msg_recu.type);
        }
    }

    // Fermeture propre
    close(sockfd);
    printf("\n[OK] Serveur arrêté proprement\n");
    printf("[INFO] Groupes créés: %d\n", nb_groupes);

    return EXIT_SUCCESS;
}