<!-- @format -->

# Protocol Help Documentation

## 📚 Overview

This documentation provides comprehensive protocol information for the Raspberry Pi Pico W5500 GPIO Controller web UI. The help system is built using Vue.js and supports both TEXT and JSON communication modes.

## 📁 Files

- **protocol_help.json** - Complete protocol reference in JSON format
- **ProtocolHelp.vue** - Vue.js component for rendering the help documentation

## 🚀 Quick Start

### 1. Import the Component

```vue
<template>
  <div id="app">
    <ProtocolHelp />
  </div>
</template>

<script>
  import ProtocolHelp from './ProtocolHelp.vue'

  export default {
    components: {
      ProtocolHelp
    }
  }
</script>
```

### 2. Use in Router

```javascript
import ProtocolHelp from './ProtocolHelp.vue'

const routes = [
  {
    path: '/help',
    name: 'Help',
    component: ProtocolHelp
  }
]
```

## 🎨 Features

### Interactive Protocol Tabs

- Switch between TEXT and JSON modes
- Color-coded command categories
- Expandable command details

### Command Categories

- 🌐 Network Commands (IP, DHCP, MAC)
- 🔌 TCP Commands (Port configuration)
- 📡 UART Commands (Baud rate)
- ⚡ GPIO Commands (Input/Output control)
- 🔄 Mode Commands (TEXT/JSON switching)
- ⚙️ System Commands (Device ID, Factory Reset, Reboot)
- 📢 Auto Events (Input change notifications)

### Each Command Shows

- Command name and description
- Required parameters
- Request/Response format
- Example usage
- Important notes
- Factory defaults (where applicable)

## 📖 Using the JSON Data Directly

### Load in Vue Component

```javascript
import protocolData from './protocol_help.json'

export default {
  data() {
    return {
      protocols: protocolData.protocols,
      deviceInfo: protocolData.device_info
    }
  }
}
```

### Access Specific Commands

```javascript
// Get all TEXT mode GPIO commands
const gpioCommands = protocolData.protocols.text_mode.commands.gpio

// Get factory reset defaults
const defaults = gpioCommands.find(
  (cmd) => cmd.command === 'factoryreset'
).defaults

// Get JSON mode network commands
const jsonNetworkCommands = protocolData.protocols.json_mode.commands.network
```

### Filter Commands

```javascript
methods: {
  getCommandsByCategory(mode, category) {
    return this.protocols[mode].commands[category]
  },

  searchCommands(searchTerm) {
    const mode = this.activeProtocol
    const allCategories = this.protocols[mode].commands

    const results = []
    Object.keys(allCategories).forEach(category => {
      allCategories[category].forEach(cmd => {
        if (cmd.command.includes(searchTerm) ||
            cmd.description.includes(searchTerm)) {
          results.push(cmd)
        }
      })
    })
    return results
  }
}
```

## 🎯 Customization

### Modify Styles

The component uses scoped CSS. You can override styles:

```vue
<style>
  /* Change primary color */
  .protocol-help h1 {
    border-bottom-color: #your-color;
  }

  .command-type {
    background: #your-color;
  }

  /* Change card shadows */
  .command-card:hover {
    box-shadow: 0 8px 16px rgba(0, 0, 0, 0.2);
  }
</style>
```

### Add Search Functionality

```vue
<template>
  <div class="search-bar">
    <input
      v-model="searchTerm"
      placeholder="Search commands..."
      @input="filterCommands" />
  </div>
</template>

<script>
  export default {
    data() {
      return {
        searchTerm: '',
        filteredCommands: []
      }
    },
    methods: {
      filterCommands() {
        // Implement search logic
      }
    }
  }
</script>
```

### Add Favorites

```vue
<script>
  export default {
    data() {
      return {
        favorites: []
      }
    },
    methods: {
      toggleFavorite(command) {
        const index = this.favorites.indexOf(command)
        if (index > -1) {
          this.favorites.splice(index, 1)
        } else {
          this.favorites.push(command)
        }
        localStorage.setItem('favorites', JSON.stringify(this.favorites))
      },
      loadFavorites() {
        const saved = localStorage.getItem('favorites')
        if (saved) {
          this.favorites = JSON.parse(saved)
        }
      }
    },
    mounted() {
      this.loadFavorites()
    }
  }
</script>
```

## 🌐 Integration Examples

### Axios API Integration

