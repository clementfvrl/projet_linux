/**********************************************
 * FICHIER : Commun.h
 * Rôle   : Déclarations communes ISY
 *********************************************/

#ifndef COMMUN_H
#define COMMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ==== CONSTANTES GÉNÉRALES ==== */

#define ISY_PORT_SERVEUR 8000
#define ISY_IP_SERVEUR "127.0.0.1"

#define ISY_MAX_GROUPES 16
#define ISY_MAX_MEMBRES 32

#define ISY_TAILLE_ORDRE 4
#define ISY_TAILLE_NOM 20
#define ISY_TAILLE_TEXTE 100

#define ISY_CESAR_DECALAGE 3

/* ==== STRUCTURE DE MESSAGE ==== */
/* Ordres simples (3 lettres + '\0') :
 * "LST" : liste groupes
 * "CRG" : créer groupe
 * "JNG" : joindre groupe
 * "ACK" : acquittement
 * "ERR" : erreur
 * "MSG" : message de groupe
 */

typedef struct struct_message
{
    char Ordre[ISY_TAILLE_ORDRE];
    char Emetteur[ISY_TAILLE_NOM];
    char Texte[ISY_TAILLE_TEXTE];
} MessageISY;

/* ==== DESCRIPTION D'UN GROUPE CÔTÉ SERVEUR ==== */

typedef struct
{
    int actif; /* 0 = slot libre, 1 = utilisé */
    int id;    /* index dans le tableau */
    char nom[32];
    int port;                            /* port UDP du groupe (ex: 8100 + id) */
    int pid;                             /* PID du processus GroupeISY associé */
    char moderateurName[ISY_TAILLE_NOM]; /* nom du créateur/modérateur */
} GroupeServeur;

/* =================================================
 * FONCTIONS UTILITAIRES COMMUNES (static inline)
 * =================================================
 *
 * Ces fonctions sont définies ici pour éviter d'avoir
 * un cinquième fichier .c (Commun.c). Chaque unité de
 * compilation qui inclut Commun.h en aura sa propre
 * copie, ce qui respecte la contrainte des 4 fichiers .c.
 */

/* Création d'un socket UDP avec timeout de réception */
static inline int creer_socket_udp(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return sock;
    }

    /* Configurer un timeout de réception de 5 secondes pour éviter les blocages infinis */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_RCVTIMEO");
        /* Non fatal, on continue */
    }

    return sock;
}

/* Fermeture d'un socket UDP */
static inline void fermer_socket_udp(int sock)
{
    if (sock >= 0)
    {
        close(sock);
    }
}

/* Initialisation d'une struct sockaddr_in */
static inline void init_sockaddr(struct sockaddr_in *addr,
                                 const char *ip,
                                 int port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    addr->sin_addr.s_addr = inet_addr(ip);
}

/* Affichage debug d'un message ISY */
static inline void afficher_message_debug(const char *prefix,
                                          const MessageISY *msg)
{
    printf("[%s] Ordre='%s' Emetteur='%s' Texte='%s'\n",
           prefix, msg->Ordre, msg->Emetteur, msg->Texte);
}

/* Validation d'un champ Ordre */
static inline int valider_ordre(const char *ordre)
{
    /* Liste des ordres valides */
    const char *ordres_valides[] = {
        "LST", "CRG", "JNG", "ACK", "ERR", "MSG", "CMD", "REG",
        "FIN", "BAN", "RSP", "CON", "DEC", "DEL", "FUS", "REP",
        NULL};

    if (!ordre || ordre[0] == '\0')
    {
        return 0;
    }

    /* Vérifier que le champ contient uniquement des lettres majuscules */
    for (int i = 0; ordre[i] != '\0' && i < ISY_TAILLE_ORDRE - 1; ++i)
    {
        if (ordre[i] < 'A' || ordre[i] > 'Z')
        {
            return 0;
        }
    }

    /* Vérifier que l'ordre est dans la liste */
    for (int i = 0; ordres_valides[i] != NULL; ++i)
    {
        if (strcmp(ordre, ordres_valides[i]) == 0)
        {
            return 1;
        }
    }

    return 0; /* Ordre inconnu */
}

/* Validation d'un ordre REQUETE (côté ServeurISY) */
static inline int valider_ordre_requete_serveur(const char *ordre)
{
    const char *req_valides[] = {
        "CON", "DEC", "CRG", "LST", "JNG", "DEL", "FUS",
        NULL};

    if (!ordre || ordre[0] == '\0')
        return 0;

    /* Majuscules uniquement (même logique que valider_ordre) */
    for (int i = 0; ordre[i] != '\0' && i < ISY_TAILLE_ORDRE - 1; ++i)
    {
        if (ordre[i] < 'A' || ordre[i] > 'Z')
            return 0;
    }

    for (int i = 0; req_valides[i] != NULL; ++i)
    {
        if (strcmp(ordre, req_valides[i]) == 0)
            return 1;
    }
    return 0;
}

/* Validation d'un nom (utilisateur ou groupe) */
static inline int valider_nom(const char *nom)
{
    if (!nom || nom[0] == '\0')
    {
        return 0; /* Nom vide */
    }

    /* Vérifier que le nom contient uniquement des caractères autorisés */
    for (int i = 0; nom[i] != '\0'; ++i)
    {
        char c = nom[i];
        /* Autoriser : lettres, chiffres, underscore, tiret, point */
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
        {
            return 0; /* Caractère invalide */
        }
    }

    /* Vérifier la longueur */
    size_t len = strlen(nom);
    if (len >= ISY_TAILLE_NOM)
    {
        return 0; /* Trop long */
    }

    return 1; /* Valide */
}

/* Chiffrement César simple sur les lettres (a-z, A-Z) */
static inline char cesar_shift_char(char c, int decalage)
{
    if (c >= 'a' && c <= 'z')
    {
        int base = 'a';
        int offset = ((c - base) + decalage) % 26;
        if (offset < 0)
            offset += 26;
        return (char)(base + offset);
    }
    else if (c >= 'A' && c <= 'Z')
    {
        int base = 'A';
        int offset = ((c - base) + decalage) % 26;
        if (offset < 0)
            offset += 26;
        return (char)(base + offset);
    }
    /* Tout le reste (espaces, ponctuation, chiffres) est inchangé */
    return c;
}

static inline void cesar_chiffrer(char *texte)
{
    if (!texte)
        return;
    for (int i = 0; texte[i] != '\0'; ++i)
    {
        texte[i] = cesar_shift_char(texte[i], ISY_CESAR_DECALAGE);
    }
}

static inline void cesar_dechiffrer(char *texte)
{
    if (!texte)
        return;
    for (int i = 0; texte[i] != '\0'; ++i)
    {
        texte[i] = cesar_shift_char(texte[i], -ISY_CESAR_DECALAGE);
    }
}

#endif /* COMMUN_H */
