// 展示代码结构：
//   · 初始化 i18n locale → 挂载根组件 App.vue
//
//--------//
// 模块：应用入口 — createApp、全局样式
import { createApp } from 'vue'
import './style.css'
import App from './App.vue'
import { initLocale, setLocale, locale } from './i18n'

initLocale()
setLocale(locale.value)

createApp(App).mount('#app')
