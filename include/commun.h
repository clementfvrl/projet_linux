#ifndef COMMUN_H
#define COMMUN_H

#include <stddef.h>
#include <time.h>

// Constantes globales

#define IP_SERVEUR             "127.0.0.1"
#define PORT_SERVEUR           8000

// Tailles de chaînes
#define TAILLE_PSEUDO          32
#define TAILLE_NOM_GROUPE      32
#define TAILLE_TEXTE           256

// Limites applicatives (adaptables)
#define NB_MAX_CLIENTS         32
#define NB_MAX_GROUPES         16
#define NB_MAX_GROUPES_PAR_CLIENT 8

// Valeur spéciale pour "pas de groupe"
#define ID_GROUPE_AUCUN        (-1)

// Types de messages échangés
typedef enum
{
    MSG_CONNEXION = 1,     /* Un client se connecte au serveur              */
    MSG_DECONNEXION,       /* Un client se déconnecte du serveur            */

    MSG_CREER_GROUPE,      /* Demande de création d'un groupe               */
    MSG_REJOINDRE_GROUPE,  /* Rejoindre un groupe existant                  */
    MSG_QUITTER_GROUPE,    /* Quitter un groupe                             */
    MSG_LISTE_GROUPES,     /* Demande / réponse : liste des groupes         */

    MSG_ENVOI_MESSAGE,     /* Client -> serveur : envoyer un message        */
    MSG_MESSAGE_GROUPE,    /* Serveur -> clients : message à diffuser       */

    MSG_DEMANDE_STATS,     /* Demande des statistiques d'un groupe          */
    MSG_REPONSE_STATS,     /* Réponse contenant les statistiques            */

    MSG_ERREUR             /* Message d'erreur générique                    */
} TypeMessage;

// Structure de base envoyée en UDP
typedef struct
{
    TypeMessage type;                          /* Type de message           */
    int         id_client;                     /* Identifiant logique       */
    int         id_groupe;                     /* Groupe concerné ou -1     */
    char        pseudo[TAILLE_PSEUDO];         /* Pseudo de l'émetteur      */
    char        texte[TAILLE_TEXTE];           /* Contenu (chiffré / clair) */
} Message;

// Prototypes de fonctions utilitaires

/* Affiche un message d'erreur et termine le programme. */
void erreur_fatale(const char *message);

/* Supprime le '\n' final d'une chaîne lue avec fgets, si présent. */
void supprimer_retour_ligne(char *chaine);

/* Affiche un message pour le debug (type, ids, pseudo, texte). */
void debug_afficher_message(const Message *msg);

#endif