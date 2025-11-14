/**********************************************
 * FICHIER : AffichageISY.c
 * Rôle   : Affichage des messages d’un groupe
 *********************************************/

#include "Commun.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: AffichageISY <portGroupe>\n");
        exit(EXIT_FAILURE);
    }

    int portGroupe = atoi(argv[1]);
    printf("AffichageISY : ecoute sur port %d\n", portGroupe);

    int sock = creer_socket_udp();
    if (sock < 0) {
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addrLocal;
    init_sockaddr(&addrLocal, ISY_IP_SERVEUR, portGroupe);
    if (bind(sock, (struct sockaddr *)&addrLocal, sizeof(addrLocal)) < 0) {
        perror("bind AffichageISY");
        fermer_socket_udp(sock);
        exit(EXIT_FAILURE);
    }

    MessageISY msg;
    struct sockaddr_in addrExp;
    socklen_t lenExp = sizeof(addrExp);

    while (1) {
        ssize_t n = recvfrom(sock, &msg, sizeof(msg), 0,
                             (struct sockaddr *)&addrExp, &lenExp);
        if (n < 0) {
            perror("recvfrom AffichageISY");
            continue;
        }

        msg.Ordre[ISY_TAILLE_ORDRE - 1]  = '\0';
        msg.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msg.Texte[ISY_TAILLE_TEXTE - 1]  = '\0';

        if (strcmp(msg.Ordre, "MSG") == 0) {
            printf("Message de %s : %s\n", msg.Emetteur, msg.Texte);
            fflush(stdout);
        }
    }

    fermer_socket_udp(sock);
    return 0;
}
