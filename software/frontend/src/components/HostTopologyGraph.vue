<script setup lang="ts">
// 展示代码结构：
//   · 节点/边常量 EDGES · SVG 自动布局 · 边选中与日志面板 · 响应缩放
//
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { t } from '../i18n'
import type { TopologyEdgeId } from '../topology'

const props = defineProps<{
  edgeLogs: Record<string, string[]>
}>()

const VB_W = 920
const VB_H = 460

type NodeDef = { id: string; x: number; y: number; w: number; h: number }

type EdgeDef = {
  id: TopologyEdgeId
  from: string
  to: string
  yOff?: number
}

const EDGES: EdgeDef[] = [
  { id: 'e_ws', from: 'browser', to: 'hostpc', yOff: -6 },
  { id: 'e_http_api', from: 'browser', to: 'hostpc', yOff: 8 },
  { id: 'e_file_settings', from: 'hostpc', to: 'settings_file' },
  { id: 'e_ros_host', from: 'hostpc', to: 'ros' },
  { id: 'e_serial', from: 'ros', to: 'esp32' },
  { id: 'e_cam', from: 'usbcam', to: 'ros' },
  { id: 'e_vision', from: 'ros', to: 'vision' },
  { id: 'e_video_ui', from: 'browser', to: 'stream' },
]

//--------//
// 模块：布局 — 计算各节点坐标（浏览器/HostPC/ROS/外设）
/** 分层自动布局：上层 = 浏览器 / 上位机 / ROS；中层 = 流与配置；底层 = 摄像头 / 视觉 / ESP32 */
function computeAutoLayout(): NodeDef[] {
  const topY = 28
  const midY = 132
  const botY = 318
  const gap = 72

  const bw = 120
  const hw = 132
  const rw = 108
  const topRowW = bw + gap + hw + gap + rw
  const xStart = (VB_W - topRowW) / 2

  const browser: NodeDef = { id: 'browser', x: xStart, y: topY, w: bw, h: 44 }
  const hostpc: NodeDef = { id: 'hostpc', x: xStart + bw + gap, y: topY, w: hw, h: 44 }
  const ros: NodeDef = { id: 'ros', x: xStart + bw + gap + hw + gap, y: topY, w: rw, h: 44 }

  const stream: NodeDef = {
    id: 'stream',
    x: browser.x + browser.w / 2 - 59,
    y: midY,
    w: 118,
    h: 36,
  }
  const settings_file: NodeDef = {
    id: 'settings_file',
    x: hostpc.x + hostpc.w / 2 - 54,
    y: midY + 4,
    w: 108,
    h: 34,
  }

  const uW = 118
  const vW = 152
  const eW = 100
  const botGap = 56
  const botRowW = uW + botGap + vW + botGap + eW
  const bx0 = (VB_W - botRowW) / 2

  const usbcam: NodeDef = { id: 'usbcam', x: bx0, y: botY, w: uW, h: 44 }
  const vision: NodeDef = { id: 'vision', x: bx0 + uW + botGap, y: botY, w: vW, h: 44 }
  const esp32: NodeDef = { id: 'esp32', x: bx0 + uW + botGap + vW + botGap, y: botY, w: eW, h: 44 }

  return [browser, hostpc, ros, settings_file, stream, usbcam, vision, esp32]
}

const nodes = ref<NodeDef[]>(computeAutoLayout())
const svgRef = ref<SVGSVGElement | null>(null)

let nodeDragActive = false
let dragNodeId: string | null = null
let dragStartSvg = { x: 0, y: 0 }
let dragOrigin = { x: 0, y: 0 }

function svgPoint(clientX: number, clientY: number): { x: number; y: number } {
  const svg = svgRef.value
  if (!svg) return { x: 0, y: 0 }
  const pt = svg.createSVGPoint()
  pt.x = clientX
  pt.y = clientY
  const ctm = svg.getScreenCTM()
  if (!ctm) return { x: 0, y: 0 }
  const p = pt.matrixTransform(ctm.inverse())
  return { x: p.x, y: p.y }
}

