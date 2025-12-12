/**********************************************
 * FICHIER : ServeurISY.c
 * Rôle   : Serveur principal ISY
 *********************************************/

#include "commun.h"
#include <signal.h>
#include <time.h>

struct UserInfo {
    char nom[ISY_TAILLE_NOM];
    int  actif; // 0 = inactif, 1 = actif 
};

/* CORRECTION 1 : Ajout de 'struct' devant UserInfo */
static struct UserInfo g_users[ISY_MAX_MEMBRES]; 

static int sock_serveur = -1;
static GroupeServeur g_groupes[ISY_MAX_GROUPES];

static void init_groupes(void)
{
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        g_groupes[i].actif = 0;
        g_groupes[i].id = i;
        g_groupes[i].port  = 0;
        g_groupes[i].nom[0] = '\0';
        g_groupes[i].pid = 0;
        g_groupes[i].moderateurName[0] = '\0';
    }
}

/* Trouve un slot libre pour un nouveau groupe, renvoie index ou -1 */
static int trouver_slot_groupe(void)
{
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        if (!g_groupes[i].actif) return i;
    }
    return -1;
}

/* Trouve un groupe par son nom, renvoie index ou -1 */
static int trouver_groupe_par_nom(const char *nom)
{
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        if (g_groupes[i].actif && strcmp(g_groupes[i].nom, nom) == 0) {
            return i;
        }
    }
    return -1;
}

/* Lance un processus GroupeISY pour ce groupe (port dédié) */
static int lancer_groupe_process(int idGroupe)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork GroupeISY");
        return -1;
    }
    if (pid == 0) {
        /* Processus fils : fermer le socket hérité du serveur */
        if (sock_serveur >= 0) {
            close(sock_serveur);
        }

        /* execl GroupeISY */
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", g_groupes[idGroupe].port);
        /* Passer aussi le nom du modérateur en argument */
        execl("./bin/GroupeISY", "GroupeISY", portStr, g_groupes[idGroupe].moderateurName, (char *)NULL);
        perror("execl GroupeISY");
        exit(EXIT_FAILURE);
    }
    /* Père : stocker le pid du groupe */
    g_groupes[idGroupe].pid = (int)pid;
    return 0;
}

/* Gère la création de groupe (ordre "CRG") */
static void traiter_creation_groupe(const MessageISY *msgReq,
                                    MessageISY *msgRep)
{
    /* msgReq->Texte = nom groupe */
    const char *nomG = msgReq->Texte;

    /* Validation du nom de groupe */
    if (!valider_nom(nomG)) {
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Nom de groupe invalide (lettres, chiffres, _, -, . uniquement)");
        return;
    }

    if (trouver_groupe_par_nom(nomG) >= 0) {
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Groupe '%s' existe deja", nomG);
        return;
    }

    int idx = trouver_slot_groupe();
    if (idx < 0) {
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Nombre max de groupes atteint");
        return;
    }

    g_groupes[idx].actif = 1;
    g_groupes[idx].port  = 8100 + idx;     /* ex: 8100, 8101, ... */
    strncpy(g_groupes[idx].nom, nomG, sizeof(g_groupes[idx].nom) - 1);
    g_groupes[idx].nom[sizeof(g_groupes[idx].nom) - 1] = '\0';
    /* Stocker le nom du créateur comme modérateur */
    strncpy(g_groupes[idx].moderateurName, msgReq->Emetteur, ISY_TAILLE_NOM - 1);
    g_groupes[idx].moderateurName[ISY_TAILLE_NOM - 1] = '\0';

    if (lancer_groupe_process(idx) < 0) {
        g_groupes[idx].actif = 0;
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Echec lancement GroupeISY");
        return;
    }

    snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ACK");
    snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
             "Groupe '%s' cree sur port %d", nomG, g_groupes[idx].port);
}

/* Gère la demande de liste ("LST") */
static void traiter_liste_groupes(MessageISY *msgRep)
{
    snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ACK");
    msgRep->Texte[0] = '\0';

    char line[64];
    int found = 0;
    size_t current_len = 0;

    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        if (g_groupes[i].actif) {
            snprintf(line, sizeof(line), "%s (port %d)\n",
                     g_groupes[i].nom, g_groupes[i].port);
            size_t line_len = strlen(line);

            /* Vérification sécurisée avec espace pour null terminator */
            if (current_len + line_len + 1 <= ISY_TAILLE_TEXTE) {
                strcpy(msgRep->Texte + current_len, line);
                current_len += line_len;
                found = 1;
            } else {
                break; /* buffer plein, on coupe */
            }
        }
    }
    if (!found) {
        strncpy(msgRep->Texte, "Aucun groupe\n", ISY_TAILLE_TEXTE - 1);
        msgRep->Texte[ISY_TAILLE_TEXTE - 1] = '\0';
    }
}

