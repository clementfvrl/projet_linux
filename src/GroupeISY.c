/**********************************************
 * FICHIER : GroupeISY.c
 * Rôle   : Processus de groupe ISY
 *********************************************/

#include "commun.h"
#include <strings.h>
#include <signal.h>  
#include <unistd.h>  

static int sock_groupe = -1;
static int g_portGroupe = 0;

/* Nom du modérateur/gestionnaire du groupe */
static char g_moderateurName[ISY_TAILLE_NOM] = "";

/* On mémorise jusqu'à ISY_MAX_MEMBRES adresses clients et leur nom/bannissement */
typedef struct {
    int actif;
    struct sockaddr_in addr;
    char nom[ISY_TAILLE_NOM];
    int banni;        /* 1 si exclu/banni, 0 sinon */
} MembreGroupe;

static MembreGroupe g_membres[ISY_MAX_MEMBRES];

static void init_membres(void)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
        g_membres[i].actif = 0;
        g_membres[i].banni = 0;
        g_membres[i].nom[0] = '\0';
    }
}

static int trouver_slot_membre(void)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
        if (!g_membres[i].actif) return i;
    }
    return -1;
}

static int adresse_deja_connue(const struct sockaddr_in *addr)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
        if (g_membres[i].actif &&
            g_membres[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            g_membres[i].addr.sin_port == addr->sin_port) {
            return 1;
        }
    }
    return 0;
}

static void ajouter_membre(const struct sockaddr_in *addr, const char *nom)
{
    if (adresse_deja_connue(addr)) return;
    int idx = trouver_slot_membre();
    if (idx < 0) return;
    g_membres[idx].actif = 1;
    g_membres[idx].addr  = *addr;
    if (nom && nom[0] != '\0') {
        strncpy(g_membres[idx].nom, nom, ISY_TAILLE_NOM - 1);
        g_membres[idx].nom[ISY_TAILLE_NOM - 1] = '\0';
    }
    g_membres[idx].banni = 0;
}

static void redistribuer_message(const MessageISY *msg,
                                 const struct sockaddr_in *addrEmetteur)
{
    /* Envoi à tous les membres, y compris l'émetteur */
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
        if (g_membres[i].actif) {
            if (sendto(sock_groupe, msg, sizeof(*msg), 0,
                       (struct sockaddr *)&g_membres[i].addr,
                       sizeof(g_membres[i].addr)) < 0) {
                perror("sendto GroupeISY");
            }
        }
    }
    (void)addrEmetteur;
}

/* Fonction appelée quand le Serveur envoie SIGINT (Kill) */
static void arret_groupe(int sig)
{
    (void)sig; /* Evite le warning */

    MessageISY msgFin;
    memset(&msgFin, 0, sizeof(msgFin));
    /* On prépare le message "FIN" */
    strncpy(msgFin.Ordre, "FIN", ISY_TAILLE_ORDRE - 1);
    strncpy(msgFin.Emetteur, "SYSTEM", ISY_TAILLE_NOM - 1);
    strncpy(msgFin.Texte, "Groupe dissous par le moderateur", ISY_TAILLE_TEXTE - 1);

    printf("\nGroupeISY : Fermeture demandée. Envoi de FIN aux membres...\n");

    /* On parcourt tous les membres actifs pour leur dire au revoir */
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
        if (g_membres[i].actif) {
            sendto(sock_groupe, &msgFin, sizeof(msgFin), 0,
                   (struct sockaddr *)&g_membres[i].addr,
                   sizeof(g_membres[i].addr));
        }
    }

    /* Petite pause pour être sûr que les paquets réseau partent avant de couper */
    usleep(100000); // 0.1 seconde

    fermer_socket_udp(sock_groupe);
    printf("GroupeISY : Arret terminé.\n");
    exit(0);
}

