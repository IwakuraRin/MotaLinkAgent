// 作用：提供文件上传、下载、删除、复制和移动 API，并把底层文件动作交给 C 控制核心执行。
package httpapi

import (
	"fmt"
	"io"
	"net/http"
	"path/filepath"
	"strings"

	"omniroam/hostpc-api/internal/control"
)

const maxUploadBytes int64 = 2 << 30

type fsPathRequest struct {
	Path string `json:"path"`
}

type fsPairRequest struct {
	Src string `json:"src"`
	Dst string `json:"dst"`
}

// ==================== 文件删除接口 ====================
// 作用：删除一个文件或目录，递归逻辑由 C 层执行。
// ====================================================
func (s *Server) handleFSDelete(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	var req fsPathRequest
	if err := readJSON(r, &req); err != nil || strings.TrimSpace(req.Path) == "" {
		jsonError(w, http.StatusBadRequest, "missing path")
		return
	}
	result, err := control.NewClient(s.cfg.ControlCore).FSDelete(r.Context(), req.Path)
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, result)
}

// ==================== 文件复制接口 ====================
// 作用：复制文件或目录到目标绝对路径，具体递归复制由 C 层执行。
// ====================================================
func (s *Server) handleFSCopy(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	var req fsPairRequest
	if err := readJSON(r, &req); err != nil || strings.TrimSpace(req.Src) == "" || strings.TrimSpace(req.Dst) == "" {
		jsonError(w, http.StatusBadRequest, "missing src or dst")
		return
	}
	result, err := control.NewClient(s.cfg.ControlCore).FSCopy(r.Context(), req.Src, req.Dst)
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, result)
}

// ==================== 文件移动接口 ====================
// 作用：移动或重命名文件目录，跨分区时由 C 层复制后删除。
// ====================================================
func (s *Server) handleFSMove(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	var req fsPairRequest
	if err := readJSON(r, &req); err != nil || strings.TrimSpace(req.Src) == "" || strings.TrimSpace(req.Dst) == "" {
		jsonError(w, http.StatusBadRequest, "missing src or dst")
		return
	}
	result, err := control.NewClient(s.cfg.ControlCore).FSMove(r.Context(), req.Src, req.Dst)
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, result)
}

// ==================== 文件上传接口 ====================
// 作用：接收 multipart 文件流，逐个交给 C 层写入当前目录。
// ====================================================
func (s *Server) handleFSUpload(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	dir := strings.TrimSpace(r.URL.Query().Get("dir"))
	if dir == "" {
		jsonError(w, http.StatusBadRequest, "missing dir")
		return
	}

	r.Body = http.MaxBytesReader(w, r.Body, maxUploadBytes)
	reader, err := r.MultipartReader()
	if err != nil {
		jsonError(w, http.StatusBadRequest, "invalid multipart upload")
		return
	}

	client := control.NewClient(s.cfg.ControlCore)
	uploaded := make([]string, 0, 1)
	for {
		part, err := reader.NextPart()
		if err == io.EOF {
			break
		}
		if err != nil {
			jsonError(w, http.StatusBadRequest, "read multipart failed")
			return
		}
		if part.FormName() != "file" || part.FileName() == "" {
			_ = part.Close()
			continue
		}
		name := filepath.Base(part.FileName())
		if name == "." || name == string(filepath.Separator) || strings.TrimSpace(name) == "" {
			_ = part.Close()
			jsonError(w, http.StatusBadRequest, "invalid filename")
			return
		}
		target := filepath.Join(dir, name)
		if _, err := client.FSWrite(r.Context(), target, part, true); err != nil {
			_ = part.Close()
			jsonError(w, http.StatusBadGateway, err.Error())
			return
		}
		_ = part.Close()
		uploaded = append(uploaded, target)
	}
	if len(uploaded) == 0 {
		jsonError(w, http.StatusBadRequest, "no file uploaded")
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "uploaded": uploaded})
}

// ==================== 文件下载接口 ====================
// 作用：先让 C 层确认文件信息，再通过 C 层 stdout 把文件流转发给浏览器。
// ====================================================
func (s *Server) handleFSDownload(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodGet) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	path := strings.TrimSpace(r.URL.Query().Get("path"))
	if path == "" {
		jsonError(w, http.StatusBadRequest, "missing path")
		return
	}

	client := control.NewClient(s.cfg.ControlCore)
	stat, err := client.FSStat(r.Context(), path)
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	if stat.IsDir {
		jsonError(w, http.StatusBadRequest, "cannot download a directory")
		return
	}

	stream, err := client.FSRead(r.Context(), path)
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	defer stream.Body.Close()

	name := stat.Name
	if name == "" {
		name = filepath.Base(path)
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=%q", name))
	if stat.Size >= 0 {
		w.Header().Set("Content-Length", fmt.Sprintf("%d", stat.Size))
	}
	w.WriteHeader(http.StatusOK)
	_, copyErr := io.Copy(w, stream.Body)
	waitErr := stream.Wait()
	if copyErr != nil || waitErr != nil {
		return
	}
}
