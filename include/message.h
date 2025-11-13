#ifndef MESSAGE_H
#define MESSAGE_H

#define BUFSIZE 100
#define MAX_GROUPE_NAME 50

// Structure des messages avec notion de groupe
typedef struct
{
    int Identifiant;              // ID du client
    char Texte[BUFSIZE];          // Contenu du message
    char Groupe[MAX_GROUPE_NAME]; // Nom du groupe (optionnel pour l'instant)
} struct_message;

// Fonctions de manipulation des messages
int GetNumClient(const char *message);
void GetMessageClient(const char *message, char *texte);
void GetGroupeMessage(const char *message, char *groupe);

void SetNumClient(int num, struct_message *msg);
void SetMessageClient(const char *texte, struct_message *msg);
void SetGroupeMessage(const char *groupe, struct_message *msg);

#endif