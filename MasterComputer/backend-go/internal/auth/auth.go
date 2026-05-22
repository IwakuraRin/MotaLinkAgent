// 作用：读取兼容 C 后端的账号文件，提供登录校验、内存会话和改密能力。
package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"errors"
	"fmt"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"
)

const sessionCookie = "HostSession"
const sessionTTL = 7 * 24 * time.Hour

// ==================== 账号与会话结构 ====================
// 作用：保存单用户账号记录和浏览器登录态。
// ========================================================
type UserRecord struct {
	Username           string
	SaltHex            string
	PasswordHashHex    string
	MustChangePassword bool
}

type sessionRecord struct {
	username  string
	expiresAt time.Time
}

type Store struct {
	path     string
	mu       sync.Mutex
	user     UserRecord
	sessions map[string]sessionRecord
}

// ==================== 账号文件加载 ====================
// 作用：读取 C 后端同格式的 cauth 文件；不存在时创建默认账号。
// ======================================================
func NewStore(path string) (*Store, error) {
	store := &Store{path: path, sessions: map[string]sessionRecord{}}
	if err := store.loadOrCreate(); err != nil {
		return nil, err
	}
	return store, nil
}

func (s *Store) loadOrCreate() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	body, err := os.ReadFile(s.path)
	if errors.Is(err, os.ErrNotExist) {
		username := getenvDefault("HOSTPC_USER", defaultUsername)
		password := getenvDefault("HOSTPC_PASSWORD", defaultPassword)
		salt := randomHex(16)
		s.user = UserRecord{
			Username:           username,
			SaltHex:            salt,
			PasswordHashHex:    hashPassword(salt, password),
			MustChangePassword: true,
		}
		return s.writeLocked()
	}
	if err != nil {
		return err
	}

	record := UserRecord{}
	for _, line := range strings.Split(string(body), "\n") {
		line = strings.TrimSpace(line)
		switch {
		case strings.HasPrefix(line, "username="):
			record.Username = strings.TrimPrefix(line, "username=")
		case strings.HasPrefix(line, "salt="):
			record.SaltHex = strings.TrimPrefix(line, "salt=")
		case strings.HasPrefix(line, "hash="):
			record.PasswordHashHex = strings.TrimPrefix(line, "hash=")
		case strings.HasPrefix(line, "must_change_password="):
			record.MustChangePassword = strings.TrimPrefix(line, "must_change_password=") == "1"
		}
	}
	if record.Username == "" || record.SaltHex == "" || record.PasswordHashHex == "" {
		return fmt.Errorf("invalid auth file: %s", s.path)
	}
	s.user = record
	return nil
}

// ==================== 登录会话 ====================
// 作用：校验密码、签发 Cookie、读取当前用户和退出登录。
// ================================================
func (s *Store) Login(w http.ResponseWriter, username string, password string) (UserRecord, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if username != s.user.Username || subtle.ConstantTimeCompare([]byte(hashPassword(s.user.SaltHex, password)), []byte(s.user.PasswordHashHex)) != 1 {
		return UserRecord{}, false
	}
	token := randomHex(32)
	s.sessions[token] = sessionRecord{username: username, expiresAt: time.Now().Add(sessionTTL)}
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookie,
		Value:    token,
		Path:     "/",
		MaxAge:   int(sessionTTL.Seconds()),
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
	})
	return s.user, true
}

func (s *Store) Logout(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if cookie, err := r.Cookie(sessionCookie); err == nil {
		delete(s.sessions, cookie.Value)
	}
	http.SetCookie(w, &http.Cookie{Name: sessionCookie, Value: "", Path: "/", MaxAge: -1, HttpOnly: true, SameSite: http.SameSiteLaxMode})
}

func (s *Store) CurrentUser(r *http.Request) (UserRecord, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	cookie, err := r.Cookie(sessionCookie)
	if err != nil {
		return UserRecord{}, false
	}
	session, ok := s.sessions[cookie.Value]
	if !ok || time.Now().After(session.expiresAt) {
		delete(s.sessions, cookie.Value)
		return UserRecord{}, false
	}
	session.expiresAt = time.Now().Add(sessionTTL)
	s.sessions[cookie.Value] = session
	return s.user, true
}

// ==================== 密码修改 ====================
// 作用：更新账号盐值和密码哈希，并写回兼容 C 后端的账号文件。
// ==================================================
func (s *Store) ChangePassword(newPassword string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.user.SaltHex = randomHex(16)
	s.user.PasswordHashHex = hashPassword(s.user.SaltHex, newPassword)
	s.user.MustChangePassword = false
	return s.writeLocked()
}

func (s *Store) writeLocked() error {
	body := fmt.Sprintf("username=%s\nsalt=%s\nhash=%s\nmust_change_password=%d\n",
		s.user.Username,
		s.user.SaltHex,
		s.user.PasswordHashHex,
		boolInt(s.user.MustChangePassword),
	)
	if err := os.WriteFile(s.path, []byte(body), 0600); err != nil {
		return err
	}
	return nil
}

// ==================== 通用工具 ====================
// 作用：生成随机值、计算与 C 后端一致的 SHA256 密码哈希。
// ==================================================
func hashPassword(saltHex string, password string) string {
	sum := sha256.Sum256([]byte(saltHex + ":" + password))
	return hex.EncodeToString(sum[:])
}

func randomHex(size int) string {
	buf := make([]byte, size)
	if _, err := rand.Read(buf); err != nil {
		panic(err)
	}
	return hex.EncodeToString(buf)
}

func getenvDefault(key string, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

func boolInt(value bool) int {
	if value {
		return 1
	}
	return 0
}
