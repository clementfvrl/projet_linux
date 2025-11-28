#include "../include/commun.h"
#include "../include/crypto.h"

/* Global variables */
volatile sig_atomic_t affichage_actif = 1;
char nom_groupe[50];
char nom_utilisateur[20];
shm_affichage *shm = NULL;
int shm_id = -1;
int sem_id = -1;
int last_displayed = 0;

/* Signal handler */
void gestionnaire_signal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        affichage_actif = 0;
    }
}

/* Display message */
void afficher_message_groupe(struct_message *msg) {
    /* Messages are already decrypted by GroupeISY, just display */
    if (strcmp(msg->Ordre, ORDRE_MES) == 0) {
        printf("[%s]: %s\n", msg->Emetteur, msg->Texte);
    } else if (strcmp(msg->Ordre, ORDRE_INF) == 0) {
        /* Information message */
        if (strcmp(msg->Emetteur, "Groupe") == 0) {
            /* Check if it's an action message */
            if (strstr(msg->Texte, "Action de") != NULL) {
                printf("%s\n", msg->Texte);
            } else {
                printf("[Info]: %s\n", msg->Texte);
            }
        } else {
            printf("[%s]: %s\n", msg->Emetteur, msg->Texte);
        }
    } else if (strcmp(msg->Ordre, ORDRE_ACK) == 0) {
        /* Acknowledgment */
        printf("[Confirmation]: %s\n", msg->Texte);
    } else if (strcmp(msg->Ordre, ORDRE_ERR) == 0) {
        /* Error */
        printf("[ERREUR]: %s\n", msg->Texte);
    }
    fflush(stdout);
}

/* Main function */
int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <nom_groupe> <nom_utilisateur> <shm_id> <sem_id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Parse arguments */
    strncpy(nom_groupe, argv[1], 49);
    nom_groupe[49] = '\0';
    strncpy(nom_utilisateur, argv[2], 19);
    nom_utilisateur[19] = '\0';
    shm_id = atoi(argv[3]);
    sem_id = atoi(argv[4]);

    /* Display header */
    printf("\n");
    printf("=====================================================\n");
    printf("  Groupe de discussion: %s\n", nom_groupe);
    printf("  Utilisateur: %s\n", nom_utilisateur);
    printf("=====================================================\n");
    printf("\n");

    /* Install signal handler */
    signal(SIGTERM, gestionnaire_signal);
    signal(SIGINT, gestionnaire_signal);

    /* Attach to shared memory */
    shm = (shm_affichage *)attacher_shm(shm_id);
    if (shm == NULL) {
        perror("Erreur: attachement SHM");
        exit(EXIT_FAILURE);
    }
    printf("[Affichage %s] Attache a la memoire partagee (ID: %d)\n\n", nom_groupe, shm_id);

    /* Main loop - read messages from shared memory */
    while (affichage_actif) {
        /* Check if parent wants us to terminate */
        P(sem_id);
        int actif = shm->actif;
        int nb_messages = shm->nb_messages;
        V(sem_id);

        if (!actif) {
            break;
        }

        /* Display new messages */
        if (nb_messages > last_displayed) {
            P(sem_id);
            for (int i = last_displayed; i < shm->nb_messages && i < 100; i++) {
                afficher_message_groupe(&shm->messages[i]);
                last_displayed++;
            }
            V(sem_id);
        } else if (nb_messages == 100 && last_displayed == 100) {
            /* Buffer is full - reset and display the last message */
            P(sem_id);
            afficher_message_groupe(&shm->messages[99]);
            V(sem_id);
        }

        /* Sleep to avoid busy-waiting */
        usleep(100000);  /* 100ms */
    }

    /* Cleanup */
    printf("\n[Affichage %s] Fermeture de l'affichage\n", nom_groupe);
    if (shm != NULL) {
        detacher_shm(shm);
    }

    return 0;
}
