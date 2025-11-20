#include "../include/commun.h"
#include "../include/crypto.h"
#include "../include/stats.h"
#include <sys/wait.h>

/* Global variables */
volatile sig_atomic_t serveur_actif = 1;
int sockfd_serveur;
groupe_info groupes[MAX_GROUPES];
int nb_groupes = 0;
config_serveur config;

/* Signal handler for CTRL-C */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n\nReception signal SIGINT, arret du serveur...\n");
        serveur_actif = 0;
    }
}

/* Find group by name */
int trouver_groupe(const char *nom) {
    for (int i = 0; i < nb_groupes; i++) {
        if (groupes[i].actif && strcmp(groupes[i].nom, nom) == 0) {
            return i;
        }
    }
    return -1;
}

/* Create a new group */
int creer_groupe(const char *nom, const char *moderateur) {
    /* Check if group already exists */
    if (trouver_groupe(nom) != -1) {
        return -1;  /* Group already exists */
    }

    /* Find free slot */
    int index = -1;
    for (int i = 0; i < MAX_GROUPES; i++) {
        if (!groupes[i].actif) {
            index = i;
            break;
        }
    }

    if (index == -1 || nb_groupes >= config.max_groupes) {
        return -2;  /* No free slots */
    }

    /* Initialize group */
    memset(&groupes[index], 0, sizeof(groupe_info));
    strncpy(groupes[index].nom, nom, 49);
    groupes[index].nom[49] = '\0';
    strncpy(groupes[index].moderateur, moderateur, 19);
    groupes[index].moderateur[19] = '\0';
    groupes[index].num_groupe = index + 1;
    groupes[index].port = PORT_GROUPE_BASE + index;
    groupes[index].max_membres = MAX_MEMBRES_PAR_GROUPE;
    groupes[index].nb_membres_actifs = 0;
    groupes[index].nb_messages = 0;
    groupes[index].actif = 1;

    /* Create shared memory for this group */
    int shm_id = creer_shm(sizeof(shm_groupe));
    if (shm_id < 0) {
        perror("Erreur: creation SHM pour groupe");
        return -3;
    }

    /* Attach to shared memory */
    shm_groupe *shm = (shm_groupe *)attacher_shm(shm_id);
    if (shm == NULL) {
        perror("Erreur: attachement SHM pour groupe");
        supprimer_shm(shm_id);
        return -3;
    }

    /* Create semaphore for synchronization */
    int sem_id = creer_semaphore();
    if (sem_id < 0) {
        perror("Erreur: creation semaphore pour groupe");
        detacher_shm(shm);
        supprimer_shm(shm_id);
        return -3;
    }

    /* Initialize shared memory */
    memcpy(&shm->groupe, &groupes[index], sizeof(groupe_info));
    shm->nb_membres = 0;
    shm->shm_id = shm_id;
    shm->sem_id = sem_id;

    /* Store SHM and semaphore IDs in groupe structure */
    groupes[index].shm_id = shm_id;
    groupes[index].sem_id = sem_id;

    /* Detach from shared memory (child will attach) */
    detacher_shm(shm);

    /* Fork GroupeISY process */
    pid_t pid = fork();
    if (pid < 0) {
        perror("Erreur: fork pour GroupeISY");
        supprimer_semaphore(sem_id);
        supprimer_shm(shm_id);
        return -3;
    }

    if (pid == 0) {
        /* Child process - execute GroupeISY */
        char port_str[10], nom_groupe[50], shm_id_str[20], sem_id_str[20];
        snprintf(port_str, sizeof(port_str), "%d", groupes[index].port);
        snprintf(nom_groupe, sizeof(nom_groupe), "%s", nom);
        snprintf(shm_id_str, sizeof(shm_id_str), "%d", shm_id);
        snprintf(sem_id_str, sizeof(sem_id_str), "%d", sem_id);

        execl("./bin/GroupeISY", "GroupeISY", port_str, nom_groupe, moderateur, shm_id_str, sem_id_str, NULL);
        
        /* If execl fails */
        perror("Erreur: execl GroupeISY");
        exit(EXIT_FAILURE);
    }

    /* Parent process */
    groupes[index].pid_groupe = pid;
    nb_groupes++;

    printf("Groupe %s cree (port %d, PID %d)\n", nom, groupes[index].port, pid);
    return index;
}

/* Delete a group */
int supprimer_groupe(const char *nom, const char *demandeur) {
    int index = trouver_groupe(nom);
    if (index == -1) {
        return -1;  /* Group not found */
    }

    /* Check if requester is the moderator */
    if (strcmp(groupes[index].moderateur, demandeur) != 0) {
        return -2;  /* Not authorized */
    }

    /* Kill GroupeISY process */
    if (groupes[index].pid_groupe > 0) {
        kill(groupes[index].pid_groupe, SIGTERM);
        waitpid(groupes[index].pid_groupe, NULL, 0);
    }

    /* Destroy shared memory and semaphore */
    if (groupes[index].sem_id > 0) {
        supprimer_semaphore(groupes[index].sem_id);
    }
    if (groupes[index].shm_id > 0) {
        supprimer_shm(groupes[index].shm_id);
    }

    /* Mark as inactive */
    groupes[index].actif = 0;
    nb_groupes--;

    printf("Groupe %s supprime par %s\n", nom, demandeur);
    return 0;
}

/* List all groups */
void lister_groupes(struct_message *reponse) {
    char liste[100] = "";
    int count = 0;

    for (int i = 0; i < MAX_GROUPES && count < nb_groupes; i++) {
        if (groupes[i].actif) {
            if (count > 0) strcat(liste, ",");
            strncat(liste, groupes[i].nom, 99 - strlen(liste));
            count++;
        }
    }

    if (count == 0) {
        strcpy(liste, "Aucun groupe");
    }

    initialiser_message(reponse, ORDRE_INF, "Serveur", liste);
}

