package main

import (
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"
)

const fsListMaxEntries = 2048

type fsListEntry struct {
	Name    string `json:"name"`
	IsDir   bool   `json:"is_dir"`
	Size    int64  `json:"size"`
	ModTime string `json:"mod_time"` // RFC3339, empty if unknown
	Mode    uint32 `json:"mode"`     // unix permission bits (ls -l style)
}

// handleFSList serves GET /api/fs/list?path=/absolute/dir — Linux host only; requires auth.
func handleFSList(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	if runtime.GOOS != "linux" {
		writeJSON(w, http.StatusNotImplemented, map[string]string{
			"error": "filesystem browser is only available when HostPC runs on Linux",
		})
		return
	}

	raw := strings.TrimSpace(r.URL.Query().Get("path"))
	if raw == "" {
		raw = "/"
	}
	if strings.Contains(raw, "\x00") {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid path"})
		return
	}
	if !filepath.IsAbs(raw) {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "path must be absolute"})
		return
	}
	clean := filepath.Clean(raw)
	if clean == "." || !strings.HasPrefix(clean, string(filepath.Separator)) {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid path"})
		return
	}

	st, err := os.Stat(clean)
	if err != nil {
		if os.IsNotExist(err) {
			writeJSON(w, http.StatusNotFound, map[string]string{"error": "not found"})
			return
		}
		if os.IsPermission(err) {
			writeJSON(w, http.StatusForbidden, map[string]string{"error": "permission denied"})
			return
		}
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "stat failed"})
		return
	}
	if !st.IsDir() {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "not a directory"})
		return
	}

	entries, err := os.ReadDir(clean)
	if err != nil {
		if os.IsPermission(err) {
			writeJSON(w, http.StatusForbidden, map[string]string{"error": "permission denied"})
			return
		}
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "read directory failed"})
		return
	}

	out := make([]fsListEntry, 0, len(entries))
	truncated := false
	for _, de := range entries {
		if len(out) >= fsListMaxEntries {
			truncated = true
			break
		}
		name := de.Name()
		if name == "." || name == ".." {
			continue
		}
		info, err := de.Info()
		if err != nil {
			out = append(out, fsListEntry{Name: name, IsDir: de.IsDir()})
			continue
		}
		var mod string
		if !info.ModTime().IsZero() {
			mod = info.ModTime().UTC().Format(time.RFC3339)
		}
		out = append(out, fsListEntry{
			Name:    name,
			IsDir:   info.IsDir(),
			Size:    info.Size(),
			ModTime: mod,
			Mode:    uint32(info.Mode().Perm()),
		})
	}

	sort.Slice(out, func(i, j int) bool {
		if out[i].IsDir != out[j].IsDir {
			return out[i].IsDir
		}
		return strings.ToLower(out[i].Name) < strings.ToLower(out[j].Name)
	})

	parent := filepath.Dir(clean)
	if parent == clean || parent == "" {
		parent = "/"
	}

	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"path":      clean,
		"parent":    parent,
		"entries":   out,
		"truncated": truncated,
	})
}
