export type DesktopAppKind = 'terminal' | 'logs' | 'files' | 'about'

export interface DesktopWin {
  id: number
  kind: DesktopAppKind
  title: string
  x: number
  y: number
  w: number
  h: number
  z: number
  minimized: boolean
  maximized: boolean
  /** Saved geometry while maximized (restore on green dot or un-maximize). */
  restoreBounds?: { x: number; y: number; w: number; h: number }
}
