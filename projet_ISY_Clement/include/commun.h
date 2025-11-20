#ifndef COMMUN_H
#define COMMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

/* Message protocol orders */
#define ORDRE_CON "CON"      /* Connection request */
#define ORDRE_DEC "DEC"      /* Disconnection request */
#define ORDRE_MES "MES"      /* Message */
#define ORDRE_CMD "CMD"      /* Command */
#define ORDRE_CRG "CRG"      /* Create group */
#define ORDRE_SUG "SUG"      /* Delete group */
#define ORDRE_LSG "LSG"      /* List groups */
#define ORDRE_JOG "JOG"      /* Join group */
#define ORDRE_QUG "QUG"      /* Quit group */
#define ORDRE_LMG "LMG"      /* List members of group */
#define ORDRE_MOD "MOD"      /* Moderate member */
#define ORDRE_FUS "FUS"      /* Merge groups */
#define ORDRE_ACK "ACK"      /* Acknowledgment */
#define ORDRE_ERR "ERR"      /* Error */
#define ORDRE_INF "INF"      /* Information */

/* Message structure */
typedef struct {
    char Ordre[4];          /* 4 bytes for order */
    char Emetteur[20];      /* 20 bytes for sender name */
    char Texte[100];        /* 100 bytes for text */
} struct_message;

/* Group information structure */
typedef struct {
    char nom[50];                    /* Group name */
    char moderateur[20];             /* Group moderator (creator) */
    int port;                        /* Group port (80xx) */
    int num_groupe;                  /* Group number */
    int max_membres;                 /* Maximum members */
    int nb_membres_actifs;           /* Current active members */
    int nb_messages;                 /* Number of messages exchanged */
    pid_t pid_groupe;                /* GroupeISY process PID */
    int actif;                       /* 1 if active, 0 if deleted */
    int shm_id;                      /* Shared memory ID for this group */
    int sem_id;                      /* Semaphore ID for this group */
} groupe_info;

/* Member information structure */
typedef struct {
    char nom[20];                    /* Member name */
    char ip[16];                     /* IP address */
    int port;                        /* Port number */
    int banni;                       /* 1 if banned, 0 otherwise */
    time_t derniere_activite;        /* Last activity timestamp */
} membre_info;

/* Configuration structure for server */
typedef struct {
    char ip_serveur[16];             /* Server IP address */
    int port_serveur;                /* Server port (8000) */
    int max_groupes;                 /* Maximum number of groups */
} config_serveur;

/* Configuration structure for client */
typedef struct {
    char nom_utilisateur[20];        /* Username */
    char ip_serveur[16];             /* Server IP address */
    int port_serveur;                /* Server port */
} config_client;

/* Shared memory structure between ServeurISY and GroupeISY */
typedef struct {
    groupe_info groupe;
    membre_info membres[100];        /* Max 100 members per group */
    int nb_membres;
    int shm_id;
    int sem_id;
} shm_groupe;

/* Shared memory structure between ClientISY and AffichageISY */
typedef struct {
    struct_message messages[100];    /* Message buffer */
    int nb_messages;
    int actif;                       /* 1 if active, 0 to terminate */
    int shm_id;
    int sem_id;
} shm_affichage;

/* Constants */
#define PORT_SERVEUR_DEFAUT 8000
#define PORT_GROUPE_BASE 8001
#define MAX_GROUPES 99
#define MAX_MEMBRES_PAR_GROUPE 100
#define TAILLE_MESSAGE sizeof(struct_message)
#define BUFFER_SIZE 1024

/* Semaphore operations macros */
#define P(sem_id) sem_wait(sem_id)
#define V(sem_id) sem_signal(sem_id)

/* Utility function prototypes */
void erreur(const char *msg);
int creer_socket_udp(void);
int lier_socket(int sockfd, const char *ip, int port);
int envoyer_message(int sockfd, struct_message *msg, const char *ip, int port);
int recevoir_message(int sockfd, struct_message *msg, struct sockaddr_in *addr);
void afficher_message(struct_message *msg);
void initialiser_message(struct_message *msg, const char *ordre, const char *emetteur, const char *texte);

/* Shared memory and semaphore utilities */
int creer_shm(size_t taille);
void* attacher_shm(int shm_id);
void detacher_shm(void *ptr);
void supprimer_shm(int shm_id);
int creer_semaphore(void);
void supprimer_semaphore(int sem_id);
void sem_wait(int sem_id);
void sem_signal(int sem_id);

/* Configuration file utilities */
int lire_config_serveur(const char *fichier, config_serveur *config);
int lire_config_client(const char *fichier, config_client *config);

#endif /* COMMUN_H */
