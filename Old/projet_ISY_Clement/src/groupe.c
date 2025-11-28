#include "../include/commun.h"
#include "../include/crypto.h"
#include "../include/stats.h"

/* Global variables */
volatile sig_atomic_t groupe_actif = 1;
int sockfd_groupe;
char nom_groupe[50];
char moderateur[20];
int port_groupe;
membre_info membres[MAX_MEMBRES_PAR_GROUPE];
int nb_membres = 0;
stats_groupe stats;
char cle_crypto[32] = "ISY_CLE_GROUPE_2025";

/* Shared memory */
shm_groupe *shm = NULL;
int shm_id = -1;
int sem_id = -1;

/* Sync members to shared memory */
void sync_membres_to_shm() {
    if (shm != NULL && sem_id >= 0) {
        P(sem_id);
        memcpy(shm->membres, membres, sizeof(membre_info) * MAX_MEMBRES_PAR_GROUPE);
        shm->nb_membres = nb_membres;
        V(sem_id);
    }
}

/* Signal handler */
void gestionnaire_signal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        printf("\n[Groupe %s] Reception signal, arret du groupe...\n", nom_groupe);
        groupe_actif = 0;
    }
}

/* Find member by name */
int trouver_membre(const char *nom) {
    for (int i = 0; i < nb_membres; i++) {
        if (strcmp(membres[i].nom, nom) == 0) {
            return i;
        }
    }
    return -1;
}

/* Add member to group */
int ajouter_membre(const char *nom, const char *ip, int port) {
    int index = trouver_membre(nom);
    
    if (index != -1) {
        /* Update existing member */
        strncpy(membres[index].ip, ip, 15);
        membres[index].ip[15] = '\0';
        membres[index].port = port;
        membres[index].derniere_activite = time(NULL);
        return index;
    }

    if (nb_membres >= MAX_MEMBRES_PAR_GROUPE) {
        return -1;  /* Group full */
    }

    /* Add new member */
    index = nb_membres;
    strncpy(membres[index].nom, nom, 19);
    membres[index].nom[19] = '\0';
    strncpy(membres[index].ip, ip, 15);
    membres[index].ip[15] = '\0';
    membres[index].port = port;
    membres[index].banni = 0;
    membres[index].derniere_activite = time(NULL);

    nb_membres++;

    /* Add to statistics */
    ajouter_membre_stats(&stats, nom);

    printf("[Groupe %s] Membre %s ajoute (%d/%d)\n",
           nom_groupe, nom, nb_membres, MAX_MEMBRES_PAR_GROUPE);

    /* Sync to shared memory */
    sync_membres_to_shm();

    return index;
}

/* Remove member from group */
int supprimer_membre(const char *nom) {
    int index = trouver_membre(nom);
    if (index == -1) {
        return -1;
    }

    /* Shift members array */
    for (int i = index; i < nb_membres - 1; i++) {
        membres[i] = membres[i + 1];
    }
    nb_membres--;

    printf("[Groupe %s] Membre %s supprime\n", nom_groupe, nom);

    /* Sync to shared memory */
    sync_membres_to_shm();

    return 0;
}

/* Ban member from group */
int bannir_membre(const char *nom) {
    int index = trouver_membre(nom);
    if (index == -1) {
        return -1;
    }

    membres[index].banni = 1;
    printf("[Groupe %s] Membre %s banni\n", nom_groupe, nom);

    /* Sync to shared memory */
    sync_membres_to_shm();

    return 0;
}

/* Redistribute message to all members */
void redistribuer_message(struct_message *msg, const char *emetteur) {
    char liste_membres[256] = "";
    int count = 0;

    /* Build member list for display */
    for (int i = 0; i < nb_membres; i++) {
        if (!membres[i].banni) {
            if (count > 0) strcat(liste_membres, ", ");
            strcat(liste_membres, membres[i].nom);
            count++;
        }
    }

    printf("Reception Message %s : %s\n", emetteur, msg->Texte);
    
    if (count > 0) {
        printf("Redistribution message a %s\n", liste_membres);

        /* Send to all members except banned ones */
        for (int i = 0; i < nb_membres; i++) {
            if (!membres[i].banni) {
                envoyer_message(sockfd_groupe, msg, membres[i].ip, membres[i].port);
            }
        }

        /* Update statistics */
        incrementer_message_membre(&stats, emetteur);
    } else {
        printf("Aucun membre pour redistribuer le message\n");
    }
}

/* List members of the group */
void lister_membres(struct_message *reponse) {
    char liste[100] = "";
    int count = 0;

    for (int i = 0; i < nb_membres; i++) {
        if (!membres[i].banni) {
            if (count > 0) strcat(liste, ",");
            strncat(liste, membres[i].nom, 99 - strlen(liste));
            count++;
        }
    }

    if (count == 0) {
        strcpy(liste, "Aucun membre");
    }

    initialiser_message(reponse, ORDRE_INF, "Groupe", liste);
}

