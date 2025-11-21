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
int creer_groupe(const char *nom, const char *moderateur, const char *password, int max_membres) {
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

    /* Set password (empty if no password) */
    if (password != NULL && strlen(password) > 0) {
        strncpy(groupes[index].password, password, 31);
        groupes[index].password[31] = '\0';
    } else {
        groupes[index].password[0] = '\0';
    }

    groupes[index].num_groupe = index + 1;
    groupes[index].port = PORT_GROUPE_BASE + index;
    groupes[index].max_membres = (max_membres > 0 && max_membres <= MAX_MEMBRES_PAR_GROUPE) ?
                                  max_membres : MAX_MEMBRES_PAR_GROUPE;
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

/* Merge two groups - source group is merged into destination group */
int fusionner_groupes(const char *nom_dest, const char *nom_source, const char *demandeur) {
    /* Find both groups */
    int index_dest = trouver_groupe(nom_dest);
    int index_source = trouver_groupe(nom_source);

    /* Validate groups exist */
    if (index_dest == -1) {
        return -1;  /* Destination group not found */
    }
    if (index_source == -1) {
        return -2;  /* Source group not found */
    }

    /* Check if same group */
    if (index_dest == index_source) {
        return -3;  /* Cannot merge group with itself */
    }

    /* Check if requester is moderator of BOTH groups */
    if (strcmp(groupes[index_dest].moderateur, demandeur) != 0) {
        return -4;  /* Not moderator of destination group */
    }
    if (strcmp(groupes[index_source].moderateur, demandeur) != 0) {
        return -5;  /* Not moderator of source group */
    }

    printf("Fusion des groupes: %s <- %s par %s\n", nom_dest, nom_source, demandeur);

    /* Access shared memory of both groups */
    shm_groupe *shm_dest = (shm_groupe *)attacher_shm(groupes[index_dest].shm_id);
    shm_groupe *shm_source = (shm_groupe *)attacher_shm(groupes[index_source].shm_id);

    if (shm_dest == NULL || shm_source == NULL) {
        if (shm_dest != NULL) detacher_shm(shm_dest);
        if (shm_source != NULL) detacher_shm(shm_source);
        return -6;  /* Failed to access shared memory */
    }

    /* Transfer members from source to destination via shared memory */
    P(groupes[index_dest].sem_id);
    P(groupes[index_source].sem_id);

    int membres_transferes = 0;
    for (int i = 0; i < shm_source->nb_membres && i < MAX_MEMBRES_PAR_GROUPE; i++) {
        /* Check if destination has space */
        if (shm_dest->nb_membres >= MAX_MEMBRES_PAR_GROUPE) {
            printf("Attention: groupe destination plein, impossible de transferer tous les membres\n");
            break;
        }

        /* Check if member already exists in destination */
        int existe = 0;
        for (int j = 0; j < shm_dest->nb_membres; j++) {
            if (strcmp(shm_dest->membres[j].nom, shm_source->membres[i].nom) == 0) {
                existe = 1;
                break;
            }
        }

        /* Add member if not already present */
        if (!existe) {
            memcpy(&shm_dest->membres[shm_dest->nb_membres],
                   &shm_source->membres[i],
                   sizeof(membre_info));
            shm_dest->nb_membres++;
            membres_transferes++;
        }
    }

    /* Update group statistics */
    groupes[index_dest].nb_membres_actifs += membres_transferes;
    shm_dest->groupe.nb_membres_actifs = groupes[index_dest].nb_membres_actifs;

    V(groupes[index_source].sem_id);
    V(groupes[index_dest].sem_id);

    /* Send notification to all members of destination group */
    struct_message notif_dest;
    char texte_notif[100];
    snprintf(texte_notif, sizeof(texte_notif),
             "Groupe %s fusionne avec ce groupe (%d membres transferes)",
             nom_source, membres_transferes);
    initialiser_message(&notif_dest, ORDRE_INF, "Serveur", texte_notif);

    /* Send to all destination group members */
    for (int i = 0; i < shm_dest->nb_membres; i++) {
        envoyer_message(sockfd_serveur, &notif_dest,
                       shm_dest->membres[i].ip,
                       shm_dest->membres[i].port);
    }

    /* Send notification to source group members about redirect */
    struct_message notif_source;
    snprintf(texte_notif, sizeof(texte_notif),
             "Ce groupe va fusionner avec %s (port %d)",
             nom_dest, groupes[index_dest].port);
    initialiser_message(&notif_source, ORDRE_INF, "Serveur", texte_notif);

    for (int i = 0; i < shm_source->nb_membres; i++) {
        envoyer_message(sockfd_serveur, &notif_source,
                       shm_source->membres[i].ip,
                       shm_source->membres[i].port);
    }

    /* Detach shared memory */
    detacher_shm(shm_dest);
    detacher_shm(shm_source);

    printf("Fusion terminee: %d membres transferes de %s vers %s\n",
           membres_transferes, nom_source, nom_dest);

    /* Delete the source group */
    if (groupes[index_source].pid_groupe > 0) {
        kill(groupes[index_source].pid_groupe, SIGTERM);
        waitpid(groupes[index_source].pid_groupe, NULL, 0);
    }

    /* Destroy shared memory and semaphore of source group */
    if (groupes[index_source].sem_id > 0) {
        supprimer_semaphore(groupes[index_source].sem_id);
    }
    if (groupes[index_source].shm_id > 0) {
        supprimer_shm(groupes[index_source].shm_id);
    }

    /* Mark source as inactive */
    groupes[index_source].actif = 0;
    nb_groupes--;

    return 0;  /* Success */
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

/* Get group connection information with password verification */
int info_connexion_groupe(const char *nom, const char *password, struct_message *reponse) {
    int index = trouver_groupe(nom);
    if (index == -1) {
        return -1;  /* Group not found */
    }

    /* Check member limit */
    if (groupes[index].nb_membres_actifs >= groupes[index].max_membres) {
        return -2;  /* Group is full */
    }

    /* Check password if group has one */
    if (strlen(groupes[index].password) > 0) {
        if (password == NULL || strcmp(groupes[index].password, password) != 0) {
            return -3;  /* Incorrect password */
        }
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

    if (strcmp(msg->Ordre, ORDRE_LOG) == 0) {
        /* Login authentication - format: "username:password" */
        char username[20], password[32];
        if (sscanf(msg->Texte, "%19[^:]:%31s", username, password) == 2) {
            if (verifier_utilisateur(username, password)) {
                initialiser_message(&reponse, ORDRE_ACK, "Serveur", "Authentification reussie");
                printf("Authentification reussie pour %s\n", username);
            } else {
                initialiser_message(&reponse, ORDRE_ERR, "Serveur", "Mot de passe incorrect");
                printf("Echec d'authentification pour %s\n", username);
            }
        } else {
            initialiser_message(&reponse, ORDRE_ERR, "Serveur", "Format invalide");
        }
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);

    } else if (strcmp(msg->Ordre, ORDRE_CRG) == 0) {
        /* Create group - format: "nom_groupe:password:max_membres" */
        char nom_groupe[50], password[32];
        int max_membres = MAX_MEMBRES_PAR_GROUPE;

        /* Parse with optional password and max members */
        int parsed = sscanf(msg->Texte, "%49[^:]:%31[^:]:%d", nom_groupe, password, &max_membres);
        if (parsed < 1) {
            initialiser_message(&reponse, ORDRE_ERR, "Serveur", "Format invalide");
            envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);
            return;
        }

        /* If no password/max_membres, set defaults */
        if (parsed < 2) password[0] = '\0';
        if (parsed < 3) max_membres = MAX_MEMBRES_PAR_GROUPE;

        int result = creer_groupe(nom_groupe, msg->Emetteur, password, max_membres);
        if (result >= 0) {
            initialiser_message(&reponse, ORDRE_ACK, "Serveur", "Groupe cree");
            printf("Groupe %s cree par %s (max: %d membres)\n", nom_groupe, msg->Emetteur, max_membres);
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
        /* Join group - format: "nom_groupe:password" */
        char nom_groupe[50], password[32];
        int parsed = sscanf(msg->Texte, "%49[^:]:%31s", nom_groupe, password);

        /* If no password provided, set empty */
        if (parsed < 2) password[0] = '\0';

        int result = info_connexion_groupe(nom_groupe, password, &reponse);
        if (result == 0) {
            printf("%s : Reception d'une demande de connexion au groupe de discussion %s\n",
                   msg->Emetteur, nom_groupe);
            printf("Envoi %s : Informations de connexion au groupe\n", msg->Emetteur);
        } else {
            const char *erreur;
            switch (result) {
                case -1: erreur = "Groupe inexistant"; break;
                case -2: erreur = "Groupe complet"; break;
                case -3: erreur = "Mot de passe incorrect"; break;
                default: erreur = "Erreur inconnue"; break;
            }
            initialiser_message(&reponse, ORDRE_ERR, "Serveur", erreur);
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

    } else if (strcmp(msg->Ordre, ORDRE_FUS) == 0) {
        /* Merge groups - format: "groupe_dest,groupe_source" */
        char nom_dest[50], nom_source[50];
        if (sscanf(msg->Texte, "%49[^,],%49s", nom_dest, nom_source) == 2) {
            int result = fusionner_groupes(nom_dest, nom_source, msg->Emetteur);
            if (result == 0) {
                initialiser_message(&reponse, ORDRE_ACK, "Serveur", "Groupes fusionnes");
                printf("%s : Fusion des groupes %s et %s reussie\n",
                       msg->Emetteur, nom_dest, nom_source);
            } else {
                const char *erreur;
                switch (result) {
                    case -1: erreur = "Groupe destination inexistant"; break;
                    case -2: erreur = "Groupe source inexistant"; break;
                    case -3: erreur = "Impossible de fusionner un groupe avec lui-meme"; break;
                    case -4: erreur = "Vous n'etes pas moderateur du groupe destination"; break;
                    case -5: erreur = "Vous n'etes pas moderateur du groupe source"; break;
                    case -6: erreur = "Erreur d'acces a la memoire partagee"; break;
                    default: erreur = "Erreur inconnue"; break;
                }
                initialiser_message(&reponse, ORDRE_ERR, "Serveur", erreur);
            }
        } else {
            initialiser_message(&reponse, ORDRE_ERR, "Serveur",
                "Format invalide: utilisez 'groupe_dest,groupe_source'");
        }
        envoyer_message(sockfd_serveur, &reponse, client_ip, client_port);

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
