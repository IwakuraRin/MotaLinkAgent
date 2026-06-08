// 作用：提供 AmseokBot 控制 API，把手机 App 或前端请求转发给 C 语言控制核心。
package httpapi

import (
	"net/http"

	"omniroam/hostpc-api/internal/control"
)

// ==================== 控制核心健康接口 ====================
// 作用：检查 C 控制核心是否存在、可执行，并返回底层控制能力清单。
// ========================================================
func (s *Server) handleControlHealth(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodGet) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	result, err := control.NewClient(s.cfg.ControlCore).Health(r.Context())
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, result)
}

// ==================== 底盘控制接口 ====================
// 作用：接收 vx、vy、wz，调用 C 层生成下位机底盘速度协议帧。
// ====================================================
func (s *Server) handleControlChassisMove(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	var req control.ChassisCommand
	if err := readJSON(r, &req); err != nil {
		jsonError(w, http.StatusBadRequest, "invalid chassis command")
		return
	}
	result, err := control.NewClient(s.cfg.ControlCore).MoveChassis(r.Context(), req)
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, result)
}

// ==================== 停止控制接口 ====================
// 作用：向 C 层请求停止协议帧，后续接入真实串口后用于急停和安全停机。
// ====================================================
func (s *Server) handleControlStop(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	result, err := control.NewClient(s.cfg.ControlCore).Stop(r.Context())
	if err != nil {
		jsonError(w, http.StatusBadGateway, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, result)
}
