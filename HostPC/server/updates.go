// 展示代码结构：
//   · updateConfig：GitHub CHANGELOG URL、本地仓库 git 对比、构建状态载荷
//   · handleStatus / handleApply：HTTP API；apply 互斥锁 + 调用自更新 shell
//
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

//--------//
// 模块：配置与辅助 — GitHub slug 解析、拉取 CHANGELOG、git 命令输出
const maxChangelogBytes = 16000

type updateConfig struct {
	h *hub

	RepoRoot      string
	GithubSlug    string // owner/name
	Branch        string
	ChangelogPath string
	ScriptPath    string
}

func parseGithubSlug(s string) (owner, repo string, ok bool) {
	s = strings.TrimSpace(s)
	if s == "" {
		return "", "", false
	}
	parts := strings.SplitN(s, "/", 3)
	if len(parts) != 2 || parts[0] == "" || parts[1] == "" {
		return "", "", false
	}
	return parts[0], parts[1], true
}

func (c *updateConfig) changelogURL() string {
	owner, name, ok := parseGithubSlug(c.GithubSlug)
	if !ok {
		return ""
	}
	p := strings.Trim(strings.ReplaceAll(c.ChangelogPath, "\\", "/"), "/")
	if p == "" {
		p = "CHANGELOG.md"
	}
	return fmt.Sprintf("https://raw.githubusercontent.com/%s/%s/%s/%s", owner, name, c.Branch, p)
}

func fetchChangelogText(ctx context.Context, url string) (text string, errMsg string) {
	if url == "" {
		return "", "no github slug"
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return "", err.Error()
	}
	req.Header.Set("User-Agent", "OmniRoam-HostPC/1.0")
	client := &http.Client{Timeout: 20 * time.Second}
	res, err := client.Do(req)
	if err != nil {
		return "", err.Error()
	}
	defer res.Body.Close()
	b, err := io.ReadAll(io.LimitReader(res.Body, maxChangelogBytes+1))
	if err != nil {
		return "", err.Error()
	}
	if res.StatusCode != http.StatusOK {
		return "", fmt.Sprintf("HTTP %d", res.StatusCode)
	}
	if len(b) > maxChangelogBytes {
		b = b[:maxChangelogBytes]
	}
	return string(b), ""
}

func runGitOutput(ctx context.Context, dir string, args ...string) (string, error) {
	cmd := exec.CommandContext(ctx, "git", append([]string{"-C", dir}, args...)...)
	cmd.Env = append(os.Environ(), "GIT_TERMINAL_PROMPT=0")
	out, err := cmd.CombinedOutput()
	s := strings.TrimSpace(string(out))
	if err != nil {
		if s != "" {
			return "", fmt.Errorf("%w: %s", err, s)
		}
		return "", err
	}
	return s, nil
}

//--------//
// 模块：更新状态 — 本地/远端 SHA、changelog、错误原因
type updateStatusPayload struct {
	Enabled         bool   `json:"enabled"`
	UpdateAvailable bool   `json:"update_available"`
	LocalSHA        string `json:"local_sha,omitempty"`
	RemoteSHA       string `json:"remote_sha,omitempty"`
	Branch          string `json:"branch,omitempty"`
	Changelog       string `json:"changelog,omitempty"`
	ChangelogOK     bool   `json:"changelog_ok"`
	ChangelogError  string `json:"changelog_error,omitempty"`
	GitError        string `json:"git_error,omitempty"`
	Reason          string `json:"reason,omitempty"`
	ScriptPath      string `json:"script_path,omitempty"`
}

