#include "Groupe.h"
#include <stdio.h>
#include <string.h>

// Variables globales pour stocker les groupes
Groupe liste_groupes[MAX_GROUPES];
int nb_groupes_actifs = 0;

/**
 * Trouve un groupe par son nom
 * Retourne un pointeur vers le groupe ou NULL si non trouvé
 */
Groupe *trouver_groupe(const char *nom)
{
    for (int i = 0; i < MAX_GROUPES; i++)
    {
        if (liste_groupes[i].actif && strcmp(liste_groupes[i].nom, nom) == 0)
        {
            return &liste_groupes[i];
        }
    }
    return NULL;
}

/**
 * Crée un nouveau groupe
 * Retourne 0 en cas de succès, -1 en cas d'erreur
 */
int creer_groupe(const char *nom, int admin_id)
{
    // Vérifier si le groupe existe déjà
    if (trouver_groupe(nom) != NULL)
    {
        fprintf(stderr, "Erreur : le groupe '%s' existe déjà\n", nom);
        return -1;
    }

    // Vérifier qu'on n'a pas atteint la limite
    if (nb_groupes_actifs >= MAX_GROUPES)
    {
        fprintf(stderr, "Erreur : nombre maximum de groupes atteint\n");
        return -1;
    }

    // Trouver un emplacement libre
    for (int i = 0; i < MAX_GROUPES; i++)
    {
        if (!liste_groupes[i].actif)
        {
            // Initialiser le groupe
            strncpy(liste_groupes[i].nom, nom, MAX_GROUPE_NAME - 1);
            liste_groupes[i].nom[MAX_GROUPE_NAME - 1] = '\0';
            liste_groupes[i].admin_id = admin_id;
            liste_groupes[i].nb_membres = 1;
            liste_groupes[i].membres[0] = admin_id;
            liste_groupes[i].actif = 1;

            nb_groupes_actifs++;
            printf("[SERVEUR] Groupe '%s' créé par client %d\n", nom, admin_id);
            return 0;
        }
    }

    return -1;
}

/**
 * Permet à un client de rejoindre un groupe
 * Retourne 0 en cas de succès, -1 en cas d'erreur
 */
int rejoindre_groupe(const char *nom_groupe, int client_id)
{
    Groupe *g = trouver_groupe(nom_groupe);

    if (g == NULL)
    {
        fprintf(stderr, "Erreur : groupe '%s' introuvable\n", nom_groupe);
        return -1;
    }

    // Vérifier si le client est déjà membre
    for (int i = 0; i < g->nb_membres; i++)
    {
        if (g->membres[i] == client_id)
        {
            printf("[SERVEUR] Client %d déjà dans le groupe '%s'\n",
                   client_id, nom_groupe);
            return 0;
        }
    }

    // Vérifier la limite de membres
    if (g->nb_membres >= MAX_MEMBRES)
    {
        fprintf(stderr, "Erreur : groupe '%s' complet\n", nom_groupe);
        return -1;
    }

    // Ajouter le membre
    g->membres[g->nb_membres] = client_id;
    g->nb_membres++;

    printf("[SERVEUR] Client %d a rejoint le groupe '%s'\n", client_id, nom_groupe);
    return 0;
}

/**
 * Permet à un client de quitter un groupe
 * Retourne 0 en cas de succès, -1 en cas d'erreur
 */
int quitter_groupe(const char *nom_groupe, int client_id)
{
    Groupe *g = trouver_groupe(nom_groupe);

    if (g == NULL)
    {
        fprintf(stderr, "Erreur : groupe '%s' introuvable\n", nom_groupe);
        return -1;
    }

    // Chercher et retirer le membre
    for (int i = 0; i < g->nb_membres; i++)
    {
        if (g->membres[i] == client_id)
        {
            // Décaler les membres suivants
            for (int j = i; j < g->nb_membres - 1; j++)
            {
                g->membres[j] = g->membres[j + 1];
            }
            g->nb_membres--;

            printf("[SERVEUR] Client %d a quitté le groupe '%s'\n",
                   client_id, nom_groupe);

            // Si le groupe est vide, le supprimer
            if (g->nb_membres == 0)
            {
                g->actif = 0;
                nb_groupes_actifs--;
                printf("[SERVEUR] Groupe '%s' supprimé (vide)\n", nom_groupe);
            }

            return 0;
        }
    }

    fprintf(stderr, "Erreur : client %d pas dans le groupe '%s'\n",
            client_id, nom_groupe);
    return -1;
}

/**
 * Supprime un groupe (admin uniquement)
 * Retourne 0 en cas de succès, -1 en cas d'erreur
 */
int supprimer_groupe(const char *nom_groupe, int client_id)
{
    Groupe *g = trouver_groupe(nom_groupe);

    if (g == NULL)
    {
        fprintf(stderr, "Erreur : groupe '%s' introuvable\n", nom_groupe);
        return -1;
    }

    // Vérifier que c'est l'admin
    if (g->admin_id != client_id)
    {
        fprintf(stderr, "Erreur : client %d n'est pas admin du groupe '%s'\n",
                client_id, nom_groupe);
        return -1;
    }

    // Supprimer le groupe
    g->actif = 0;
    nb_groupes_actifs--;
    printf("[SERVEUR] Groupe '%s' supprimé par admin %d\n", nom_groupe, client_id);

    return 0;
}

/**
 * Vérifie si un client est admin d'un groupe
 * Retourne 1 si admin, 0 sinon
 */
int est_admin(const char *nom_groupe, int client_id)
{
    Groupe *g = trouver_groupe(nom_groupe);
    if (g == NULL)
    {
        return 0;
    }
    return (g->admin_id == client_id);
}

/**
 * Liste tous les groupes actifs
 */
void lister_groupes(void)
{
    printf("\n=== GROUPES ACTIFS ===\n");
    if (nb_groupes_actifs == 0)
    {
        printf("Aucun groupe actif\n");
        return;
    }

    for (int i = 0; i < MAX_GROUPES; i++)
    {
        if (liste_groupes[i].actif)
        {
            printf("- %s (admin: %d, membres: %d)\n",
                   liste_groupes[i].nom,
                   liste_groupes[i].admin_id,
                   liste_groupes[i].nb_membres);
        }
    }
    printf("======================\n");
}

/**
 * Liste les membres d'un groupe
 */
void lister_membres_groupe(const char *nom_groupe)
{
    Groupe *g = trouver_groupe(nom_groupe);

    if (g == NULL)
    {
        fprintf(stderr, "Erreur : groupe '%s' introuvable\n", nom_groupe);
        return;
    }

    printf("\n=== MEMBRES du groupe '%s' ===\n", nom_groupe);
    for (int i = 0; i < g->nb_membres; i++)
    {
        printf("- Client %d%s\n",
               g->membres[i],
               (g->membres[i] == g->admin_id) ? " (admin)" : "");
    }
    printf("============================\n");
}