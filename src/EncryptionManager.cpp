#include "EncryptionManager.h"
#include <string.h>

EncryptionManager::EncryptionManager() {
    en = EVP_CIPHER_CTX_new();
    de = EVP_CIPHER_CTX_new();
};

void EncryptionManager::Init(std::string key_data, unsigned char* salt) {
    int i, nrounds = 5;

    /*
     * Generate key & IV for AES 128 CBC mode. SHA1 digest is used to hash the supplied key material.
     */
    i = EVP_BytesToKey(EVP_aes_128_cbc(), EVP_sha1(), salt, (unsigned char*)key_data.c_str(), key_data.length(), nrounds, key, iv);

    EVP_CIPHER_CTX_init(en);
    EVP_EncryptInit_ex(en, EVP_aes_128_cbc(), NULL, key, iv);
    EVP_CIPHER_CTX_init(de);
    EVP_DecryptInit_ex(de, EVP_aes_128_cbc(), NULL, key, iv);

}

EncryptionManager::~EncryptionManager()
{
    EVP_CIPHER_CTX_free(en);
    EVP_CIPHER_CTX_free(de);
}

std::string EncryptionManager::aes_encrypt(std::string plaintext)
{
    /* max ciphertext len for a n bytes of plaintext is n + AES_BLOCK_SIZE -1 bytes */
    int c_len = plaintext.length() + AES_BLOCK_SIZE, f_len = 0;
    unsigned char* ciphertext = new unsigned char[c_len];
    
    /* allows reusing of 'en' for multiple encryption cycles */
    EVP_EncryptInit_ex(en, NULL, NULL, NULL, NULL);

    /* update ciphertext, c_len is filled with the length of ciphertext generated, */
        EVP_EncryptUpdate(en, ciphertext, &c_len, (unsigned char*)plaintext.c_str(), plaintext.length());

    /* update ciphertext with the final remaining bytes */
    EVP_EncryptFinal_ex(en, ciphertext + c_len, &f_len);

    std::string output((char*)ciphertext, c_len + f_len);
    return output;
}

std::string EncryptionManager::aes_decrypt(std::string ciphertext)
{
    /* plaintext will always be equal to or lesser than length of ciphertext*/
    int p_len = ciphertext.length(), f_len = 0;
    std::string plaintext("", p_len);

    EVP_DecryptInit_ex(en, NULL, NULL, NULL, NULL);
    EVP_DecryptUpdate(en,(unsigned char*) &plaintext, &p_len, (unsigned char*)ciphertext.c_str(), ciphertext.length());
    EVP_DecryptFinal_ex(en, (unsigned char*) &plaintext[p_len], &f_len);

    return plaintext;
}