/* Gère la demande de join ("JNG") :
 * - msgReq->Texte = nom groupe
 * - réponse : ACK + texte = "OK <port>" ou ERR
 */
static void traiter_join_groupe(const MessageISY *msgReq,
                                MessageISY *msgRep)
{
    const char *nomG = msgReq->Texte;
    int idx = trouver_groupe_par_nom(nomG);
    if (idx < 0) {
        snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ERR");
        snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
                 "Groupe '%s' introuvable", nomG);
        return;
    }

    snprintf(msgRep->Ordre, ISY_TAILLE_ORDRE, "ACK");
    snprintf(msgRep->Texte, ISY_TAILLE_TEXTE,
             "OK %d", g_groupes[idx].port);
}

/* Handler SIGINT pour arrêter proprement */
static volatile sig_atomic_t g_stop = 0;
static void sigint_handler(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* Handler SIGCHLD pour éviter les zombies */
static void sigchld_handler(int sig)
{
    (void)sig;
    /* Récolter tous les processus fils terminés (non-bloquant) */
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* Continue à récolter tant qu'il y a des processus terminés */
    }
    errno = saved_errno;
}

/* ==== MAIN ==== */
int main(void)
{
    struct sockaddr_in addrServ, addrCli;
    socklen_t lenCli = sizeof(addrCli);

    /* Installation des handlers de signaux avec sigaction (plus sûr que signal) */
    struct sigaction sa_int, sa_chld;

    /* Handler SIGINT */
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) < 0) {
        perror("sigaction SIGINT");
        exit(EXIT_FAILURE);
    }

    /* Handler SIGCHLD */
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; /* SA_RESTART pour éviter EINTR, SA_NOCLDSTOP pour ignorer les STOP */
    if (sigaction(SIGCHLD, &sa_chld, NULL) < 0) {
        perror("sigaction SIGCHLD");
        exit(EXIT_FAILURE);
    }

    init_groupes();
    /* --- AJOUT : Initialisation du tableau des utilisateurs à 0 --- */
    memset(&g_users, 0, sizeof(g_users));

    sock_serveur = creer_socket_udp();
    if (sock_serveur < 0) {
        exit(EXIT_FAILURE);
    }

    init_sockaddr(&addrServ, ISY_IP_SERVEUR, ISY_PORT_SERVEUR);
    if (bind(sock_serveur, (struct sockaddr *)&addrServ, sizeof(addrServ)) < 0) {
        perror("bind ServeurISY");
        fprintf(stderr, "Impossible de se lier au port %d. Verifiez qu'aucun autre processus n'utilise ce port.\n", ISY_PORT_SERVEUR);
        close(sock_serveur);
        exit(EXIT_FAILURE);
    }

    printf("ServeurISY : en ecoute sur %s:%d\n",
           ISY_IP_SERVEUR, ISY_PORT_SERVEUR);

    while (!g_stop) {
        MessageISY msgReq, msgRep;
        ssize_t n = recvfrom(sock_serveur, &msgReq, sizeof(msgReq), 0,
                             (struct sockaddr *)&addrCli, &lenCli);
        if (n < 0) {
            if (errno == EINTR && g_stop) break;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Timeout atteint, pas d'erreur, on continue */
                continue;
            }
            perror("recvfrom ServeurISY");
            continue;
        }

        /* Validation de la taille du message reçu */
        if ((size_t)n != sizeof(msgReq)) {
            fprintf(stderr, "Message incomplet recu (%zd octets au lieu de %zu), ignore\n",
                    n, sizeof(msgReq));
            continue;
        }

        msgReq.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msgReq.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msgReq.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        /* Validation du champ Ordre */
        if (!valider_ordre(msgReq.Ordre)) {
            fprintf(stderr, "Ordre invalide recu: '%s', ignore\n", msgReq.Ordre);
            continue;
        }

        printf("\n--- Recu du client ---\n");
        afficher_message_debug("Serveur", &msgReq);

        memset(&msgRep, 0, sizeof(msgRep));
        strncpy(msgRep.Emetteur, "Serveur", ISY_TAILLE_NOM - 1);

        /* --- AJOUT : Logique de Connexion (CON) --- */
        if (strcmp(msgReq.Ordre, "CON") == 0) {
            /* Validation du nom d'utilisateur */
            if (!valider_nom(msgReq.Emetteur)) {
                snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "REP");
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "INVALID");
            } else {
                int existe = 0;
                int libre_idx = -1;

                /* Vérification unicité */
            for (int i = 0; i < ISY_MAX_MEMBRES; i++) {
                if (g_users[i].actif && strncmp(g_users[i].nom, msgReq.Emetteur, ISY_TAILLE_NOM) == 0) {
                    existe = 1;
                }
                if (!g_users[i].actif && libre_idx == -1) {
                    libre_idx = i;
                }
            }

            snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "REP");
            
            if (existe) {
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "KO"); /* Pseudo déjà pris */
            } else if (libre_idx != -1) {
                /* Enregistrement */
                g_users[libre_idx].actif = 1;
                strncpy(g_users[libre_idx].nom, msgReq.Emetteur, ISY_TAILLE_NOM - 1);
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "OK");
                printf("Nouvel utilisateur connecte : %s\n", msgReq.Emetteur);
            } else {
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "FULL"); /* Serveur plein */
            }
            } /* Fin du else de validation */

        /* --- AJOUT : Logique de Déconnexion (DEC) --- */
        } else if (strcmp(msgReq.Ordre, "DEC") == 0) {
            for (int i = 0; i < ISY_MAX_MEMBRES; i++) {
                if (g_users[i].actif && strncmp(g_users[i].nom, msgReq.Emetteur, ISY_TAILLE_NOM) == 0) {
                    g_users[i].actif = 0;
                    printf("Utilisateur deconnecte : %s\n", msgReq.Emetteur);
                    break;
                }
            }
            snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ACK");
            snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "Au revoir");

        /* CORRECTION 3 : Ajout du 'else' et fermeture de l'accolade manquante */
        } else if (strcmp(msgReq.Ordre, "CRG") == 0) {
            traiter_creation_groupe(&msgReq, &msgRep);
        } else if (strcmp(msgReq.Ordre, "LST") == 0) {
            traiter_liste_groupes(&msgRep);
        } else if (strcmp(msgReq.Ordre, "JNG") == 0) {
            traiter_join_groupe(&msgReq, &msgRep);
        } else if (strcmp(msgReq.Ordre, "DEL") == 0) {
            /* Demande de suppression de groupe */
            int idx = trouver_groupe_par_nom(msgReq.Texte);
            
            if (idx < 0 || !g_groupes[idx].actif) {
                snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ERR");
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "Groupe introuvable");
            } 
            else if (strncmp(g_groupes[idx].moderateurName, msgReq.Emetteur, ISY_TAILLE_NOM) != 0) {
                snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ERR");
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "Refuse : vous n'etes pas moderateur");
            } 
            else {
                /* 1. AVANT de tuer le processus, envoyer une notification DEL aux membres */
                MessageISY delMsg;
                memset(&delMsg, 0, sizeof(delMsg));
                strncpy(delMsg.Ordre, "DEC", ISY_TAILLE_ORDRE - 1); /* DEC = Déconnexion forcée */
                strncpy(delMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                snprintf(delMsg.Texte, ISY_TAILLE_TEXTE,
                         "Le groupe '%s' a été supprimé par le modérateur", msgReq.Texte);

                struct sockaddr_in addrG;
                init_sockaddr(&addrG, ISY_IP_SERVEUR, g_groupes[idx].port);
                if (sendto(sock_serveur, &delMsg, sizeof(delMsg), 0,
                          (struct sockaddr *)&addrG, sizeof(addrG)) < 0) {
                    perror("sendto DEC suppression");
                }

                /* Petit délai pour s'assurer que le message DEC est traité */
                struct timespec delay = {0, 200000000}; /* 200ms */
                nanosleep(&delay, NULL);

                /* 2. Tuer le processus de groupe */
                if (g_groupes[idx].pid > 0) {
                    if (kill(g_groupes[idx].pid, SIGINT) < 0) {
                        perror("kill SIGINT");
                    }
                    /* Note: SIGCHLD handler se charge de récolter le zombie */
                }

                /* 3. Nettoyer la structure */
                g_groupes[idx].actif = 0;
                g_groupes[idx].port = 0;
                g_groupes[idx].nom[0] = '\0';
                g_groupes[idx].moderateurName[0] = '\0';
                g_groupes[idx].pid = 0;

                /* 4. REPONSE CRUCIALE : "OK" pour que le client ferme sa fenêtre */
                snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "OK");
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE, "Groupe supprime");
            }
        } else if (strcmp(msgReq.Ordre, "FUS") == 0) {
            /* Demande de fusion : msgReq->Texte = nomG1 nomG2 (séparés par espace) */
            char g1[32], g2[32];
            if (sscanf(msgReq.Texte, "%31s %31s", g1, g2) != 2) {
                snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ERR");
                snprintf(msgRep.Texte, ISY_TAILLE_TEXTE,
                         "Format fusion invalide");
            } else {
                int idx1 = trouver_groupe_par_nom(g1);
                int idx2 = trouver_groupe_par_nom(g2);
                if (idx1 < 0 || idx2 < 0 || !g_groupes[idx1].actif || !g_groupes[idx2].actif) {
                    snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ERR");
                    snprintf(msgRep.Texte, ISY_TAILLE_TEXTE,
                             "Groupes introuvables pour fusion");
                } else {
                    /* Vérifie que l'émetteur est modérateur des deux groupes (fiche 2.0) */
                    if (strncmp(g_groupes[idx1].moderateurName, msgReq.Emetteur, ISY_TAILLE_NOM) != 0 ||
                        strncmp(g_groupes[idx2].moderateurName, msgReq.Emetteur, ISY_TAILLE_NOM) != 0) {
                        snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ERR");
                        snprintf(msgRep.Texte, ISY_TAILLE_TEXTE,
                                 "Fusion refusée : vous devez être modérateur des deux groupes");
                    } else {
                        /* Fusion de g2 dans g1 */

                        /* AVANT de tuer g2, envoyer un message REP aux membres de g2
                         * pour leur indiquer vers quel groupe se rediriger */
                        MessageISY repMsg;
                        memset(&repMsg, 0, sizeof(repMsg));
                        strncpy(repMsg.Ordre, "REP", ISY_TAILLE_ORDRE - 1);
                        strncpy(repMsg.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                        snprintf(repMsg.Texte, ISY_TAILLE_TEXTE,
                                 "Fusion : Rejoignez '%s' (port %d)", g1, g_groupes[idx1].port);

                        struct sockaddr_in addrG2;
                        init_sockaddr(&addrG2, ISY_IP_SERVEUR, g_groupes[idx2].port);
                        if (sendto(sock_serveur, &repMsg, sizeof(repMsg), 0,
                                  (struct sockaddr *)&addrG2, sizeof(addrG2)) < 0) {
                            perror("sendto REP fusion");
                        }

                        /* Petit délai pour s'assurer que le message REP est traité */
                        struct timespec delay = {0, 200000000}; /* 200ms */
                        nanosleep(&delay, NULL);

                        /* Maintenant on peut tuer le processus g2 */
                        if (g_groupes[idx2].pid > 0) {
                            if (kill(g_groupes[idx2].pid, SIGINT) < 0) {
                                perror("kill SIGINT (fusion)");
                            }
                            /* Note: SIGCHLD handler se charge de récolter le zombie */
                        }
                        g_groupes[idx2].actif = 0;
                        g_groupes[idx2].port = 0;
                        g_groupes[idx2].nom[0] = '\0';
                        g_groupes[idx2].moderateurName[0] = '\0';
                        g_groupes[idx2].pid = 0;

                        /* Notifie les membres du groupe restant qu'une fusion a eu lieu */
                        MessageISY notif;
                        memset(&notif, 0, sizeof(notif));
                        strncpy(notif.Ordre, "MSG", ISY_TAILLE_ORDRE - 1);
                        strncpy(notif.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
                        snprintf(notif.Texte, ISY_TAILLE_TEXTE, "Le groupe '%s' a fusionné avec ce groupe", g2);
                        /* Chiffrer le message SYSTEM avant envoi */
                        cesar_chiffrer(notif.Texte);
                        struct sockaddr_in addrG1;
                        init_sockaddr(&addrG1, ISY_IP_SERVEUR, g_groupes[idx1].port);
                        /* Envoi du message au processus GroupeISY pour diffusion */
                        if (sendto(sock_serveur, &notif, sizeof(notif), 0, (struct sockaddr *)&addrG1, sizeof(addrG1)) < 0) {
                            perror("sendto notification fusion");
                        }

                        snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ACK");
                        snprintf(msgRep.Texte, ISY_TAILLE_TEXTE,
                                 "Fusion effectuee : '%s' absorbe '%s'", g1, g2);
                    }
                }
            }
        } else {
            snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ERR");
            snprintf(msgRep.Texte, ISY_TAILLE_TEXTE,
                     "Ordre '%s' non supporte", msgReq.Ordre);
        }

        printf("--- Envoi reponse ---\n");
        afficher_message_debug("Serveur", &msgRep);

        if (sendto(sock_serveur, &msgRep, sizeof(msgRep), 0,
                   (struct sockaddr *)&addrCli, lenCli) < 0) {
            perror("sendto ServeurISY");
        }
    }

    printf("ServeurISY : arret\n");
    fermer_socket_udp(sock_serveur);
    return 0;
}