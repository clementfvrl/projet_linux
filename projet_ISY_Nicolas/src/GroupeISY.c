/**********************************************
 * FICHIER : GroupeISY.c
 * Rôle   : Processus de groupe ISY
 *********************************************/

#include "Commun.h"

static int sock_groupe = -1;
static int g_portGroupe = 0;

/* On mémorise jusqu'à ISY_MAX_MEMBRES adresses clients */
typedef struct {
    int actif;
    struct sockaddr_in addr;
} MembreGroupe;

static MembreGroupe g_membres[ISY_MAX_MEMBRES];

static void init_membres(void)
{
    for (int i = 0; i < ISY_MAX_MEMBRES; ++i) {
        g_membres[i].actif = 0;
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

static void ajouter_membre(const struct sockaddr_in *addr)
{
    if (adresse_deja_connue(addr)) return;
    int idx = trouver_slot_membre();
    if (idx < 0) return;
    g_membres[idx].actif = 1;
    g_membres[idx].addr  = *addr;
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

/* ==== MAIN ==== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: GroupeISY <portGroupe>\n");
        exit(EXIT_FAILURE);
    }

    g_portGroupe = atoi(argv[1]);
    printf("GroupeISY : lancement sur port %d\n", g_portGroupe);

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

        if (strcmp(msg.Ordre, "MSG") == 0) {
            ajouter_membre(&addrCli);
            printf("GroupeISY(port %d) message recu : ", g_portGroupe);
            afficher_message_debug("Groupe", &msg);
            redistribuer_message(&msg, &addrCli);
        } else {
            /* pour l'instant, on ignore les autres ordres */
        }
    }

    fermer_socket_udp(sock_groupe);
    return 0;
}
