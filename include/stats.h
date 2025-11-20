#ifndef STATS_H
#define STATS_H

#include "commun.h"

/* Statistics structure for a member */
typedef struct {
    char nom[20];
    int nb_messages_envoyes;
    time_t date_derniere_activite;
    time_t date_connexion;
} stats_membre;

/* Statistics structure for a group */
typedef struct {
    char nom_groupe[50];
    int nb_messages_totaux;
    int nb_membres_total;
    int nb_membres_actifs;
    time_t date_creation;
    stats_membre membres[MAX_MEMBRES_PAR_GROUPE];
} stats_groupe;

/* Statistics functions */
void initialiser_stats_groupe(stats_groupe *stats, const char *nom);
void ajouter_membre_stats(stats_groupe *stats, const char *nom);
void incrementer_message_membre(stats_groupe *stats, const char *nom);
void afficher_stats_groupe(stats_groupe *stats);
void afficher_stats_membre(stats_groupe *stats, const char *nom);

#endif /* STATS_H */
