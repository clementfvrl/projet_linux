#include "Message.h"
#include <string.h>
#include <strings.h>

// Obtention du numéro de client
int GetNumClient(const char *message)
{
    struct_message message_recu;
    bzero(&message_recu, sizeof(struct_message));
    memcpy(&message_recu, message, sizeof(struct_message));
    return message_recu.Identifiant;
}

// Récupération du texte du message
void GetMessageClient(const char *message, char *texte)
{
    struct_message message_recu;
    bzero(&message_recu, sizeof(struct_message));
    memcpy(&message_recu, message, sizeof(struct_message));
    strcpy(texte, message_recu.Texte);
}

// Récupération du groupe
void GetGroupeMessage(const char *message, char *groupe)
{
    struct_message message_recu;
    bzero(&message_recu, sizeof(struct_message));
    memcpy(&message_recu, message, sizeof(struct_message));
    strcpy(groupe, message_recu.Groupe);
}

// Définition du numéro de client
void SetNumClient(int num, struct_message *strMessage)
{
    strMessage->Identifiant = num;
}

// Définition du texte du message
void SetMessageClient(const char *message, struct_message *strMessage)
{
    bzero(strMessage->Texte, BUFSIZE);
    strncpy(strMessage->Texte, message, BUFSIZE - 1);
    strMessage->Texte[BUFSIZE - 1] = '\0';
}

// Définition du groupe
void SetGroupeMessage(const char *groupe, struct_message *strMessage)
{
    bzero(strMessage->Groupe, MAX_GROUPE_NAME);
    strncpy(strMessage->Groupe, groupe, MAX_GROUPE_NAME - 1);
    strMessage->Groupe[MAX_GROUPE_NAME - 1] = '\0';
}