function endNodeDrag() {
  if (!nodeDragActive) return
  nodeDragActive = false
  dragNodeId = null
  document.removeEventListener('pointermove', onNodePointerMove, true)
  document.removeEventListener('pointerup', endNodeDrag, true)
  document.removeEventListener('pointercancel', endNodeDrag, true)
}

function onNodePointerMove(e: PointerEvent) {
  if (!nodeDragActive || !dragNodeId) return
  if ((e.buttons & 1) === 0) {
    endNodeDrag()
    return
  }
  const cur = svgPoint(e.clientX, e.clientY)
  const dx = cur.x - dragStartSvg.x
  const dy = cur.y - dragStartSvg.y
  const list = nodes.value.map((n) => {
    if (n.id !== dragNodeId) return n
    return {
      ...n,
      x: Math.max(4, Math.min(VB_W - n.w - 4, dragOrigin.x + dx)),
      y: Math.max(4, Math.min(VB_H - n.h - 4, dragOrigin.y + dy)),
    }
  })
  nodes.value = list
}

function onNodePointerDown(e: PointerEvent, n: NodeDef) {
  if (e.button !== 0) return
  e.stopPropagation()
  e.preventDefault()
  endNodeDrag()
  nodeDragActive = true
  dragNodeId = n.id
  dragStartSvg = svgPoint(e.clientX, e.clientY)
  dragOrigin = { x: n.x, y: n.y }
  document.addEventListener('pointermove', onNodePointerMove, true)
  document.addEventListener('pointerup', endNodeDrag, true)
  document.addEventListener('pointercancel', endNodeDrag, true)
}

function applyAutoLayout() {
  endNodeDrag()
  nodes.value = computeAutoLayout()
}

function anchorOut(n: NodeDef) {
  return { x: n.x + n.w, y: n.y + n.h / 2 }
}
function anchorIn(n: NodeDef) {
  return { x: n.x, y: n.y + n.h / 2 }
}
function anchorBottom(n: NodeDef) {
  return { x: n.x + n.w / 2, y: n.y + n.h }
}
function anchorTop(n: NodeDef) {
  return { x: n.x + n.w / 2, y: n.y }
}

function cubicMid(
  p0: { x: number; y: number },
  p1: { x: number; y: number },
  p2: { x: number; y: number },
  p3: { x: number; y: number },
  tt: number,
) {
  const u = 1 - tt
  const u2 = u * u
  const u3 = u2 * u
  const t2 = tt * tt
  const t3 = t2 * tt
  return {
    x: u3 * p0.x + 3 * u2 * tt * p1.x + 3 * u * t2 * p2.x + t3 * p3.x,
    y: u3 * p0.y + 3 * u2 * tt * p1.y + 3 * u * t2 * p2.y + t3 * p3.y,
  }
}

function edgeGeometry(e: EdgeDef, nm: Record<string, NodeDef>) {
  const fm = nm[e.from]
  const to = nm[e.to]
  if (!fm || !to) return { d: '', mid: { x: 0, y: 0 } }

  let x1: number, y1: number, x2: number, y2: number
  if (e.to === 'settings_file') {
    const o = anchorBottom(fm)
    const i = anchorTop(to)
    x1 = o.x
    y1 = o.y
    x2 = i.x
    y2 = i.y
  } else if (e.from === 'usbcam') {
    const o = anchorOut(fm)
    const i = anchorIn(to)
    x1 = o.x
    y1 = o.y
    x2 = i.x
    y2 = i.y + 18
  } else if (e.from === 'browser' && e.to === 'stream') {
    const o = anchorBottom(fm)
    const i = anchorTop(to)
    x1 = o.x
    y1 = o.y
    x2 = i.x
    y2 = i.y
  } else {
    const o = anchorOut(fm)
    const i = anchorIn(to)
    x1 = o.x
    y1 = o.y + (e.yOff ?? 0)
    x2 = i.x
    y2 = i.y + (e.yOff ?? 0)
  }

  const dx = Math.max(72, Math.abs(x2 - x1) * 0.45)
  const c1x = x1 + dx
  const c2x = x2 - dx
  const d = `M ${x1} ${y1} C ${c1x} ${y1}, ${c2x} ${y2}, ${x2} ${y2}`
  const mid = cubicMid({ x: x1, y: y1 }, { x: c1x, y: y1 }, { x: c2x, y: y2 }, { x: x2, y: y2 }, 0.5)
  return { d, mid }
}

