// 展示代码结构：
//   · 会话密钥 loadOrCreateAuthSecret（文件或 AUTH_SECRET）
//   · MySQL 表 ensureHostUsersTableMySQL、默认账号 ensureDefaultWebUser（SQLite 见 sqlite_users.go）
//   · authRuntime：Cookie/JWT、requireAuth / requireAuthWS、登录登出改密等 HTTP 处理器
//
package main

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"golang.org/x/crypto/bcrypt"
)

//--------//
// 模块：常量与类型 — 会话 Cookie 名、时长、JWT Claims、authRuntime
const (
	sessionCookieName = "HostSession"
	sessionDuration   = 7 * 24 * time.Hour
	minNewPasswordLen = 8
)

type ctxKey int

const ctxKeyUID ctxKey = iota

type sessionClaims struct {
	UID uint64 `json:"uid"`
	jwt.RegisteredClaims
}

type authRuntime struct {
	db     *sql.DB
	secret []byte
}

func loadOrCreateAuthSecret(path string) ([]byte, error) {
	if env := strings.TrimSpace(os.Getenv("AUTH_SECRET")); env != "" {
		sum := sha256.Sum256([]byte(env))
		return sum[:], nil
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		if !os.IsNotExist(err) {
			return nil, err
		}
		var b [32]byte
		if _, err := rand.Read(b[:]); err != nil {
			return nil, err
		}
		line := hex.EncodeToString(b[:]) + "\n"
		if err := os.WriteFile(path, []byte(line), 0o600); err != nil {
			return nil, err
		}
		log.Printf("auth: wrote new %s (keep this file secret; or set AUTH_SECRET)", path)
		return b[:], nil
	}
	s := strings.TrimSpace(string(raw))
	if dec, err := hex.DecodeString(s); err == nil && len(dec) == 32 {
		return dec, nil
	}
	sum := sha256.Sum256([]byte(s))
	return sum[:], nil
}

//--------//
// 模块：数据库 schema — host_users（MySQL）
func ensureHostUsersTableMySQL(ctx context.Context, db *sql.DB) error {
	_, err := db.ExecContext(ctx, `CREATE TABLE IF NOT EXISTS host_users (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(64) NOT NULL,
  password_hash VARBINARY(255) NOT NULL,
  must_change_password TINYINT(1) NOT NULL DEFAULT 0,
  created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  UNIQUE KEY uq_host_users_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci`)
	return err
}

//--------//
// 模块：首次部署 — 无用户时插入默认 web 账号
func ensureDefaultWebUser(ctx context.Context, db *sql.DB) error {
	var n int
	if err := db.QueryRowContext(ctx, `SELECT COUNT(*) FROM host_users`).Scan(&n); err != nil {
		return err
	}
	if n > 0 {
		return nil
	}
	hash, err := bcrypt.GenerateFromPassword([]byte("123456"), bcrypt.DefaultCost)
	if err != nil {
		return err
	}
	_, err = db.ExecContext(ctx,
		`INSERT INTO host_users (username, password_hash, must_change_password) VALUES (?, ?, 1)`,
		"user", hash)
	if err != nil {
		return err
	}
	log.Println("auth: created default web user \"user\" / 123456 (must change password on first login)")
	return nil
}

//--------//
// 模块：会话 Cookie — Secure 判定、写入/清除
func (ar *authRuntime) cookieSecure(r *http.Request) bool {
	if r.TLS != nil {
		return true
	}
	return strings.EqualFold(r.Header.Get("X-Forwarded-Proto"), "https")
}

func (ar *authRuntime) setSessionCookie(w http.ResponseWriter, r *http.Request, token string) {
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookieName,
		Value:    token,
		Path:     "/",
		MaxAge:   int(sessionDuration.Seconds()),
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
		Secure:   ar.cookieSecure(r),
	})
}

func (ar *authRuntime) clearSessionCookie(w http.ResponseWriter, r *http.Request) {
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookieName,
		Value:    "",
		Path:     "/",
		MaxAge:   -1,
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
		Secure:   ar.cookieSecure(r),
	})
}

//--------//
// 模块：JWT — 签发与从 Cookie 解析
func (ar *authRuntime) signToken(uid uint64) (string, error) {
	now := time.Now()
	claims := sessionClaims{
		UID: uid,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(now.Add(sessionDuration)),
			IssuedAt:  jwt.NewNumericDate(now),
		},
	}
	t := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return t.SignedString(ar.secret)
}

func (ar *authRuntime) parseSession(r *http.Request) (uint64, error) {
	c, err := r.Cookie(sessionCookieName)
	if err != nil || c.Value == "" {
		return 0, err
	}
	var claims sessionClaims
	tok, err := jwt.ParseWithClaims(c.Value, &claims, func(t *jwt.Token) (any, error) {
		if t.Method != jwt.SigningMethodHS256 {
			return nil, errors.New("bad alg")
		}
		return ar.secret, nil
	})
	if err != nil || !tok.Valid {
		return 0, errors.New("invalid token")
	}
	if claims.UID == 0 {
		return 0, errors.New("invalid uid")
	}
	return claims.UID, nil
}

