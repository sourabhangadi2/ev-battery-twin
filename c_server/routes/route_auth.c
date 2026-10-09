/*
 * routes/route_auth.c — Login, logout, register, and dashboard routes.
 * Replaces Flask-Login + WTForms with C form parsing + session cookies.
 */
#include "route_auth.h"
#include "../database.h"
#include "../auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ── Helper: read an HTML file from www/ and substitute placeholders ── */

static char *read_html_file(const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "c_server/www/%s", filename);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof(path), "www/%s", filename);
        f = fopen(path, "rb");
    }
    if (!f) {
        fprintf(stderr, "Cannot open HTML: %s\n", filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f); rewind(f);
    char *buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* Replace first occurrence of needle with replacement in heap buffer.
 * Returns new heap-allocated string. Caller must free. */
static char *str_replace(const char *src, const char *needle, const char *rep) {
    const char *p = strstr(src, needle);
    if (!p) return strdup(src);
    size_t pre  = (size_t)(p - src);
    size_t rlen = strlen(rep);
    size_t nlen = strlen(needle);
    size_t total = strlen(src) - nlen + rlen + 1;
    char *out = (char*)malloc(total);
    if (!out) return strdup(src);
    memcpy(out, src, pre);
    memcpy(out + pre, rep, rlen);
    strcpy(out + pre + rlen, p + nlen);
    return out;
}

/* Replace ALL occurrences */
static char *str_replace_all(const char *src, const char *needle, const char *rep) {
    char *cur = strdup(src);
    const char *p;
    while (cur && (p = strstr(cur, needle))) {
        char *next = str_replace(cur, needle, rep);
        free(cur);
        cur = next;
    }
    return cur ? cur : strdup(src);
}

/* ── Set-Cookie header builder ───────────────────────────────────────── */
static void set_session_cookie(HttpResponse *resp, const char *token, int remember) {
    char cookie[512];
    if (remember) {
        snprintf(cookie, sizeof(cookie),
            "Set-Cookie: session=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d\r\n",
            token, SESSION_LIFETIME_SECS);
    } else {
        snprintf(cookie, sizeof(cookie),
            "Set-Cookie: session=%s; Path=/; HttpOnly; SameSite=Lax\r\n", token);
    }
    strncat(resp->extra_headers, cookie,
            sizeof(resp->extra_headers) - strlen(resp->extra_headers) - 1);
}

static void clear_session_cookie(HttpResponse *resp) {
    const char *s = "Set-Cookie: session=; Path=/; HttpOnly; Max-Age=0\r\n";
    strncat(resp->extra_headers, s,
            sizeof(resp->extra_headers) - strlen(resp->extra_headers) - 1);
}

/* ── Inject flash message into HTML placeholder ─────────────────────── */
static char *inject_flash(char *html, const char *css_class, const char *msg) {
    /* Replace <!--FLASH--> placeholder in HTML */
    char flash_html[1024];
    snprintf(flash_html, sizeof(flash_html),
        "<div class=\"p-3 mb-4 rounded text-xs text-center border %s\" role=\"alert\">%s</div>",
        css_class, msg);
    char *result = str_replace(html, "<!--FLASH-->", flash_html);
    free(html);
    return result;
}

/* ── GET/POST /login ─────────────────────────────────────────────────── */

void route_login(HttpRequest *req, HttpResponse *resp) {
    /* If already logged in, redirect to dashboard */
    char existing_token[256] = {0};
    if (http_get_cookie(req, "session", existing_token, sizeof(existing_token))) {
        Session sess;
        if (db_session_find(existing_token, &sess) && sess.expires_at > time(NULL)) {
            http_respond_redirect(resp, "/dashboard");
            return;
        }
    }

    if (strcmp(req->method, "GET") == 0) {
        char *html = read_html_file("login.html");
        if (!html) { http_respond_text(resp, 500, "text/plain", "Cannot load login page"); return; }
        char *out = str_replace(html, "<!--FLASH-->", "");
        free(html);
        resp->status_code = 200;
        strncpy(resp->content_type, "text/html", sizeof(resp->content_type)-1);
        resp->body     = out;
        resp->body_len = (int)strlen(out);
        return;
    }

    if (strcmp(req->method, "POST") == 0) {
        char *keys[16], *vals[16];
        char buf[4096];
        int n = form_parse(req->body, req->body_len, keys, vals, 16, buf, sizeof(buf));

        const char *email    = form_get(keys, vals, n, "email");
        const char *password = form_get(keys, vals, n, "password");
        const char *remember = form_get(keys, vals, n, "remember");

        if (!email || !password) {
            char *html = read_html_file("login.html");
            html = inject_flash(html, "bg-red-500/10 text-red-400 border-red-500/20", "Email and password are required.");
            resp->status_code = 400;
            strncpy(resp->content_type, "text/html", sizeof(resp->content_type)-1);
            resp->body = html; resp->body_len = (int)strlen(html);
            return;
        }

        User user;
        int found = db_user_find_by_email(email, &user);
        if (found && auth_verify_password(password, user.password_hash)) {
            /* Login success — create session */
            char token[65];
            auth_generate_token(token, sizeof(token));
            time_t expires = time(NULL) + SESSION_LIFETIME_SECS;
            db_session_create(user.id, token, expires);

            char log_desc[256];
            snprintf(log_desc, sizeof(log_desc),
                "Operator '%s' (%s) successfully logged in.", user.name, user.email);
            db_log_insert("AUTH_SUCCESS", log_desc);

            http_respond_redirect(resp, "/dashboard");
            set_session_cookie(resp, token, remember && strcmp(remember,"on")==0 ? 1 : 0);
        } else {
            char log_desc[256];
            snprintf(log_desc, sizeof(log_desc),
                "Failed login attempt for email: '%s'.", email ? email : "");
            db_log_insert("AUTH_FAILED", log_desc);

            char *html = read_html_file("login.html");
            html = inject_flash(html, "bg-red-500/10 text-red-400 border-red-500/20",
                "Invalid email or password. Please try again.");
            resp->status_code = 401;
            strncpy(resp->content_type, "text/html", sizeof(resp->content_type)-1);
            resp->body = html; resp->body_len = (int)strlen(html);
        }
        return;
    }

    http_respond_text(resp, 405, "text/plain", "Method Not Allowed");
}

/* ── GET /logout ─────────────────────────────────────────────────────── */

void route_logout(HttpRequest *req, HttpResponse *resp) {
    char token[256] = {0};
    if (http_get_cookie(req, "session", token, sizeof(token))) {
        Session sess; User user;
        if (db_session_find(token, &sess) && db_user_find_by_id(sess.user_id, &user)) {
            char log_desc[256];
            snprintf(log_desc, sizeof(log_desc), "Operator '%s' logged out.", user.name);
            db_log_insert("AUTH_LOGOUT", log_desc);
        }
        db_session_delete(token);
    }
    http_respond_redirect(resp, "/login");
    clear_session_cookie(resp);
}

/* ── GET/POST /register ──────────────────────────────────────────────── */

void route_register(HttpRequest *req, HttpResponse *resp) {
    char existing_token[256] = {0};
    if (http_get_cookie(req, "session", existing_token, sizeof(existing_token))) {
        Session sess;
        if (db_session_find(existing_token, &sess) && sess.expires_at > time(NULL)) {
            http_respond_redirect(resp, "/dashboard");
            return;
        }
    }

    if (strcmp(req->method, "GET") == 0) {
        char *html = read_html_file("register.html");
        if (!html) { http_respond_text(resp, 500, "text/plain", "Cannot load register page"); return; }
        char *out = str_replace(html, "<!--FLASH-->", "");
        free(html);
        resp->status_code = 200;
        strncpy(resp->content_type, "text/html", sizeof(resp->content_type)-1);
        resp->body = out; resp->body_len = (int)strlen(out);
        return;
    }

    if (strcmp(req->method, "POST") == 0) {
        char *keys[16], *vals[16];
        char buf[4096];
        int n = form_parse(req->body, req->body_len, keys, vals, 16, buf, sizeof(buf));

        const char *name     = form_get(keys, vals, n, "name");
        const char *email    = form_get(keys, vals, n, "email");
        const char *password = form_get(keys, vals, n, "password");
        const char *confirm  = form_get(keys, vals, n, "confirm_password");

        /* Validation */
        char *html = NULL;
        const char *err = NULL;
        if (!name || strlen(name) < 2)            err = "Full name must be at least 2 characters.";
        else if (!email || !strchr(email,'@'))     err = "Please enter a valid email address.";
        else if (!password || strlen(password) < 8) err = "Password must be at least 8 characters.";
        else if (!confirm || strcmp(password, confirm) != 0) err = "Passwords do not match.";

        if (err) {
            html = read_html_file("register.html");
            html = inject_flash(html, "bg-red-500/10 text-red-400 border-red-500/20", err);
            resp->status_code = 400;
            strncpy(resp->content_type, "text/html", sizeof(resp->content_type)-1);
            resp->body = html; resp->body_len = (int)strlen(html);
            return;
        }

        /* Check duplicate email */
        User existing_user;
        if (db_user_find_by_email(email, &existing_user)) {
            html = read_html_file("register.html");
            html = inject_flash(html, "bg-red-500/10 text-red-400 border-red-500/20",
                "This email address is already registered. Please sign in.");
            resp->status_code = 409;
            strncpy(resp->content_type, "text/html", sizeof(resp->content_type)-1);
            resp->body = html; resp->body_len = (int)strlen(html);
            return;
        }

        /* Create user */
        User new_user; memset(&new_user, 0, sizeof(new_user));
        strncpy(new_user.name,  name,    sizeof(new_user.name)-1);
        strncpy(new_user.email, email,   sizeof(new_user.email)-1);
        strncpy(new_user.role,  "operator", sizeof(new_user.role)-1);
        new_user.created_at = time(NULL);
        auth_hash_password(password, new_user.password_hash, sizeof(new_user.password_hash));
        db_user_insert(&new_user);

        char log_desc[256];
        snprintf(log_desc, sizeof(log_desc),
            "New operator account registered: '%s' (%s).", name, email);
        db_log_insert("AUTH_REGISTER", log_desc);

        /* Redirect to login with success message — encode in URL */
        http_respond_redirect(resp, "/login?registered=1");
        return;
    }

    http_respond_text(resp, 405, "text/plain", "Method Not Allowed");
}

/* ── GET /dashboard ──────────────────────────────────────────────────── */

void route_dashboard(HttpRequest *req, HttpResponse *resp, int user_id) {
    (void)req;
    User user; memset(&user, 0, sizeof(user));
    db_user_find_by_id(user_id, &user);

    char *html = read_html_file("dashboard.html");
    if (!html) { http_respond_text(resp, 500, "text/plain", "Cannot load dashboard"); return; }

    /* Replace {{name}} placeholder */
    char *out = str_replace_all(html, "{{name}}", user.name);
    free(html);

    resp->status_code = 200;
    strncpy(resp->content_type, "text/html", sizeof(resp->content_type)-1);
    resp->body     = out;
    resp->body_len = (int)strlen(out);
}
