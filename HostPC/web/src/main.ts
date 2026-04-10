import { createApp } from 'vue'
import './style.css'
import App from './App.vue'
import { initLocale, setLocale, locale } from './i18n'

initLocale()
setLocale(locale.value)

createApp(App).mount('#app')