//--------//
// 模块：中间件 — HTTP / WebSocket 鉴权包装
func (ar *authRuntime) requireAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		uid, err := ar.parseSession(r)
		if err != nil {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusUnauthorized)
			_ = json.NewEncoder(w).Encode(map[string]string{"error": "unauthorized"})
			return
		}
		next(w, r.WithContext(context.WithValue(r.Context(), ctxKeyUID, uid)))
	}
}

func (ar *authRuntime) requireAuthWS(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if _, err := ar.parseSession(r); err != nil {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		next(w, r)
	}
}

func uidFromRequest(r *http.Request) (uint64, bool) {
	v := r.Context().Value(ctxKeyUID)
	id, ok := v.(uint64)
	return id, ok
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

//--------//
// 模块：HTTP 处理器 — /api/auth/*（登录、登出、me、改密）
func (ar *authRuntime) handleLogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	time.Sleep(90 * time.Millisecond)
	body, err := io.ReadAll(io.LimitReader(r.Body, 1<<14))
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "read"})
		return
	}
	var in struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if json.Unmarshal(body, &in) != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid json"})
		return
	}
	u := strings.TrimSpace(in.Username)
	p := in.Password
	if u == "" || p == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missing credentials"})
		return
	}
	var id uint64
	var hash []byte
	var mustChange int
	err = ar.db.QueryRowContext(r.Context(),
		`SELECT id, password_hash, must_change_password FROM host_users WHERE username = ? LIMIT 1`,
		u).Scan(&id, &hash, &mustChange)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "invalid username or password"})
			return
		}
		log.Println("auth login db:", err)
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "server"})
		return
	}
	if bcrypt.CompareHashAndPassword(hash, []byte(p)) != nil {
		writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "invalid username or password"})
		return
	}
	token, err := ar.signToken(id)
	if err != nil {
		log.Println("auth sign:", err)
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "server"})
		return
	}
	ar.setSessionCookie(w, r, token)
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":                     true,
		"username":               u,
		"must_change_password":   mustChange != 0,
	})
}

func (ar *authRuntime) handleLogout(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	ar.clearSessionCookie(w, r)
	writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
}

func (ar *authRuntime) handleMe(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	uid, ok := uidFromRequest(r)
	if !ok {
		writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "unauthorized"})
		return
	}
	var name string
	var mustChange int
	err := ar.db.QueryRowContext(r.Context(),
		`SELECT username, must_change_password FROM host_users WHERE id = ?`,
		uid).Scan(&name, &mustChange)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			ar.clearSessionCookie(w, r)
			writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "unauthorized"})
			return
		}
		log.Println("auth me:", err)
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "server"})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":                   true,
		"username":             name,
		"must_change_password": mustChange != 0,
	})
}

func (ar *authRuntime) handleChangePassword(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	uid, ok := uidFromRequest(r)
	if !ok {
		writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "unauthorized"})
		return
	}
	body, err := io.ReadAll(io.LimitReader(r.Body, 1<<14))
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "read"})
		return
	}
	var in struct {
		NewPassword       string `json:"new_password"`
		NewPasswordConfirm string `json:"new_password_confirm"`
		CurrentPassword   string `json:"current_password"`
	}
	if json.Unmarshal(body, &in) != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid json"})
		return
	}
	n1 := strings.TrimSpace(in.NewPassword)
	n2 := strings.TrimSpace(in.NewPasswordConfirm)
	if n1 == "" || n2 == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missing password"})
		return
	}
	if n1 != n2 {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "passwords do not match"})
		return
	}
	if len(n1) < minNewPasswordLen {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "password too short"})
		return
	}
	var hash []byte
	var mustChange int
	err = ar.db.QueryRowContext(r.Context(),
		`SELECT password_hash, must_change_password FROM host_users WHERE id = ?`,
		uid).Scan(&hash, &mustChange)
	if err != nil {
		log.Println("auth chpwd select:", err)
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "server"})
		return
	}
	if mustChange == 0 {
		if in.CurrentPassword == "" {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "current password required"})
			return
		}
		if bcrypt.CompareHashAndPassword(hash, []byte(in.CurrentPassword)) != nil {
			writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "current password incorrect"})
			return
		}
	}
	newHash, err := bcrypt.GenerateFromPassword([]byte(n1), bcrypt.DefaultCost)
	if err != nil {
		log.Println("auth chpwd hash:", err)
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "server"})
		return
	}
	_, err = ar.db.ExecContext(r.Context(),
		`UPDATE host_users SET password_hash = ?, must_change_password = 0, updated_at = CURRENT_TIMESTAMP WHERE id = ?`,
		newHash, uid)
	if err != nil {
		log.Println("auth chpwd update:", err)
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "server"})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "must_change_password": false})
}