const PANEL_W = 180
const PANEL_H = 120
const BTN_W = 48
const BTN_H = 22

function panelPosition(mid: { x: number; y: number }) {
  let x = mid.x - PANEL_W / 2
  let y = mid.y - PANEL_H / 2 - 24
  x = Math.max(6, Math.min(VB_W - PANEL_W - 6, x))
  y = Math.max(6, Math.min(VB_H - PANEL_H - 6, y))
  return { x, y }
}

const nodeMap = computed(() => Object.fromEntries(nodes.value.map((n) => [n.id, n])))

const edgeLayouts = computed(() => {
  const nm = nodeMap.value
  return EDGES.map((e) => ({
    e,
    ...edgeGeometry(e, nm),
  }))
})

function nodeLabel(id: string): string {
  return t(`graph.node.${id}`)
}

function edgeLabel(id: TopologyEdgeId): string {
  return t(`graph.edge.${id}`)
}

function linesFor(id: TopologyEdgeId): string[] {
  const arr = props.edgeLogs[id]
  return Array.isArray(arr) ? arr : []
}

const openEdgeLogId = ref<TopologyEdgeId | null>(null)

const openEdgeLayout = computed(() => {
  const id = openEdgeLogId.value
  if (!id) return null
  return edgeLayouts.value.find((l) => l.e.id === id) ?? null
})

const openPanelPos = computed(() => {
  const lay = openEdgeLayout.value
  if (!lay) return { x: 0, y: 0 }
  return panelPosition(lay.mid)
})

function toggleEdgeLog(id: TopologyEdgeId) {
  openEdgeLogId.value = openEdgeLogId.value === id ? null : id
}

function closeEdgeLogPanel() {
  openEdgeLogId.value = null
}

function onKeyDown(e: KeyboardEvent) {
  if (e.key === 'Escape') closeEdgeLogPanel()
}

onMounted(() => {
  nodes.value = computeAutoLayout()
  window.addEventListener('keydown', onKeyDown)
})

onUnmounted(() => {
  endNodeDrag()
  window.removeEventListener('keydown', onKeyDown)
})
</script>

