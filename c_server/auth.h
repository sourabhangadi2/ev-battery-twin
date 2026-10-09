/*
 * auth.h — Session management and password hashing.
 * Uses OpenSSL for PBKDF2-SHA256 compatible with werkzeug's format.
 */
#ifndef AUTH_H
#define AUTH_H

#include "models.h"

/*
 * Hash a plain-text password using werkzeug's pbkdf2:sha256 scheme.
 * Output format: "pbkdf2:sha256:600000$<salt>$<hash>"
 * out_buf must be at least 256 bytes.
 */
void auth_hash_password(const char *password, char *out_buf, int out_size);

/*
 * Verify password against a werkzeug-style hash string.
 * Returns 1 if match, 0 otherwise.
 */
int auth_verify_password(const char *password, const char *stored_hash);

/*
 * Generate a cryptographically random session token (64 hex chars + NUL).
 * out_buf must be at least 65 bytes.
 */
void auth_generate_token(char *out_buf, int out_size);

/*
 * Session token lifetime in seconds (7 days).
 */
#define SESSION_LIFETIME_SECS (7 * 24 * 3600)

#endif /* AUTH_H */