/* Get group connection information */
int info_connexion_groupe(const char *nom, struct_message *reponse) {
    int index = trouver_groupe(nom);
    if (index == -1) {
        return -1;
    }

    char info[100];
    snprintf(info, sizeof(info), "%d", groupes[index].port);
    initialiser_message(reponse, ORDRE_INF, "Serveur", info);
    
    groupes[index].nb_membres_actifs++;
    return 0;
}

/* Process incoming messages */
void traiter_message(struct_message *msg, struct sockaddr_in *client_addr) {
    struct_message reponse;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr->sin_port);

    printf("%s : Reception d'une demande %s\n", msg->Emetteur, msg->Ordre);

    if (strcmp(msg->Ordre, ORDRE_CRG) == 0) {
        /* Create group */
        int result = creer_groupe(msg->Texte, msg->Emetteur);
        if (result >= 0) {
            initialiser_message(&reponse, ORDRE_ACK, "Serveur", "Groupe cree");
            printf("Groupe %s cree par %s\n", msg->Texte, msg->Emetteur);
        } else {
            initialiser_message(&reponse, ORDRE_ERR, "Serveur", 
                result == -1 ? "Groupe existe deja" : "Impossible de creer le groupe");
        }
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);

    } else if (strcmp(msg->Ordre, ORDRE_SUG) == 0) {
        /* Delete group */
        int result = supprimer_groupe(msg->Texte, msg->Emetteur);
        if (result == 0) {
            initialiser_message(&reponse, ORDRE_ACK, "Serveur", "Groupe supprime");
        } else {
            initialiser_message(&reponse, ORDRE_ERR, "Serveur",
                result == -1 ? "Groupe inexistant" : "Non autorise");
        }
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);

    } else if (strcmp(msg->Ordre, ORDRE_LSG) == 0) {
        /* List groups */
        lister_groupes(&reponse);
        printf("Envoi %s : liste des groupes de discussions\n", msg->Emetteur);
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);

    } else if (strcmp(msg->Ordre, ORDRE_JOG) == 0) {
        /* Join group */
        if (info_connexion_groupe(msg->Texte, &reponse) == 0) {
            printf("%s : Reception d'une demande de connexion au groupe de discussion %s\n",
                   msg->Emetteur, msg->Texte);
            printf("Envoi %s : Informations de connexion au groupe\n", msg->Emetteur);
        } else {
            initialiser_message(&reponse, ORDRE_ERR, "Serveur", "Groupe inexistant");
        }
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);

    } else if (strcmp(msg->Ordre, ORDRE_CON) == 0) {
        /* Connection */
        initialiser_message(&reponse, ORDRE_ACK, "Serveur", "Connexion acceptee");
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);
        printf("%s connecte\n", msg->Emetteur);

    } else if (strcmp(msg->Ordre, ORDRE_DEC) == 0) {
        /* Disconnection */
        printf("%s deconnecte\n", msg->Emetteur);

    } else {
        /* Unknown command */
        initialiser_message(&reponse, ORDRE_ERR, "Serveur", "Commande inconnue");
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);
    }
}

/* Main function */
int main(int argc, char *argv[]) {
    struct_message msg;
    struct sockaddr_in client_addr;

    printf("=== ServeurISY - Serveur de messagerie instantanee ===\n\n");

    /* Install signal handler */
    signal(SIGINT, gestionnaire_signal);
    signal(SIGCHLD, SIG_IGN);  /* Avoid zombie processes */

    /* Read configuration */
    if (argc > 1) {
        lire_config_serveur(argv[1], &config);
    } else {
        lire_config_serveur("conf/serveur.conf", &config);
    }
    printf("Lecture du fichier de configuration OK\n");
    printf("IP: %s, Port: %d\n\n", config.ip_serveur, config.port_serveur);

    /* Initialize groups array */
    memset(groupes, 0, sizeof(groupes));

    /* Create and bind socket */
    sockfd_serveur = creer_socket_udp();
    lier_socket(sockfd_serveur, config.ip_serveur, config.port_serveur);

    printf("Serveur en ecoute sur %s:%d\n\n", config.ip_serveur, config.port_serveur);

    /* Main loop */
    while (serveur_actif) {
        if (recevoir_message(sockfd_serveur, &msg, &client_addr) > 0) {
            traiter_message(&msg, &client_addr);
        }
    }

    /* Cleanup: terminate all groups */
    printf("\nArret de tous les groupes de discussion...\n");
    for (int i = 0; i < MAX_GROUPES; i++) {
        if (groupes[i].actif && groupes[i].pid_groupe > 0) {
            printf("Arret du groupe %s...\n", groupes[i].nom);
            kill(groupes[i].pid_groupe, SIGTERM);
        }
    }

    /* Wait for all children */
    printf("Attente de la terminaison des processus groupes...\n");
    for (int i = 0; i < MAX_GROUPES; i++) {
        if (groupes[i].actif && groupes[i].pid_groupe > 0) {
            waitpid(groupes[i].pid_groupe, NULL, 0);
        }
    }

    /* Cleanup shared memory and semaphores */
    printf("Nettoyage des ressources partagees...\n");
    for (int i = 0; i < MAX_GROUPES; i++) {
        if (groupes[i].actif) {
            if (groupes[i].sem_id > 0) {
                supprimer_semaphore(groupes[i].sem_id);
            }
            if (groupes[i].shm_id > 0) {
                supprimer_shm(groupes[i].shm_id);
            }
        }
    }

    close(sockfd_serveur);
    printf("\nServeur arrete proprement\n");

    return 0;
}
