/**********************************************
 * FICHIER : AffichageISY.c
 * Rôle   : Affichage des messages d’un groupe
 *********************************************/

#include "commun.h"
#include <stdlib.h>

/* ... includes ... */

int main(int argc, char *argv[])
{
    /* 1. On vérifie qu'on a bien 2 arguments : port et nom */
    if (argc < 3)
    {
        fprintf(stderr, "Usage: AffichageISY <portGroupe> <nomUtilisateur>\n");
        exit(EXIT_FAILURE);
    }

    int portGroupe = atoi(argv[1]);
    char *monNom = argv[2]; /* On récupère le nom passé en paramètre */

    printf("AffichageISY : inscription sur le port %d pour %s\n", portGroupe, monNom);

    int sock = creer_socket_udp();
    if (sock < 0)
        exit(EXIT_FAILURE);

    struct sockaddr_in addrLocal;
    init_sockaddr(&addrLocal, ISY_IP_SERVEUR, 0);
    if (bind(sock, (struct sockaddr *)&addrLocal, sizeof(addrLocal)) < 0)
    {
        perror("bind AffichageISY");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addrGroupe;
    init_sockaddr(&addrGroupe, ISY_IP_SERVEUR, portGroupe);

    /* Envoi de l'inscription */
    MessageISY reg;
    memset(&reg, 0, sizeof(reg));
    snprintf(reg.Ordre, ISY_TAILLE_ORDRE, "REG");
    /* C'est plus propre d'utiliser le vrai nom ici aussi, même si facultatif */
    snprintf(reg.Emetteur, ISY_TAILLE_NOM, "%s", monNom);

    if (sendto(sock, &reg, sizeof(reg), 0, (struct sockaddr *)&addrGroupe, sizeof(addrGroupe)) < 0)
    {
        perror("sendto REG");
        exit(EXIT_FAILURE);
    }

    printf("AffichageISY : inscrit, attente des messages...\n");

    MessageISY msg;
    struct sockaddr_in addrExp;
    socklen_t lenExp = sizeof(addrExp);

    while (1)
    {
        ssize_t n = recvfrom(sock, &msg, sizeof(msg), 0, (struct sockaddr *)&addrExp, &lenExp);
        if (n < 0)
            continue;

        msg.Ordre[ISY_TAILLE_ORDRE - 1] = '\0';
        msg.Emetteur[ISY_TAILLE_NOM - 1] = '\0';
        msg.Texte[ISY_TAILLE_TEXTE - 1] = '\0';

        if (strcmp(msg.Ordre, "MSG") == 0)
        {
            /* Déchiffrement du texte reçu */
            cesar_dechiffrer(msg.Texte);

            if (strcmp(msg.Emetteur, monNom) == 0)
            {
                printf("[Moi] : %s\n", msg.Texte);
            }
            else
            {
                printf("[%s] : %s\n", msg.Emetteur, msg.Texte);
            }
            fflush(stdout);
        }
        else if (strcmp(msg.Ordre, "FIN") == 0)
        {
            printf("--- Le groupe a été fermé par le modérateur ---\n");
            printf("Fermeture automatique...\n");
            break; /* On sort de la boucle while(1), donc le programme s'arrête */
        }
        else if (strcmp(msg.Ordre, "BAN") == 0)
        {
            /* Notification de bannissement reçue : on informe l'utilisateur et on ferme */
            printf("--- %s ---\n", msg.Texte);
            printf("Vous avez été exclu du groupe. Fermeture automatique...\n");
            break;
        }
        else if (strcmp(msg.Ordre, "MIG") == 0)
        {
            /* Migration : le serveur/groupe nous informe d'un nouveau port */
            int newPort = atoi(msg.Texte);
            printf("--- Migration automatique vers le port %d ---\n", newPort);
            /* Mettre à jour le port et l'adresse du groupe */
            portGroupe = newPort;
            init_sockaddr(&addrGroupe, ISY_IP_SERVEUR, portGroupe);
            /* Envoyer un nouveau REG pour s'inscrire dans le nouveau groupe */
            MessageISY reg2;
            memset(&reg2, 0, sizeof(reg2));
            strncpy(reg2.Ordre, "REG", ISY_TAILLE_ORDRE - 1);
            strncpy(reg2.Emetteur, monNom, ISY_TAILLE_NOM - 1);
            if (sendto(sock, &reg2, sizeof(reg2), 0,
                       (struct sockaddr *)&addrGroupe, sizeof(addrGroupe)) < 0)
            {
                perror("sendto REG (migration)");
                break;
            }
            printf("Reinscription reussie, attente des messages...\n");
            continue;
        }
    }

    fermer_socket_udp(sock);
    return 0;
}
