#ifndef CRYPTO_H
#define CRYPTO_H

#include "commun.h"

/* Simple XOR encryption/decryption functions */
void crypter_message(char *texte, int longueur, const char *cle);
void decrypter_message(char *texte, int longueur, const char *cle);

/* Helper functions */
void generer_cle(char *cle, int longueur);

#endif /* CRYPTO_H */
