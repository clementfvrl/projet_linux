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
    printf("AffichageISY : inscription sur le groupe (port %d)\n", portGroupe);

    int sock = creer_socket_udp();
    if (sock < 0) {
        exit(EXIT_FAILURE);
    }

    /* 1) Bind local sur port 0 pour laisser le noyau choisir un port libre */
    struct sockaddr_in addrLocal;
    init_sockaddr(&addrLocal, ISY_IP_SERVEUR, 0);  // port 0 = auto
    if (bind(sock, (struct sockaddr *)&addrLocal, sizeof(addrLocal)) < 0) {
        perror("bind AffichageISY");
        fermer_socket_udp(sock);
        exit(EXIT_FAILURE);
    }

    /* 2) Préparer l’adresse du GroupeISY (port fixe du groupe) */
    struct sockaddr_in addrGroupe;
    init_sockaddr(&addrGroupe, ISY_IP_SERVEUR, portGroupe);

    /* 3) Envoyer un message de REGISTRATION au groupe pour s'inscrire */
    MessageISY reg;
    memset(&reg, 0, sizeof(reg));
    snprintf(reg.Ordre, ISY_TAILLE_ORDRE, "REG");
    snprintf(reg.Emetteur, ISY_TAILLE_NOM, "Affichage");  // ou autre

    if (sendto(sock, &reg, sizeof(reg), 0,
               (struct sockaddr *)&addrGroupe, sizeof(addrGroupe)) < 0) {
        perror("sendto REG vers GroupeISY");
        fermer_socket_udp(sock);
        exit(EXIT_FAILURE);
    }

    printf("AffichageISY : inscrit, attente des messages...\n");

    /* 4) Boucle de réception des messages MSG */
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