/* Process incoming messages */
void traiter_message(struct_message *msg, struct sockaddr_in *client_addr) {
    struct_message reponse;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr->sin_port);

    if (strcmp(msg->Ordre, ORDRE_JOG) == 0) {
        /* Join group */
        if (ajouter_membre(msg->Emetteur, client_ip, client_port) >= 0) {
            initialiser_message(&reponse, ORDRE_ACK, "Groupe", nom_groupe);
            envoyer_message(sockfd_groupe, &reponse, client_ip, client_port);
            
            /* Notify all members */
            struct_message notif;
            char texte_notif[100];
            snprintf(texte_notif, sizeof(texte_notif), "%s a rejoint le groupe", msg->Emetteur);
            initialiser_message(&notif, ORDRE_INF, "Groupe", texte_notif);
            redistribuer_message(&notif, "Groupe");
        }

    } else if (strcmp(msg->Ordre, ORDRE_QUG) == 0) {
        /* Quit group */
        supprimer_membre(msg->Emetteur);
        
        /* Notify all members */
        struct_message notif;
        char texte_notif[100];
        snprintf(texte_notif, sizeof(texte_notif), "%s a quitte le groupe", msg->Emetteur);
        initialiser_message(&notif, ORDRE_INF, "Groupe", texte_notif);
        redistribuer_message(&notif, "Groupe");

    } else if (strcmp(msg->Ordre, ORDRE_MES) == 0) {
        /* Message - decrypt if needed, then redistribute */
        int index = trouver_membre(msg->Emetteur);
        if (index == -1 || membres[index].banni) {
            return;  /* Unknown or banned member */
        }
        
        /* Update last activity */
        membres[index].derniere_activite = time(NULL);
        
        /* Decrypt message */
        decrypter_message(msg->Texte, 100, cle_crypto);
        
        /* Redistribute */
        redistribuer_message(msg, msg->Emetteur);

    } else if (strcmp(msg->Ordre, ORDRE_LMG) == 0) {
        /* List members */
        lister_membres(&reponse);
        envoyer_message(sockfd_groupe, &reponse, client_ip, client_port);

    } else if (strcmp(msg->Ordre, ORDRE_MOD) == 0) {
        /* Moderate member (ban) */
        if (strcmp(msg->Emetteur, moderateur) == 0) {
            int index = trouver_membre(msg->Texte);
            if (index != -1) {
                /* Send direct notification to banned member */
                struct_message ban_notif;
                char ban_text[100];
                snprintf(ban_text, sizeof(ban_text),
                         "Vous avez ete banni du groupe %s par %s",
                         nom_groupe, moderateur);
                initialiser_message(&ban_notif, ORDRE_ERR, "Groupe", ban_text);
                envoyer_message(sockfd_groupe, &ban_notif, membres[index].ip, membres[index].port);

                /* Now ban the member */
                bannir_membre(msg->Texte);

                initialiser_message(&reponse, ORDRE_ACK, "Groupe", "Membre banni");

                /* Notify all other members */
                struct_message notif;
                char texte_notif[100];
                snprintf(texte_notif, sizeof(texte_notif),
                         "Action de %s : Membre %s supprime du groupe %s",
                         moderateur, msg->Texte, nom_groupe);
                initialiser_message(&notif, ORDRE_INF, "Groupe", texte_notif);
                redistribuer_message(&notif, "Groupe");
            } else {
                initialiser_message(&reponse, ORDRE_ERR, "Groupe", "Membre introuvable");
            }
        } else {
            initialiser_message(&reponse, ORDRE_ERR, "Groupe", "Non autorise");
        }
        envoyer_message(sockfd_groupe, &reponse, client_ip, client_port);

    } else if (strcmp(msg->Ordre, ORDRE_CMD) == 0) {
        /* Special commands */
        if (strcmp(msg->Texte, "stats") == 0) {
            afficher_stats_groupe(&stats);
        }
    }
}

/* Main function */
int main(int argc, char *argv[]) {
    struct_message msg;
    struct sockaddr_in client_addr;

    if (argc < 6) {
        fprintf(stderr, "Usage: %s <port> <nom_groupe> <moderateur> <shm_id> <sem_id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Parse arguments */
    port_groupe = atoi(argv[1]);
    strncpy(nom_groupe, argv[2], 49);
    nom_groupe[49] = '\0';
    strncpy(moderateur, argv[3], 19);
    moderateur[19] = '\0';
    shm_id = atoi(argv[4]);
    sem_id = atoi(argv[5]);

    printf("[Groupe %s] Lancement du groupe de discussion, moderateur %s\n",
           nom_groupe, moderateur);

    /* Attach to shared memory */
    if (shm_id >= 0) {
        shm = (shm_groupe *)attacher_shm(shm_id);
        if (shm == NULL) {
            perror("Erreur: attachement SHM");
            exit(EXIT_FAILURE);
        }
        printf("[Groupe %s] Attache a la memoire partagee (ID: %d)\n", nom_groupe, shm_id);
    }

    /* Install signal handler */
    signal(SIGTERM, gestionnaire_signal);
    signal(SIGINT, gestionnaire_signal);

    /* Initialize statistics */
    initialiser_stats_groupe(&stats, nom_groupe);

    /* Initialize members array */
    memset(membres, 0, sizeof(membres));

    /* Create and bind socket */
    sockfd_groupe = creer_socket_udp();
    lier_socket(sockfd_groupe, "0.0.0.0", port_groupe);

    printf("[Groupe %s] En ecoute sur le port %d\n\n", nom_groupe, port_groupe);

    /* Main loop */
    while (groupe_actif) {
        if (recevoir_message(sockfd_groupe, &msg, &client_addr) > 0) {
            traiter_message(&msg, &client_addr);
        }
    }

    /* Cleanup */
    printf("\n[Groupe %s] Arret du groupe\n", nom_groupe);

    /* Detach from shared memory */
    if (shm != NULL) {
        detacher_shm(shm);
        printf("[Groupe %s] Detache de la memoire partagee\n", nom_groupe);
    }

    close(sockfd_groupe);

    return 0;
}
