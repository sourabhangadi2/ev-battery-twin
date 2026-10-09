/*
 * auth.c — Password hashing (werkzeug-compatible PBKDF2-SHA256) and session tokens.
 * Uses OpenSSL EVP_PBE_scrypt / PKCS5_PBKDF2_HMAC.
 */
#include "auth.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Hex encoding helpers ───────────────────────────────────────────── */

static const char HEX[] = "0123456789abcdef";

static void bytes_to_hex(const unsigned char *in, int len, char *out) {
    for (int i = 0; i < len; i++) {
        out[2*i]   = HEX[(in[i] >> 4) & 0xF];
        out[2*i+1] = HEX[in[i] & 0xF];
    }
    out[2*len] = '\0';
}

static int hex_to_byte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int hex_to_bytes(const char *hex, unsigned char *out, int max_len) {
    int len = (int)strlen(hex) / 2;
    if (len > max_len) len = max_len;
    for (int i = 0; i < len; i++) {
        out[i] = (unsigned char)((hex_to_byte(hex[2*i]) << 4) | hex_to_byte(hex[2*i+1]));
    }
    return len;
}

/* ── Password hashing ───────────────────────────────────────────────── */

void auth_hash_password(const char *password, char *out_buf, int out_size) {
    /* Generate 16-byte random salt → 32 hex chars */
    unsigned char salt_bytes[16];
    RAND_bytes(salt_bytes, sizeof(salt_bytes));
    char salt_hex[33];
    bytes_to_hex(salt_bytes, 16, salt_hex);

    /* PBKDF2-SHA256, 600000 iterations, 32-byte output key */
    const int ITERATIONS = 600000;
    const int KEY_LEN = 32;
    unsigned char key[32];
    PKCS5_PBKDF2_HMAC(
        password, (int)strlen(password),
        salt_bytes, 16,
        ITERATIONS,
        EVP_sha256(),
        KEY_LEN, key
    );

    char key_hex[65];
    bytes_to_hex(key, KEY_LEN, key_hex);

    /* Format: "pbkdf2:sha256:600000$<salt>$<hash>" */
    snprintf(out_buf, out_size, "pbkdf2:sha256:%d$%s$%s", ITERATIONS, salt_hex, key_hex);
}

/*
 * Verify a password against a werkzeug-format hash.
 * Format: "pbkdf2:sha256:<iterations>$<salt_hex>$<hash_hex>"
 */
int auth_verify_password(const char *password, const char *stored_hash) {
    /* Parse prefix */
    if (strncmp(stored_hash, "pbkdf2:sha256:", 14) != 0) return 0;

    const char *rest = stored_hash + 14;

    /* Parse iterations */
    char *dollar1 = strchr(rest, '$');
    if (!dollar1) return 0;
    int iterations = atoi(rest);

    /* Parse salt */
    const char *salt_part = dollar1 + 1;
    char *dollar2 = strchr(salt_part, '$');
    if (!dollar2) return 0;

    int salt_hex_len = (int)(dollar2 - salt_part);
    char salt_hex[65] = {0};
    if (salt_hex_len > 64) return 0;
    strncpy(salt_hex, salt_part, salt_hex_len);

    unsigned char salt_bytes[32];
    int salt_len = hex_to_bytes(salt_hex, salt_bytes, sizeof(salt_bytes));

    /* Parse stored hash */
    const char *hash_hex = dollar2 + 1;
    int hash_hex_len = (int)strlen(hash_hex);
    if (hash_hex_len > 128) return 0;

    unsigned char stored_key[64];
    int key_len = hex_to_bytes(hash_hex, stored_key, sizeof(stored_key));

    /* Recompute PBKDF2 */
    unsigned char computed_key[64];
    if (!PKCS5_PBKDF2_HMAC(
            password, (int)strlen(password),
            salt_bytes, salt_len,
            iterations,
            EVP_sha256(),
            key_len, computed_key)) {
        return 0;
    }

    /* Constant-time compare */
    int diff = 0;
    for (int i = 0; i < key_len; i++) diff |= (stored_key[i] ^ computed_key[i]);
    return (diff == 0);
}

/* ── Token generation ───────────────────────────────────────────────── */

void auth_generate_token(char *out_buf, int out_size) {
    unsigned char rnd[32];
    RAND_bytes(rnd, sizeof(rnd));
    char hex[65];
    bytes_to_hex(rnd, 32, hex);
    strncpy(out_buf, hex, out_size - 1);
    out_buf[out_size - 1] = '\0';
}
