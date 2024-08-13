#pragma once
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string>

class EncryptionManager {
public:
	EncryptionManager();
	~EncryptionManager();

	// Apply aes-128 encryption based on key and iv values
	// All data going in & out is considered binary
	std::string aes_encrypt(std::string plaintext);

	// Decrypt aes-128 encryption based on key and iv values
	std::string aes_decrypt(std::string plaintext);
	void Init(std::string kd, unsigned char* salt);

private:
	/* ctx structures that libcrypto used to record encryption/decryption status */
	EVP_CIPHER_CTX* en;
	EVP_CIPHER_CTX* de;

	// store key and iv values for passing to clients
	unsigned char key[32], iv[32];
};
