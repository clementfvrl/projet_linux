#include "../include/commun.h"

/* Error handling function */
void erreur(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

/* Create UDP socket */
int creer_socket_udp(void) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        erreur("Erreur: creation socket");
    }
    return sockfd;
}

/* Bind socket to IP and port */
int lier_socket(int sockfd, const char *ip, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip == NULL || strcmp(ip, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
            erreur("Erreur: adresse IP invalide");
        }
    }

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        erreur("Erreur: liaison socket");
    }

    return 0;
}

/* Send message via UDP */
int envoyer_message(int sockfd, struct_message *msg, const char *ip, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Erreur: adresse IP invalide\n");
        return -1;
    }

    int n = sendto(sockfd, msg, TAILLE_MESSAGE, 0,
                   (struct sockaddr *)&addr, sizeof(addr));
    if (n < 0) {
        perror("Erreur: envoi message");
        return -1;
    }

    return n;
}

/* Receive message via UDP */
int recevoir_message(int sockfd, struct_message *msg, struct sockaddr_in *addr) {
    socklen_t addr_len = sizeof(*addr);
    memset(msg, 0, sizeof(struct_message));

    int n = recvfrom(sockfd, msg, TAILLE_MESSAGE, 0,
                     (struct sockaddr *)addr, &addr_len);
    if (n < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Erreur: reception message");
        }
        return -1;
    }

    return n;
}

/* Display message */
void afficher_message(struct_message *msg) {
    printf("Ordre: %s | Emetteur: %s | Texte: %s\n",
           msg->Ordre, msg->Emetteur, msg->Texte);
}

/* Initialize message structure */
void initialiser_message(struct_message *msg, const char *ordre,
                        const char *emetteur, const char *texte) {
    memset(msg, 0, sizeof(struct_message));

    if (ordre != NULL) {
        strncpy(msg->Ordre, ordre, 3);
        msg->Ordre[3] = '\0';
    }

    if (emetteur != NULL) {
        strncpy(msg->Emetteur, emetteur, 19);
        msg->Emetteur[19] = '\0';
    }

    if (texte != NULL) {
        strncpy(msg->Texte, texte, 99);
        msg->Texte[99] = '\0';
    }
}

/* Create shared memory segment */
int creer_shm(size_t taille) {
    int shm_id = shmget(IPC_PRIVATE, taille, IPC_CREAT | 0666);
    if (shm_id < 0) {
        erreur("Erreur: creation memoire partagee");
    }
    return shm_id;
}

/* Attach to shared memory */
void* attacher_shm(int shm_id) {
    void *ptr = shmat(shm_id, NULL, 0);
    if (ptr == (void *)-1) {
        erreur("Erreur: attachement memoire partagee");
    }
    return ptr;
}

/* Detach from shared memory */
void detacher_shm(void *ptr) {
    if (shmdt(ptr) < 0) {
        erreur("Erreur: detachement memoire partagee");
    }
}

/* Delete shared memory segment */
void supprimer_shm(int shm_id) {
    if (shmctl(shm_id, IPC_RMID, NULL) < 0) {
        perror("Erreur: suppression memoire partagee");
    }
}

/* Create semaphore */
int creer_semaphore(void) {
    int sem_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    if (sem_id < 0) {
        erreur("Erreur: creation semaphore");
    }

    /* Initialize semaphore to 1 */
    if (semctl(sem_id, 0, SETVAL, 1) < 0) {
        erreur("Erreur: initialisation semaphore");
    }

    return sem_id;
}

/* Delete semaphore */
void supprimer_semaphore(int sem_id) {
    if (semctl(sem_id, 0, IPC_RMID) < 0) {
        perror("Erreur: suppression semaphore");
    }
}

/* Wait on semaphore (P operation) */
void sem_wait(int sem_id) {
    struct sembuf op = {0, -1, 0};
    if (semop(sem_id, &op, 1) < 0) {
        if (errno != EINTR) {
            erreur("Erreur: operation P sur semaphore");
        }
    }
}