```javascript
import axios from 'axios'
import protocolData from './protocol_help.json'

export default {
  data() {
    return {
      deviceIP: '192.168.1.100',
      devicePort: 5050
    }
  },
  methods: {
    async sendTextCommand(command) {
      try {
        const response = await axios.post(
          `http://${this.deviceIP}:${this.devicePort}`,
          command,
          { headers: { 'Content-Type': 'text/plain' } }
        )
        return response.data
      } catch (error) {
        console.error('Command failed:', error)
      }
    },

    async sendJsonCommand(commandObj) {
      try {
        const response = await axios.post(
          `http://${this.deviceIP}:${this.devicePort}`,
          commandObj,
          { headers: { 'Content-Type': 'application/json' } }
        )
        return response.data
      } catch (error) {
        console.error('Command failed:', error)
      }
    },

    async executeCommand(command) {
      const mode = this.currentMode // 'text_mode' or 'json_mode'

      if (mode === 'text_mode') {
        return await this.sendTextCommand(command.example.request)
      } else {
        const jsonCmd = JSON.parse(command.example.request)
        return await this.sendJsonCommand(jsonCmd)
      }
    }
  }
}
```

### WebSocket Integration

```javascript
export default {
  data() {
    return {
      ws: null,
      connected: false
    }
  },
  methods: {
    connectWebSocket() {
      this.ws = new WebSocket(`ws://${this.deviceIP}:${this.devicePort}`)

      this.ws.onopen = () => {
        this.connected = true
        console.log('Connected to device')
      }

      this.ws.onmessage = (event) => {
        // Handle auto-feedback events
        if (this.currentMode === 'text_mode') {
          if (event.data.startsWith('inputchanged')) {
            this.handleInputChanged(event.data)
          }
        } else {
          const data = JSON.parse(event.data)
          if (data.event === 'input_changed') {
            this.handleInputChanged(data)
          }
        }
      }

      this.ws.onclose = () => {
        this.connected = false
        console.log('Disconnected from device')
      }
    },

    sendCommand(command) {
      if (this.ws && this.connected) {
        this.ws.send(command)
      }
    }
  },
  mounted() {
    this.connectWebSocket()
  },
  beforeUnmount() {
    if (this.ws) {
      this.ws.close()
    }
  }
}
```

## 📱 Responsive Design

The component is fully responsive and adapts to different screen sizes:

- **Desktop**: Multi-column grid layout
- **Tablet**: Adjusted grid with 2 columns
- **Mobile**: Single column layout with stacked elements

## 🔧 Advanced Usage

### Create Command Builder

```vue
<template>
  <div class="command-builder">
    <h3>Command Builder</h3>

    <select v-model="selectedCategory">
      <option
        v-for="category in categories"
        :key="category">
        {{ category }}
      </option>
    </select>

    <select v-model="selectedCommand">
      <option
        v-for="cmd in availableCommands"
        :key="cmd.command">
        {{ cmd.command }}
      </option>
    </select>

    <div
      v-if="selectedCommand"
      class="parameters">
      <input
        v-for="(param, index) in selectedCommand.parameters"
        :key="index"
        :placeholder="param"
        v-model="paramValues[index]" />
    </div>

    <button @click="buildCommand">Build Command</button>

    <div class="result">
      <code>{{ builtCommand }}</code>
    </div>
  </div>
</template>
```

### Generate API Documentation

```javascript
import protocolData from './protocol_help.json'
import fs from 'fs'

function generateMarkdownDocs() {
  let markdown = '# API Documentation\n\n'

  Object.keys(protocolData.protocols).forEach((protocol) => {
    const protoData = protocolData.protocols[protocol]
    markdown += `## ${protoData.name}\n\n`
    markdown += `${protoData.description}\n\n`

    Object.keys(protoData.commands).forEach((category) => {
      markdown += `### ${category.toUpperCase()}\n\n`

      protoData.commands[category].forEach((cmd) => {
        markdown += `#### ${cmd.command}\n`
        markdown += `${cmd.description}\n\n`
        markdown += `**Example:**\n\`\`\`\n${cmd.example.request}\n\`\`\`\n\n`
      })
    })
  })

  fs.writeFileSync('API_DOCS.md', markdown)
}
```

## 💡 Tips

1. **Load on Demand**: For large applications, consider lazy loading the help component
2. **Offline Support**: Store the JSON in localStorage for offline access
3. **Multi-language**: Add translation keys to the JSON for i18n support
4. **Version Control**: Include version information in the JSON for API versioning
5. **Search Indexing**: Pre-process the JSON to create a search index for faster searches

## 🐛 Troubleshooting

### JSON Import Issues

```javascript
// If import doesn't work, use fetch
async mounted() {
  const response = await fetch('/protocol_help.json')
  this.helpData = await response.json()
}
```

### Styling Conflicts

```vue
<style scoped>
  /* Use scoped styles to avoid conflicts */
</style>
```

### Performance

```javascript
// Use computed properties for filtering
computed: {
  filteredCommands() {
    if (!this.searchTerm) return this.allCommands
    return this.allCommands.filter(cmd =>
      cmd.command.includes(this.searchTerm)
    )
  }
}
```

## 📄 License

This documentation is part of the Raspberry Pi Pico W5500 GPIO Controller project.

## 🤝 Contributing

To add new commands or update documentation:

1. Edit `protocol_help.json` with the new command information
2. Follow the existing structure for consistency
3. Test the component renders correctly
4. Update examples if needed

---

**Last Updated**: 2025-10-19
