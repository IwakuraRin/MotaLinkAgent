/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_CAMERA_URL?: string
}

declare module '*.vue' {
  import type { DefineComponent } from 'vue'
  const component: DefineComponent<object, object, unknown>
  export default component
}

declare module '@novnc/novnc/lib/rfb.js' {
  const RFB: new (target: HTMLElement, url: string, options?: Record<string, unknown>) => {
    scaleViewport: boolean
    resizeSession: boolean
    background: string
    disconnect: () => void
    sendCredentials: (c: { password?: string }) => void
    addEventListener: (type: string, fn: (ev: Event) => void) => void
  }
  export default RFB
}