/* ==== MAIN ==== */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: GroupeISY <portGroupe> <moderateurName>\n");
        exit(EXIT_FAILURE);
    }

    g_portGroupe = atoi(argv[1]);
    strncpy(g_moderateurName, argv[2], ISY_TAILLE_NOM - 1);
    g_moderateurName[ISY_TAILLE_NOM - 1] = '\0';
    printf("GroupeISY : lancement sur port %d (moderateur %s)\n", g_portGroupe, g_moderateurName);

    signal(SIGINT, arret_groupe); /* Gestion de l'arrêt du groupe */

    init_membres();

    sock_groupe = creer_socket_udp();
    if (sock_groupe < 0) {
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addrG;
    init_sockaddr(&addrG, ISY_IP_SERVEUR, g_portGroupe);
    if (bind(sock_groupe, (struct sockaddr *)&addrG, sizeof(addrG)) < 0) {
        perror("bind GroupeISY");
        fermer_socket_udp(sock_groupe);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addrCli;
    socklen_t lenCli = sizeof(addrCli);
    MessageISY msg;

    while (1) {
    ssize_t n = recvfrom(sock_groupe, &msg, sizeof(msg), 0,
                         (struct sockaddr *)&addrCli, &lenCli);
    if (n < 0) {
        perror("recvfrom GroupeISY");
        continue;
    }

    msg.Ordre[ISY_TAILLE_ORDRE - 1]  = '\0';
    msg.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
    msg.Texte[ISY_TAILLE_TEXTE - 1]  = '\0';

        if (strcmp(msg.Ordre, "REG") == 0) {
            /* Inscription d'un nouveau récepteur (AffichageISY) */
            /* Pour l'affichage, on utilise un nom générique */
            ajouter_membre(&addrCli, msg.Emetteur);
            printf("GroupeISY(port %d) : nouveau client d'affichage inscrit\n",
                   g_portGroupe);

        } else if (strcmp(msg.Ordre, "MSG") == 0) {
            /* Message normal d'un client -> on ajoute l'émetteur (si pas déjà là)
               puis on redistribue à tous les membres (clients + affichages) */
            ajouter_membre(&addrCli, msg.Emetteur);
            /* Ne pas redistribuer les messages des membres bannis */
            int banned = 0;
            for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
                if (g_membres[i].actif &&
                    g_membres[i].addr.sin_addr.s_addr == addrCli.sin_addr.s_addr &&
                    g_membres[i].addr.sin_port == addrCli.sin_port) {
                    banned = g_membres[i].banni;
                    break;
                }
            }
            if (!banned) {
                printf("GroupeISY(port %d) message recu : ", g_portGroupe);
                afficher_message_debug("Groupe", &msg);
                redistribuer_message(&msg, &addrCli);
            }

        } else if (strcmp(msg.Ordre, "CMD") == 0) {
            /* Commande administrative envoyée par le modérateur */
            /* Vérifier si c'est bien le modérateur */
            if (strncmp(msg.Emetteur, g_moderateurName, ISY_TAILLE_NOM) != 0) {
                /* Ignorer les commandes des non-modérateurs */
            } else {
                char cmd[ISY_TAILLE_TEXTE];
                strncpy(cmd, msg.Texte, sizeof(cmd) - 1);
                cmd[sizeof(cmd) - 1] = '\0';
                /* Réponse par défaut */
                MessageISY rep;
                memset(&rep, 0, sizeof(rep));
                strncpy(rep.Ordre, "RSP", ISY_TAILLE_ORDRE - 1);
                strncpy(rep.Emetteur, "Groupe", ISY_TAILLE_NOM - 1);
                rep.Texte[0] = '\0';
                if (strncasecmp(cmd, "list", 4) == 0) {
                    /* Lister les membres actifs (non bannis) */
                    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
                        if (g_membres[i].actif && !g_membres[i].banni) {
                            if (strlen(rep.Texte) + strlen(g_membres[i].nom) + 2 < ISY_TAILLE_TEXTE) {
                                strcat(rep.Texte, g_membres[i].nom);
                                strcat(rep.Texte, "\n");
                            }
                        }
                    }
                    if (rep.Texte[0] == '\0') {
                        strncpy(rep.Texte, "Aucun membre\n", ISY_TAILLE_TEXTE - 1);
                        rep.Texte[ISY_TAILLE_TEXTE - 1] = '\0';
                    }
                } else if (strncasecmp(cmd, "delete ", 7) == 0 || strncasecmp(cmd, "ban ", 4) == 0) {
                    /* Extraire le nom à bannir/exclure */
                    char *nameStart = strchr(cmd, ' ');
                    if (nameStart) {
                        nameStart++;
                        /* Chercher le membre correspondant */
                        int found = 0;
                        for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
                            if (g_membres[i].actif && !g_membres[i].banni &&
                                strncasecmp(g_membres[i].nom, nameStart, ISY_TAILLE_NOM) == 0) {
                                g_membres[i].banni = 1;
                                g_membres[i].actif = 0; /* retirer de la liste active */
                                snprintf(rep.Texte, ISY_TAILLE_TEXTE,
                                         "Membre %s exclu", g_membres[i].nom);
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            snprintf(rep.Texte, ISY_TAILLE_TEXTE,
                                     "Membre %s introuvable", nameStart);
                        }
                    } else {
                        strncpy(rep.Texte, "Nom manquant", ISY_TAILLE_TEXTE - 1);
                    }
                } else {
                    strncpy(rep.Texte, "Commande inconnue", ISY_TAILLE_TEXTE - 1);
                }
                /* Envoyer la réponse uniquement à l'émetteur de la commande */
                sendto(sock_groupe, &rep, sizeof(rep), 0,
                       (struct sockaddr *)&addrCli, sizeof(addrCli));
            }

        } else {
            /* Autres ordres éventuels plus tard */
        }   
    }


    fermer_socket_udp(sock_groupe);
    return 0;
}
