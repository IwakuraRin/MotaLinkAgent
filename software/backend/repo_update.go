// 展示代码结构：
//   · 解析本机 Git 仓库根、origin URL、分支与 fetch/pull
//   · handleRepoStatus / handleRepoPull：供其它流程查询或拉取（与 updates.go 自更新可并存）
//
package main

import (
	"context"
	"encoding/json"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

// hostGitRepoRoot is the absolute path to the repository root (directory containing .git).
// Set from main() after flag parsing; empty disables /api/repo/*.
var hostGitRepoRoot string

//--------//
// 模块：仓库根解析 — 从参数或当前目录向上查找 .git
func resolveGitRepoRoot(explicit string) string {
	explicit = strings.TrimSpace(explicit)
	if explicit != "" {
		if abs, err := filepath.Abs(explicit); err == nil && isGitDir(abs) {
			return abs
		}
		return ""
	}
	wd, err := os.Getwd()
	if err != nil {
		return ""
	}
	return walkFindGitRoot(wd)
}

func isGitDir(dir string) bool {
	st, err := os.Stat(filepath.Join(dir, ".git"))
	if err != nil {
		return false
	}
	return st.IsDir() || st.Mode().IsRegular()
}

func walkFindGitRoot(start string) string {
	dir := start
	for i := 0; i < 32; i++ {
		if isGitDir(dir) {
			abs, err := filepath.Abs(dir)
			if err != nil {
				return dir
			}
			return abs
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return ""
}

func gitTrim(s string) string {
	return strings.TrimSpace(strings.TrimSuffix(s, "\n"))
}

//--------//
// 模块：git 封装 — 单行输出、remote URL、当前分支
func gitLine(ctx context.Context, dir string, args ...string) (string, error) {
	cmd := exec.CommandContext(ctx, "git", args...)
	cmd.Dir = dir
	out, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return gitTrim(string(out)), nil
}

func remoteURL(ctx context.Context, dir string) string {
	u, err := gitLine(ctx, dir, "remote", "get-url", "origin")
	if err != nil || u == "" {
		u, _ = gitLine(ctx, dir, "remote", "get-url", "orgin")
	}
	return u
}

func currentBranch(ctx context.Context, dir string) string {
	b, err := gitLine(ctx, dir, "rev-parse", "--abbrev-ref", "HEAD")
	if err != nil || b == "" || b == "HEAD" {
		return "main"
	}
	return b
}

//--------//
// 模块：HTTP — 仓库状态（ahead/behind、SHA）
func handleRepoStatus(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	if hostGitRepoRoot == "" {
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":    false,
			"error": "git_repo_not_configured",
		})
		return
	}
	dir := hostGitRepoRoot
	ctx, cancel := context.WithTimeout(r.Context(), 90*time.Second)
	defer cancel()

	localSHA, err := gitLine(ctx, dir, "rev-parse", "HEAD")
	if err != nil || localSHA == "" {
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":    false,
			"error": "not_a_git_repo",
		})
		return
	}

	branch := currentBranch(ctx, dir)
	remoteRef := "refs/remotes/origin/" + branch
	url := remoteURL(ctx, dir)

	fetchErr := ""
	if err := exec.CommandContext(ctx, "git", "-C", dir, "fetch", "origin").Run(); err != nil {
		fetchErr = err.Error()
	}

	remoteSHA, rerr := gitLine(ctx, dir, "rev-parse", remoteRef)
	if rerr != nil || remoteSHA == "" {
		// fallback: main
		remoteSHA, rerr = gitLine(ctx, dir, "rev-parse", "refs/remotes/origin/main")
		if rerr == nil && remoteSHA != "" {
			branch = "main"
			remoteRef = "refs/remotes/origin/main"
		}
	}

	behind := false
	ahead := false
	originBranch := "origin/" + branch
	if fetchErr == "" && remoteSHA != "" && localSHA != remoteSHA {
		cmdA := exec.CommandContext(ctx, "git", "-C", dir, "merge-base", "--is-ancestor", "HEAD", originBranch)
		if cmdA.Run() == nil {
			behind = true
		} else {
			cmdB := exec.CommandContext(ctx, "git", "-C", dir, "merge-base", "--is-ancestor", originBranch, "HEAD")
			if cmdB.Run() == nil {
				ahead = true
			}
		}
	}

	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":          true,
		"repo_root":   dir,
		"branch":      branch,
		"local_sha":   localSHA,
		"remote_sha":  remoteSHA,
		"remote_url":  url,
		"behind":      behind,
		"ahead":       ahead,
		"fetch_ok":    fetchErr == "",
		"fetch_error": fetchErr,
	})
}

//--------//
// 模块：HTTP — git pull origin <branch>
func handleRepoPull(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	if hostGitRepoRoot == "" {
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": false, "error": "git_repo_not_configured"})
		return
	}
	dir := hostGitRepoRoot
	ctx, cancel := context.WithTimeout(r.Context(), 120*time.Second)
	defer cancel()

	branch := currentBranch(ctx, dir)
	cmd := exec.CommandContext(ctx, "git", "-C", dir, "pull", "--no-edit", "origin", branch)
	out, err := cmd.CombinedOutput()
	msg := strings.TrimSpace(string(out))
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":     false,
			"error":  "git_pull_failed",
			"detail": msg,
		})
		return
	}
	newSHA, _ := gitLine(ctx, dir, "rev-parse", "HEAD")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":       true,
		"message":  msg,
		"head_sha": newSHA,
	})
}