<template>
  <div class="flex min-h-0 min-w-0 flex-1 flex-col border-pve-border bg-pve-bg lg:border-l">
    <div class="pve-panel-title flex items-center justify-between gap-2 pr-2">
      <span>{{ t('graph.title') }}</span>
      <button
        type="button"
        class="shrink-0 rounded border border-pve-border bg-pve-panel px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wide text-pve-text hover:bg-pve-header"
        @click="applyAutoLayout"
      >
        {{ t('graph.autoLayout') }}
      </button>
    </div>
    <div class="relative min-h-[440px] flex-1 overflow-auto bg-[#222] p-2">
      <svg
        ref="svgRef"
        class="mx-auto block h-auto w-full max-w-[960px] touch-none"
        :viewBox="`0 0 ${VB_W} ${VB_H}`"
        preserveAspectRatio="xMidYMid meet"
      >
        <defs>
          <marker id="arrowhead" markerWidth="6" markerHeight="6" refX="5" refY="3" orient="auto">
            <path d="M0,0 L6,3 L0,6 Z" fill="#5a8fc4" />
          </marker>
        </defs>

        <g v-for="lay in edgeLayouts" :key="lay.e.id">
          <path
            :d="lay.d"
            fill="none"
            stroke="#4a7ab0"
            stroke-width="1.35"
            stroke-opacity="0.9"
            marker-end="url(#arrowhead)"
            class="pointer-events-none"
          />
        </g>

        <g
          v-for="n in nodes"
          :key="n.id"
          class="cursor-grab"
          @pointerdown="(ev) => onNodePointerDown(ev, n)"
        >
          <rect
            :x="n.x"
            :y="n.y"
            :width="n.w"
            :height="n.h"
            rx="6"
            fill="#2f2f2f"
            stroke="#6a8a5a"
            stroke-width="1.5"
            class="hover:stroke-pve-accent2"
          />
          <text
            :x="n.x + n.w / 2"
            :y="n.y + n.h / 2 + 4"
            text-anchor="middle"
            fill="#d4d4d4"
            class="pointer-events-none"
            style="font-size: 11px"
          >
            {{ nodeLabel(n.id) }}
          </text>
        </g>

        <g v-for="lay in edgeLayouts" :key="'btn-' + lay.e.id" class="pointer-events-auto">
          <foreignObject
            :x="lay.mid.x - BTN_W / 2"
            :y="lay.mid.y - BTN_H / 2"
            :width="BTN_W"
            :height="BTN_H"
          >
            <div xmlns="http://www.w3.org/1999/xhtml" class="flex h-full w-full items-center justify-center">
              <button
                type="button"
                class="rounded border border-[#1a4d2e] bg-[#2d8f4e] px-1.5 py-0.5 text-[9px] font-bold leading-none text-white shadow hover:bg-[#36a85c] active:bg-[#257a45]"
                :class="openEdgeLogId === lay.e.id ? 'ring-2 ring-[#9fe6b8]' : ''"
                :title="t('graph.edgeLogBtnHint')"
                @click.stop="toggleEdgeLog(lay.e.id)"
              >
                {{ t('graph.edgeLogBtn') }}
              </button>
            </div>
          </foreignObject>
        </g>

        <g v-if="openEdgeLayout" class="pointer-events-auto">
          <foreignObject
            :x="openPanelPos.x"
            :y="openPanelPos.y"
            :width="PANEL_W"
            :height="PANEL_H"
          >
            <div
              xmlns="http://www.w3.org/1999/xhtml"
              class="flex h-full flex-col overflow-hidden rounded border border-[#2d6a45] bg-[#0d1410]/98 shadow-lg ring-1 ring-[#3d9b5c]/40"
            >
              <div class="flex shrink-0 items-center justify-between gap-1 border-b border-[#2a4a35] bg-[#122218] px-1.5 py-1">
                <span class="text-[9px] font-semibold text-[#9fe6b8]">{{ edgeLabel(openEdgeLayout.e.id) }}</span>
                <button
                  type="button"
                  class="rounded px-1.5 py-0.5 text-[11px] font-bold leading-none text-[#7dcea0] hover:bg-[#1a3024]"
                  :aria-label="t('graph.edgeLogClose')"
                  @click.stop="closeEdgeLogPanel"
                >
                  ×
                </button>
              </div>
              <div class="min-h-0 flex-1 overflow-auto p-1.5 font-mono text-[9px] leading-snug text-[#9bdc9b]">
                <div
                  v-for="(ln, i) in linesFor(openEdgeLayout.e.id)"
                  :key="openEdgeLayout.e.id + i"
                  class="whitespace-pre-wrap break-all"
                >
                  {{ ln }}
                </div>
                <div v-if="linesFor(openEdgeLayout.e.id).length === 0" class="text-[#666]">
                  {{ t('graph.edgeEmpty') }}
                </div>
              </div>
            </div>
          </foreignObject>
        </g>
      </svg>
    </div>
  </div>
</template>
