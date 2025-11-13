#ifndef GROUPE_H
#define GROUPE_H

#define MAX_GROUPE_NAME 50
#define MAX_MEMBRES 50
#define MAX_GROUPES 20

typedef struct
{
    char nom[MAX_GROUPE_NAME];
    int membres[MAX_MEMBRES];  // IDs des clients connectés
    int nb_membres;
    int admin_id;              // ID du créateur (admin)
    int actif;                 // 1 si le groupe existe, 0 sinon
} Groupe;

// Gestion de la liste des groupes
extern Groupe liste_groupes[MAX_GROUPES];
extern int nb_groupes_actifs;

// Fonctions de gestion des groupes
int creer_groupe(const char *nom, int admin_id);
int rejoindre_groupe(const char *nom_groupe, int client_id);
int quitter_groupe(const char *nom_groupe, int client_id);
int supprimer_groupe(const char *nom_groupe, int client_id);
Groupe* trouver_groupe(const char *nom);
void lister_groupes(void);
void lister_membres_groupe(const char *nom_groupe);
int est_admin(const char *nom_groupe, int client_id);

#endif