func (c *updateConfig) buildStatus(ctx context.Context) updateStatusPayload {
	out := updateStatusPayload{Branch: c.Branch}
	repo := strings.TrimSpace(c.RepoRoot)
	if repo == "" {
		out.Reason = "repo root not configured (set -repo-root or HOSTPC_REPO_ROOT)"
		return out
	}
	if fi, err := os.Stat(repo); err != nil || !fi.IsDir() {
		out.Enabled = false
		out.GitError = "repo root is not a directory"
		return out
	}
	out.Enabled = true
	out.ScriptPath = c.ScriptPath

	gctx, cancel := context.WithTimeout(ctx, 90*time.Second)
	defer cancel()

	if _, err := runGitOutput(gctx, repo, "rev-parse", "--is-inside-work-tree"); err != nil {
		out.GitError = err.Error()
		return out
	}

	if err := func() error {
		_, e := runGitOutput(gctx, repo, "fetch", "origin")
		return e
	}(); err != nil {
		out.GitError = err.Error()
		// Without a successful fetch, origin/* may be stale — do not report updates.
		chURL := c.changelogURL()
		text, cErr := fetchChangelogText(ctx, chURL)
		out.Changelog = text
		if cErr == "" && text != "" {
			out.ChangelogOK = true
		} else if cErr != "" {
			out.ChangelogError = cErr
		}
		return out
	}

	localSHA, err := runGitOutput(gctx, repo, "rev-parse", "HEAD")
	if err != nil {
		out.GitError = err.Error()
		return out
	}
	out.LocalSHA = localSHA

	remoteRef := "origin/" + c.Branch
	remoteSHA, err := runGitOutput(gctx, repo, "rev-parse", remoteRef)
	if err != nil {
		out.GitError = err.Error()
		return out
	}
	out.RemoteSHA = remoteSHA
	out.UpdateAvailable = localSHA != remoteSHA && localSHA != "" && remoteSHA != ""

	chURL := c.changelogURL()
	text, cErr := fetchChangelogText(ctx, chURL)
	out.Changelog = text
	if cErr == "" && text != "" {
		out.ChangelogOK = true
	} else if cErr != "" {
		out.ChangelogError = cErr
	}

	return out
}

//--------//
// 模块：HTTP — GET /api/updates/status
func (c *updateConfig) handleStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	st := c.buildStatus(r.Context())
	_ = json.NewEncoder(w).Encode(st)
}

var deployMu sync.Mutex

//--------//
// 模块：HTTP — POST /api/updates/apply（执行自更新脚本）
func (c *updateConfig) handleApply(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "application/json")

	repo := strings.TrimSpace(c.RepoRoot)
	if repo == "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "repo root not configured"})
		return
	}
	script := strings.TrimSpace(c.ScriptPath)
	if script == "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "update script path empty"})
		return
	}
	if fi, err := os.Stat(script); err != nil || fi.IsDir() {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "update script missing"})
		return
	}

	if !deployMu.TryLock() {
		w.WriteHeader(http.StatusConflict)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "update already running"})
		return
	}
	defer deployMu.Unlock()

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Minute)
	defer cancel()

	st := c.buildStatus(ctx)
	if !st.Enabled || st.GitError != "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]any{"error": "git check failed", "status": st})
		return
	}
	if !st.UpdateAvailable {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": "no update available"})
		return
	}

	if c.h != nil {
		c.h.sendLogEdge("INFO  OmniRoam update.sh started (download → sync → build → restart)", "e_http_api")
	}

	cmd := exec.CommandContext(ctx, "bash", script, repo)
	cmd.Dir = filepath.Dir(script)
	slug := strings.TrimSpace(c.GithubSlug)
	branch := strings.TrimSpace(c.Branch)
	if branch == "" {
		branch = "main"
	}
	cmd.Env = append(os.Environ(),
		"OMNIROAM_REPO_ROOT="+repo,
		"OMNIROAM_GITHUB_BRANCH="+branch,
	)
	if slug != "" {
		cmd.Env = append(cmd.Env, "OMNIROAM_GITHUB_SLUG="+slug)
	}
	out, err := cmd.CombinedOutput()
	text := strings.TrimSpace(string(out))
	exit := 0
	if err != nil {
		if x, ok := err.(*exec.ExitError); ok && x.ProcessState != nil {
			exit = x.ExitCode()
		} else {
			exit = -1
		}
	}

	ok := err == nil && exit == 0
	if c.h != nil {
		if ok {
			c.h.sendLogEdge("INFO  HostPC self-update finished OK — service may have restarted", "e_http_api")
		} else {
			c.h.sendLogEdge(fmt.Sprintf("WARN  HostPC self-update failed (exit %d)", exit), "e_http_api")
		}
	}
	if !ok {
		log.Printf("updates apply: exit=%d err=%v output=%s", exit, err, text)
	}

	w.WriteHeader(http.StatusOK)
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":        ok,
		"exit_code": exit,
		"output":    text,
	})
}
