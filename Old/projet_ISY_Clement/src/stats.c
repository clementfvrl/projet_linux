#include "../include/stats.h"

void initialiser_stats_groupe(stats_groupe *stats, const char *nom) {
    memset(stats, 0, sizeof(stats_groupe));
    strncpy(stats->nom_groupe, nom, 49);
    stats->nom_groupe[49] = '\0';
    stats->date_creation = time(NULL);
}

void ajouter_membre_stats(stats_groupe *stats, const char *nom) {
    if (stats->nb_membres_total >= MAX_MEMBRES_PAR_GROUPE) {
        return;
    }
    for (int i = 0; i < stats->nb_membres_total; i++) {
        if (strcmp(stats->membres[i].nom, nom) == 0) {
            return;
        }
    }
    strncpy(stats->membres[stats->nb_membres_total].nom, nom, 19);
    stats->membres[stats->nb_membres_total].nom[19] = '\0';
    stats->membres[stats->nb_membres_total].nb_messages_envoyes = 0;
    stats->membres[stats->nb_membres_total].date_connexion = time(NULL);
    stats->membres[stats->nb_membres_total].date_derniere_activite = time(NULL);
    stats->nb_membres_total++;
    stats->nb_membres_actifs++;
}

void incrementer_message_membre(stats_groupe *stats, const char *nom) {
    for (int i = 0; i < stats->nb_membres_total; i++) {
        if (strcmp(stats->membres[i].nom, nom) == 0) {
            stats->membres[i].nb_messages_envoyes++;
            stats->membres[i].date_derniere_activite = time(NULL);
            stats->nb_messages_totaux++;
            return;
        }
    }
}

void afficher_stats_groupe(stats_groupe *stats) {
    printf("\n=== Statistiques du groupe %s ===\n", stats->nom_groupe);
    printf("Date de creation: %s", ctime(&stats->date_creation));
    printf("Nombre total de messages: %d\n", stats->nb_messages_totaux);
    printf("Nombre de membres actifs: %d\n", stats->nb_membres_actifs);
    printf("Nombre total de membres: %d\n\n", stats->nb_membres_total);
    printf("Membres:\n");
    for (int i = 0; i < stats->nb_membres_total; i++) {
        printf("  - %s: %d messages\n",
               stats->membres[i].nom,
               stats->membres[i].nb_messages_envoyes);
    }
    printf("\n");
}

void afficher_stats_membre(stats_groupe *stats, const char *nom) {
    for (int i = 0; i < stats->nb_membres_total; i++) {
        if (strcmp(stats->membres[i].nom, nom) == 0) {
            printf("\n=== Statistiques pour %s ===\n", nom);
            printf("Messages envoyes: %d\n", stats->membres[i].nb_messages_envoyes);
            printf("Date de connexion: %s", ctime(&stats->membres[i].date_connexion));
            printf("Derniere activite: %s\n", ctime(&stats->membres[i].date_derniere_activite));
            return;
        }
    }
    printf("Membre %s non trouve\n", nom);
}
