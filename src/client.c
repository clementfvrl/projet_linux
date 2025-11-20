#include "../include/commun.h"
#include "../include/crypto.h"
#include <sys/wait.h>
#include <sys/time.h>

/* Global variables */
volatile sig_atomic_t client_actif = 1;
int sockfd_client;
config_client config;
char cle_crypto[32] = "ISY_CLE_GROUPE_2025";

typedef struct {
    char nom[50];
    int port;
    pid_t pid_affichage;
    int actif;
    int shm_id;
    int sem_id;
    shm_affichage *shm;
} groupe_rejoint;

groupe_rejoint groupes_rejoints[10];
int nb_groupes_rejoints = 0;

/* Signal handler */
void gestionnaire_signal(int sig) {
    if (sig == SIGINT) {
        printf("\n\nReception signal SIGINT, arret du client...\n");
        client_actif = 0;
    }
}

/* Send message to server and wait for response */
int envoyer_et_recevoir(struct_message *envoi, struct_message *reponse) {
    struct sockaddr_in addr;
    
    if (envoyer_message(sockfd_client, envoi, config.ip_serveur, config.port_serveur) < 0) {
        return -1;
    }

    /* Set timeout for receive */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sockfd_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int ret = recevoir_message(sockfd_client, reponse, &addr);
    
    /* Remove timeout */
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(sockfd_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return ret;
}

/* Create a group */
void creer_groupe_cmd() {
    struct_message msg, reponse;
    char nom_groupe[50];

    printf("Saisir le nom du groupe\n");
    if (fgets(nom_groupe, sizeof(nom_groupe), stdin) != NULL) {
        nom_groupe[strcspn(nom_groupe, "\n")] = '\0';

        if (strlen(nom_groupe) == 0) {
            printf("Erreur: nom de groupe vide\n");
            return;
        }

        initialiser_message(&msg, ORDRE_CRG, config.nom_utilisateur, nom_groupe);
        printf("Envoi de la demande au serveur\n");

        if (envoyer_et_recevoir(&msg, &reponse) > 0) {
            if (strcmp(reponse.Ordre, ORDRE_ACK) == 0) {
                printf("Groupe cree !\n");
            } else {
                printf("Erreur: %s\n", reponse.Texte);
            }
        } else {
            printf("Erreur: pas de reponse du serveur\n");
        }
    }
}

/* List groups */
void lister_groupes_cmd() {
    struct_message msg, reponse;

    initialiser_message(&msg, ORDRE_LSG, config.nom_utilisateur, "");

    if (envoyer_et_recevoir(&msg, &reponse) > 0) {
        printf("Groupes disponibles : %s\n", reponse.Texte);
    } else {
        printf("Erreur: pas de reponse du serveur\n");
    }
}

/* Join a group */
void rejoindre_groupe_cmd() {
    struct_message msg, reponse;
    char nom_groupe[50];

    printf("Saisir le nom du groupe\n");
    if (fgets(nom_groupe, sizeof(nom_groupe), stdin) != NULL) {
        nom_groupe[strcspn(nom_groupe, "\n")] = '\0';

        if (strlen(nom_groupe) == 0) {
            printf("Erreur: nom de groupe vide\n");
            return;
        }

        /* Check if already joined */
        for (int i = 0; i < nb_groupes_rejoints; i++) {
            if (groupes_rejoints[i].actif &&
                strcmp(groupes_rejoints[i].nom, nom_groupe) == 0) {
                printf("Vous avez deja rejoint ce groupe\n");
                return;
            }
        }

        /* Request group info */
        initialiser_message(&msg, ORDRE_JOG, config.nom_utilisateur, nom_groupe);

        if (envoyer_et_recevoir(&msg, &reponse) > 0) {
            if (strcmp(reponse.Ordre, ORDRE_ERR) == 0) {
                printf("Erreur: %s\n", reponse.Texte);
                return;
            }

            int port_groupe = atoi(reponse.Texte);
            printf("Connexion au groupe %s realisee, lancement de l'affichage\n", nom_groupe);

            /* Create shared memory for AffichageISY */
            int shm_id = creer_shm(sizeof(shm_affichage));
            if (shm_id < 0) {
                perror("Erreur: creation SHM pour affichage");
                return;
            }

            /* Attach to shared memory */
            shm_affichage *shm = (shm_affichage *)attacher_shm(shm_id);
            if (shm == NULL) {
                perror("Erreur: attachement SHM pour affichage");
                supprimer_shm(shm_id);
                return;
            }

            /* Create semaphore for synchronization */
            int sem_id = creer_semaphore();
            if (sem_id < 0) {
                perror("Erreur: creation semaphore pour affichage");
                detacher_shm(shm);
                supprimer_shm(shm_id);
                return;
            }

            /* Initialize shared memory */
            shm->nb_messages = 0;
            shm->actif = 1;
            shm->shm_id = shm_id;
            shm->sem_id = sem_id;

            /* Fork AffichageISY process */
            pid_t pid = fork();
            if (pid < 0) {
                perror("Erreur: fork pour AffichageISY");
                supprimer_semaphore(sem_id);
                detacher_shm(shm);
                supprimer_shm(shm_id);
                return;
            }

            if (pid == 0) {
                /* Child process */
                char port_str[10], nom_str[50], shm_id_str[20], sem_id_str[20];
                snprintf(port_str, sizeof(port_str), "%d", port_groupe);
                snprintf(nom_str, sizeof(nom_str), "%s", nom_groupe);
                snprintf(shm_id_str, sizeof(shm_id_str), "%d", shm_id);
                snprintf(sem_id_str, sizeof(sem_id_str), "%d", sem_id);

                execl("./bin/AffichageISY", "AffichageISY", nom_str,
                      config.nom_utilisateur, shm_id_str, sem_id_str, NULL);

                perror("Erreur: execl AffichageISY");
                exit(EXIT_FAILURE);
            }

            /* Parent process - save group info */
            if (nb_groupes_rejoints < 10) {
                strncpy(groupes_rejoints[nb_groupes_rejoints].nom, nom_groupe, 49);
                groupes_rejoints[nb_groupes_rejoints].port = port_groupe;
                groupes_rejoints[nb_groupes_rejoints].pid_affichage = pid;
                groupes_rejoints[nb_groupes_rejoints].actif = 1;
                groupes_rejoints[nb_groupes_rejoints].shm_id = shm_id;
                groupes_rejoints[nb_groupes_rejoints].sem_id = sem_id;
                groupes_rejoints[nb_groupes_rejoints].shm = shm;
                nb_groupes_rejoints++;
            }

            /* Join the group via UDP - send to group port on server */
            struct_message join_msg;
            initialiser_message(&join_msg, ORDRE_JOG, config.nom_utilisateur, "");
            envoyer_message(sockfd_client, &join_msg, "127.0.0.1", port_groupe);
        }
    }
}

/* Dialog on a group */
void dialoguer_groupe_cmd() {
    struct_message msg;
    char nom_groupe[50];
    char ligne[200];

    printf("Saisir le nom du groupe\n");
    if (fgets(nom_groupe, sizeof(nom_groupe), stdin) != NULL) {
        nom_groupe[strcspn(nom_groupe, "\n")] = '\0';

        /* Find joined group */
        int index = -1;
        for (int i = 0; i < nb_groupes_rejoints; i++) {
            if (groupes_rejoints[i].actif && 
                strcmp(groupes_rejoints[i].nom, nom_groupe) == 0) {
                index = i;
                break;
            }
        }

        if (index == -1) {
            printf("Erreur: vous n'avez pas rejoint ce groupe\n");
            return;
        }

        printf("Tapez 'quit' pour revenir au menu, 'cmd' pour entrer une commande, 'msg' pour revenir aux messages\n");
        printf("Les messages des autres membres s'afficheront automatiquement\n\n");

        int mode_message = 1;
        fd_set readfds;
        struct timeval timeout;
        int show_prompt = 1;

        while (1) {
            /* Display prompt only when needed */
            if (show_prompt) {
                if (mode_message) {
                    printf("Message : ");
                } else {
                    printf("Commande : ");
                }
                fflush(stdout);
                show_prompt = 0;
            }

            /* Use select to monitor both stdin and socket */
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            FD_SET(sockfd_client, &readfds);

            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int activity = select(sockfd_client + 1, &readfds, NULL, NULL, &timeout);

            /* Check for incoming messages from group */
            if (activity > 0 && FD_ISSET(sockfd_client, &readfds)) {
                struct_message incoming_msg;
                struct sockaddr_in incoming_addr;
                if (recevoir_message(sockfd_client, &incoming_msg, &incoming_addr) > 0) {
                    /* Write message to shared memory for AffichageISY to display */
                    shm_affichage *shm = groupes_rejoints[index].shm;
                    if (shm != NULL && groupes_rejoints[index].sem_id >= 0) {
                        P(groupes_rejoints[index].sem_id);
                        if (shm->nb_messages >= 100) {
                            /* Buffer full - shift all messages left and add new one at the end */
                            memmove(&shm->messages[0], &shm->messages[1],
                                    sizeof(struct_message) * 99);
                            memcpy(&shm->messages[99], &incoming_msg, sizeof(struct_message));
                        } else {
                            /* Normal case - add to end */
                            memcpy(&shm->messages[shm->nb_messages], &incoming_msg, sizeof(struct_message));
                            shm->nb_messages++;
                        }
                        V(groupes_rejoints[index].sem_id);
                    }

                    /* Handle error messages that require breaking the loop */
                    if (strcmp(incoming_msg.Ordre, ORDRE_ERR) == 0) {
                        if (strstr(incoming_msg.Texte, "banni") != NULL ||
                            strstr(incoming_msg.Texte, "exclu") != NULL) {
                            printf("\r\033[K");  /* Clear current line */
                            printf("Vous avez ete exclu du groupe. Retour au menu.\n");
                            break;
                        }
                    }
                    show_prompt = 1;  /* Show prompt again after message */
                }
                continue;
            }

            /* Check for user input */
            if (activity > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                if (fgets(ligne, sizeof(ligne), stdin) == NULL) {
                    break;
                }
                show_prompt = 1;  /* Show prompt for next input */

                ligne[strcspn(ligne, "\n")] = '\0';

                if (strcmp(ligne, "quit") == 0) {
                    break;
                } else if (strcmp(ligne, "cmd") == 0) {
                    mode_message = 0;
                    continue;
                } else if (strcmp(ligne, "msg") == 0) {
                    mode_message = 1;
                    continue;
                }

                if (strlen(ligne) == 0) {
                    continue;
                }

            if (mode_message) {
                /* Send message */
                char texte_crypte[100];
                strncpy(texte_crypte, ligne, 99);
                texte_crypte[99] = '\0';
                crypter_message(texte_crypte, 100, cle_crypto);

                initialiser_message(&msg, ORDRE_MES, config.nom_utilisateur, texte_crypte);
                envoyer_message(sockfd_client, &msg, "127.0.0.1",
                              groupes_rejoints[index].port);
            } else {
                /* Send command */
                if (strncmp(ligne, "list", 4) == 0) {
                    initialiser_message(&msg, ORDRE_LMG, config.nom_utilisateur, "");
                    struct_message reponse;
                    struct sockaddr_in addr;

                    envoyer_message(sockfd_client, &msg, "127.0.0.1",
                                  groupes_rejoints[index].port);
                    
                    /* Wait for response */
                    struct timeval tv = {2, 0};
                    setsockopt(sockfd_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    if (recevoir_message(sockfd_client, &reponse, &addr) > 0) {
                        printf("Membres : %s\n", reponse.Texte);
                    }
                    tv.tv_sec = 0;
                    setsockopt(sockfd_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

                } else if (strncmp(ligne, "delete ", 7) == 0) {
                    char *nom_membre = ligne + 7;
                    initialiser_message(&msg, ORDRE_MOD, config.nom_utilisateur, nom_membre);
                    envoyer_message(sockfd_client, &msg, "127.0.0.1",
                                  groupes_rejoints[index].port);
                    printf("Demande de suppression de %s envoyee\n", nom_membre);
                } else {
                    printf("Commandes: list, delete <nom>\n");
                }
                }
            }  /* End of user input block */
        }  /* End of dialogue while loop */
    }
}

/* Quit and cleanup */
void quitter() {
    printf("Demande de deconnexion des groupes\n");

    /* Quit all groups */
    for (int i = 0; i < nb_groupes_rejoints; i++) {
        if (groupes_rejoints[i].actif) {
            struct_message msg;
            initialiser_message(&msg, ORDRE_QUG, config.nom_utilisateur, "");
            envoyer_message(sockfd_client, &msg, config.ip_serveur, 
                          groupes_rejoints[i].port);
        }
    }

    /* Terminate all display processes and cleanup shared memory */
    printf("Attente de fermeture des affichages\n");
    for (int i = 0; i < nb_groupes_rejoints; i++) {
        if (groupes_rejoints[i].actif) {
            /* Signal display process to terminate */
            if (groupes_rejoints[i].shm != NULL && groupes_rejoints[i].sem_id >= 0) {
                P(groupes_rejoints[i].sem_id);
                groupes_rejoints[i].shm->actif = 0;
                V(groupes_rejoints[i].sem_id);
            }

            /* Kill display process */
            if (groupes_rejoints[i].pid_affichage > 0) {
                kill(groupes_rejoints[i].pid_affichage, SIGTERM);
                waitpid(groupes_rejoints[i].pid_affichage, NULL, 0);
            }

            /* Cleanup shared memory and semaphore */
            if (groupes_rejoints[i].shm != NULL) {
                detacher_shm(groupes_rejoints[i].shm);
            }
            if (groupes_rejoints[i].sem_id >= 0) {
                supprimer_semaphore(groupes_rejoints[i].sem_id);
            }
            if (groupes_rejoints[i].shm_id >= 0) {
                supprimer_shm(groupes_rejoints[i].shm_id);
            }
        }
    }
    printf("Affichages clos\n");

    /* Disconnect from server */
    struct_message msg;
    initialiser_message(&msg, ORDRE_DEC, config.nom_utilisateur, "");
    envoyer_message(sockfd_client, &msg, config.ip_serveur, config.port_serveur);

    printf("Fin du programme\n");
}

/* Main menu */
void menu() {
    int choix;

    while (client_actif) {
        printf("\nChoix des commandes :\n");
        printf("  0 Creation de groupe\n");
        printf("  1 Rejoindre un groupe\n");
        printf("  2 Lister les groupes\n");
        printf("  3 Dialoguer sur un groupe\n");
        printf("  4 Quitter\n");
        printf("Choix :\n>");

        if (scanf("%d", &choix) != 1) {
            if (!client_actif) {
                /* CTRL-C pressed during input */
                quitter();
                return;
            }
            while (getchar() != '\n');  /* Clear input buffer */
            printf("Erreur: choix invalide\n");
            continue;
        }
        while (getchar() != '\n');  /* Clear newline */

        if (!client_actif) {
            /* CTRL-C pressed */
            quitter();
            return;
        }

        switch (choix) {
            case 0:
                creer_groupe_cmd();
                break;
            case 1:
                rejoindre_groupe_cmd();
                break;
            case 2:
                lister_groupes_cmd();
                break;
            case 3:
                dialoguer_groupe_cmd();
                break;
            case 4:
                quitter();
                return;
            default:
                printf("Choix invalide\n");
        }
    }

    /* If we exit the loop due to signal */
    quitter();
}

/* Main function */
int main(int argc, char *argv[]) {
    struct_message msg, reponse;

    printf("=== ClientISY - Client de messagerie ===\n\n");

    /* Install signal handler */
    signal(SIGINT, gestionnaire_signal);
    signal(SIGCHLD, SIG_IGN);

    /* Read configuration */
    if (argc > 1) {
        if (lire_config_client(argv[1], &config) < 0) {
            exit(EXIT_FAILURE);
        }
    } else {
        if (lire_config_client("conf/client.conf", &config) < 0) {
            exit(EXIT_FAILURE);
        }
    }
    printf("Lecture du fichier de configuration OK, utilisateur %s\n", 
           config.nom_utilisateur);

    /* Initialize groups array */
    memset(groupes_rejoints, 0, sizeof(groupes_rejoints));

    /* Create socket */
    sockfd_client = creer_socket_udp();

    /* Bind to any port */
    lier_socket(sockfd_client, "0.0.0.0", 0);

    /* Connect to server */
    initialiser_message(&msg, ORDRE_CON, config.nom_utilisateur, "");
    if (envoyer_et_recevoir(&msg, &reponse) > 0) {
        if (strcmp(reponse.Ordre, ORDRE_ACK) == 0) {
            printf("Connexion au serveur reussie\n");
        }
    }

    /* Main menu */
    menu();

    /* Cleanup */
    close(sockfd_client);

    return 0;
}
