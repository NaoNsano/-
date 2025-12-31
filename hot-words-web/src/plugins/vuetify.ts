import 'vuetify/styles'
import '@mdi/font/css/materialdesignicons.css'
import { createVuetify, type ThemeDefinition } from 'vuetify'
import * as components from 'vuetify/components'
import * as directives from 'vuetify/directives'

const lightGoogleTheme: ThemeDefinition = {
  dark: false,
  colors: {
    background: '#F5F7FA', // 极淡的灰蓝色背景，护眼且现代
    surface: '#FFFFFF',
    primary: '#1976D2',    // Google Blue
    secondary: '#424242',
    error: '#D32F2F',
    info: '#0288D1',
    success: '#2E7D32',
    warning: '#ED6C02',
  },
}

export default createVuetify({
  components,
  directives,
  theme: {
    defaultTheme: 'lightGoogleTheme',
    themes: {
      lightGoogleTheme,
    },
  },
})