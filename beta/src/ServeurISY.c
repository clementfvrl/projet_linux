/**********************************************
 * FICHIER : ServeurISY.c
 * Rôle   : Serveur principal ISY
 *********************************************/

#include "commun.h"
#include <signal.h>

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
        /* Processus fils : execl GroupeISY */
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
    int first = 1;
    for (int i = 0; i < ISY_MAX_GROUPES; ++i) {
        if (g_groupes[i].actif) {
            snprintf(line, sizeof(line), "%s (port %d)\n",
                     g_groupes[i].nom, g_groupes[i].port);
            if (strlen(msgRep->Texte) + strlen(line) < ISY_TAILLE_TEXTE) {
                strcat(msgRep->Texte, line);
            } else {
                break; /* buffer plein, on coupe */
            }
            first = 0;
        }
    }
    if (first) {
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

/* ==== MAIN ==== */
int main(void)
{
    struct sockaddr_in addrServ, addrCli;
    socklen_t lenCli = sizeof(addrCli);

    signal(SIGINT, sigint_handler);

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
            perror("recvfrom ServeurISY");
            continue;
        }

        msgReq.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msgReq.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msgReq.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        printf("\n--- Recu du client ---\n");
        afficher_message_debug("Serveur", &msgReq);

        memset(&msgRep, 0, sizeof(msgRep));
        strncpy(msgRep.Emetteur, "Serveur", ISY_TAILLE_NOM - 1);

        /* --- AJOUT : Logique de Connexion (CON) --- */
        if (strcmp(msgReq.Ordre, "CON") == 0) {
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
                /* 1. Tuer le processus de groupe */
                if (g_groupes[idx].pid > 0) {
                    kill(g_groupes[idx].pid, SIGINT);
                    waitpid(g_groupes[idx].pid, NULL, 0); // Attendre la fin propre du fils pour éviter zombie
                }
                
                /* 2. Nettoyer la structure */
                g_groupes[idx].actif = 0;
                g_groupes[idx].port = 0;
                g_groupes[idx].nom[0] = '\0';
                g_groupes[idx].moderateurName[0] = '\0';
                g_groupes[idx].pid = 0;

                /* 3. REPONSE CRUCIALE : "OK" pour que le client ferme sa fenêtre */
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
                        if (g_groupes[idx2].pid > 0) {
                            kill(g_groupes[idx2].pid, SIGINT);
                            waitpid(g_groupes[idx2].pid, NULL, 0);
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
                        snprintf(notif.Texte, ISY_TAILLE_TEXTE, "Les groupes '%s' et '%s' ont été fusionnés", g1, g2);
                        /*
                         * Les messages système doivent être chiffrés avant diffusion car
                         * les clients appliquent un déchiffrement sur les ordres MSG.
                         */
                        cesar_chiffrer(notif.Texte);
                        struct sockaddr_in addrG1;
                        init_sockaddr(&addrG1, ISY_IP_SERVEUR, g_groupes[idx1].port);
                        /* Envoi du message au processus GroupeISY pour diffusion */
                        sendto(sock_serveur, &notif, sizeof(notif), 0, (struct sockaddr *)&addrG1, sizeof(addrG1));

                        snprintf(msgRep.Ordre, ISY_TAILLE_ORDRE, "ACK");
                        snprintf(msgRep.Texte, ISY_TAILLE_TEXTE,
                                 "Groupes '%s' et '%s' fusionnes (membres de %s doivent se reconnecter)", g1, g2, g1);
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