/* Signal semaphore (V operation) */
void sem_signal(int sem_id) {
    struct sembuf op = {0, 1, 0};
    if (semop(sem_id, &op, 1) < 0) {
        erreur("Erreur: operation V sur semaphore");
    }
}

/* Read server configuration file */
int lire_config_serveur(const char *fichier, config_serveur *config) {
    FILE *f = fopen(fichier, "r");
    if (f == NULL) {
        /* Use default values */
        strcpy(config->ip_serveur, "0.0.0.0");
        config->port_serveur = PORT_SERVEUR_DEFAUT;
        config->max_groupes = MAX_GROUPES;
        return 0;
    }

    char ligne[256];
    while (fgets(ligne, sizeof(ligne), f) != NULL) {
        char cle[50], valeur[200];
        if (sscanf(ligne, "%[^=]=%s", cle, valeur) == 2) {
            if (strcmp(cle, "IP") == 0) {
                strncpy(config->ip_serveur, valeur, 15);
                config->ip_serveur[15] = '\0';
            } else if (strcmp(cle, "PORT") == 0) {
                config->port_serveur = atoi(valeur);
            } else if (strcmp(cle, "MAX_GROUPES") == 0) {
                config->max_groupes = atoi(valeur);
            }
        }
    }

    fclose(f);
    return 0;
}

/* Read client configuration file */
int lire_config_client(const char *fichier, config_client *config) {
    FILE *f = fopen(fichier, "r");
    if (f == NULL) {
        fprintf(stderr, "Erreur: impossible d'ouvrir le fichier de configuration\n");
        return -1;
    }

    char ligne[256];
    while (fgets(ligne, sizeof(ligne), f) != NULL) {
        char cle[50], valeur[200];
        if (sscanf(ligne, "%[^=]=%s", cle, valeur) == 2) {
            if (strcmp(cle, "UTILISATEUR") == 0) {
                strncpy(config->nom_utilisateur, valeur, 19);
                config->nom_utilisateur[19] = '\0';
            } else if (strcmp(cle, "IP_SERVEUR") == 0) {
                strncpy(config->ip_serveur, valeur, 15);
                config->ip_serveur[15] = '\0';
            } else if (strcmp(cle, "PORT_SERVEUR") == 0) {
                config->port_serveur = atoi(valeur);
            }
        }
    }

    fclose(f);
    return 0;
}

/* Verify user credentials - returns 1 if valid, 0 if invalid */
int verifier_utilisateur(const char *username, const char *password) {
    FILE *f = fopen("data/users.txt", "r");
    if (f == NULL) {
        /* If file doesn't exist, create first user */
        return creer_utilisateur(username, password);
    }

    char ligne[256];
    char stored_user[20], stored_pass[32];

    while (fgets(ligne, sizeof(ligne), f) != NULL) {
        /* Format: username:password */
        if (sscanf(ligne, "%19[^:]:%31s", stored_user, stored_pass) == 2) {
            if (strcmp(stored_user, username) == 0) {
                fclose(f);
                /* User exists, check password */
                return (strcmp(stored_pass, password) == 0) ? 1 : 0;
            }
        }
    }

    fclose(f);
    /* User not found - auto-register new user */
    return creer_utilisateur(username, password);
}

/* Create new user - returns 1 on success, 0 on failure */
int creer_utilisateur(const char *username, const char *password) {
    /* Check if username is valid (no colons, newlines) */
    if (strchr(username, ':') != NULL || strchr(username, '\n') != NULL ||
        strlen(username) == 0 || strlen(password) == 0) {
        return 0;
    }

    FILE *f = fopen("data/users.txt", "a");
    if (f == NULL) {
        perror("Erreur: impossible de creer l'utilisateur");
        return 0;
    }

    fprintf(f, "%s:%s\n", username, password);
    fclose(f);

    printf("Nouvel utilisateur cree: %s\n", username);
    return 1;
}
