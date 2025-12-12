#include "../include/crypto.h"

/* Simple XOR encryption */
void crypter_message(char *texte, int longueur, const char *cle) {
    int cle_len = strlen(cle);
    if (cle_len == 0) return;

    for (int i = 0; i < longueur && texte[i] != '\0'; i++) {
        texte[i] ^= cle[i % cle_len];
    }
}

/* XOR decryption (same as encryption for XOR) */
void decrypter_message(char *texte, int longueur, const char *cle) {
    crypter_message(texte, longueur, cle);
}

/* Generate a simple encryption key */
void generer_cle(char *cle, int longueur) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    srand(time(NULL));

    for (int i = 0; i < longueur - 1; i++) {
        cle[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    cle[longueur - 1] = '\0';
}
