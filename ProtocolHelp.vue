<!-- @format -->

<template>
  <div class="protocol-help">
    <h1>{{ helpData.device_info.name }}</h1>

    <!-- Device Info -->
    <section class="device-info">
      <h2>Device Information</h2>
      <div class="info-grid">
        <div
          v-for="(value, key) in helpData.device_info.hardware"
          :key="key"
          class="info-item">
          <strong>{{ formatKey(key) }}:</strong>
          {{ value }}
        </div>
      </div>
      <div class="features">
        <h3>Features</h3>
        <ul>
          <li
            v-for="(feature, index) in helpData.device_info.features"
            :key="index">
            {{ feature }}
          </li>
        </ul>
      </div>
    </section>

    <!-- Device Addressing -->
    <section class="device-addressing">
      <h2>{{ helpData.device_addressing.title }}</h2>
      <p>{{ helpData.device_addressing.description }}</p>
      <div class="id-types">
        <div
          v-for="idType in helpData.device_addressing.id_types"
          :key="idType.id"
          class="id-card">
          <h4>ID {{ idType.id }}: {{ idType.name }}</h4>
          <p>{{ idType.description }}</p>
        </div>
      </div>
    </section>

    <!-- Protocol Tabs -->
    <section class="protocols">
      <div class="protocol-tabs">
        <button
          v-for="protocol in ['text_mode', 'json_mode']"
          :key="protocol"
          :class="{ active: activeProtocol === protocol }"
          @click="activeProtocol = protocol">
          {{ helpData.protocols[protocol].name }}
        </button>
      </div>

      <div class="protocol-content">
        <div class="protocol-header">
          <h2>{{ helpData.protocols[activeProtocol].name }}</h2>
          <p>{{ helpData.protocols[activeProtocol].description }}</p>
          <div class="format-info">
            <strong>Format:</strong>
            <code>{{ helpData.protocols[activeProtocol].format }}</code>
          </div>
          <div
            v-if="helpData.protocols[activeProtocol].default"
            class="badge default">
            Default Mode
          </div>
        </div>

        <!-- Command Categories -->
        <div class="command-categories">
          <div
            v-for="(commands, category) in helpData.protocols[activeProtocol]
              .commands"
            :key="category"
            class="category">
            <h3>{{ formatCategory(category) }}</h3>

            <!-- Commands -->
            <template v-if="category !== 'events'">
              <div
                v-for="command in commands"
                :key="command.command"
                class="command-card">
                <div class="command-header">
                  <h4>{{ command.command }}</h4>
                  <span class="command-type">{{ category }}</span>
                </div>

                <p class="description">{{ command.description }}</p>

                <!-- Parameters (TEXT mode) -->
                <div
                  v-if="command.parameters && command.parameters.length > 0"
                  class="parameters">
                  <strong>Parameters:</strong>
                  <ul>
                    <li
                      v-for="(param, idx) in command.parameters"
                      :key="idx">
                      <code>{{ param }}</code>
                    </li>
                  </ul>
                </div>

                <!-- Request/Response (JSON mode) -->
                <div
                  v-if="command.request"
                  class="json-structure">
                  <div class="request">
                    <strong>Request:</strong>
                    <pre>{{ formatJSON(command.request) }}</pre>
                  </div>
                  <div class="response">
                    <strong>Response:</strong>
                    <pre>{{ formatJSON(command.response) }}</pre>
                  </div>
                </div>

                <!-- Response format (TEXT mode) -->
                <div
                  v-if="
                    command.response && typeof command.response === 'string'
                  "
                  class="text-response">
                  <strong>Response:</strong>
                  <code>{{ command.response }}</code>
                </div>

                <!-- Example -->
                <div class="example">
                  <strong>Example:</strong>
                  <div class="example-content">
                    <div class="request-example">
                      <span class="label">Request:</span>
                      <code>{{ command.example.request }}</code>
                    </div>
                    <div class="response-example">
                      <span class="label">Response:</span>
                      <code>{{ command.example.response }}</code>
                    </div>
                  </div>
                </div>

                <!-- Note -->
                <div
                  v-if="command.note"
                  class="note">
                  <strong>⚠️ Note:</strong>
                  {{ command.note }}
                </div>

                <!-- Defaults (for factory reset) -->
                <div
                  v-if="command.defaults"
                  class="defaults">
                  <strong>Factory Defaults:</strong>
                  <ul>
                    <li
                      v-for="(value, key) in command.defaults"
                      :key="key">
                      <strong>{{ formatKey(key) }}:</strong>
                      {{ value }}
                    </li>
                  </ul>
                </div>
              </div>
            </template>

            <!-- Events -->
            <template v-if="category === 'events'">
              <div
                v-for="event in commands"
                :key="event.event"
                class="event-card">
                <div class="event-header">
                  <h4>{{ event.event }}</h4>
                  <span class="event-badge">Event</span>
                </div>

                <p class="description">{{ event.description }}</p>

                <div class="event-format">
                  <strong>Format:</strong>
                  <pre v-if="typeof event.format === 'object'">{{
                    formatJSON(event.format)
                  }}</pre>
                  <code v-else>{{ event.format }}</code>
                </div>

                <div class="example">
                  <strong>Example:</strong>
                  <code>{{ event.example }}</code>
                </div>

                <div
                  v-if="event.note"
                  class="note">
                  <strong>ℹ️ Note:</strong>
                  {{ event.note }}
                </div>
              </div>
            </template>
          </div>
        </div>
      </div>
    </section>

    <!-- Quick Start -->
    <section class="quick-start">
      <h2>{{ helpData.quick_start.title }}</h2>
      <div class="steps">
        <div
          v-for="step in helpData.quick_start.steps"
          :key="step.step"
          class="step-card">
          <div class="step-number">{{ step.step }}</div>
          <div class="step-content">
            <h3>{{ step.title }}</h3>
            <p>{{ step.description }}</p>
          </div>
        </div>
      </div>
    </section>

    <!-- Tips -->
    <section class="tips">
      <h2>{{ helpData.tips.title }}</h2>
      <div class="tips-grid">
        <div
          v-for="(tip, index) in helpData.tips.items"
          :key="index"
          class="tip-card">
          <h4>{{ tip.title }}</h4>
          <p>{{ tip.description }}</p>
        </div>
      </div>
    </section>
  </div>
</template>

<script>
  import protocolHelpData from './protocol_help.json'

  export default {
    name: 'ProtocolHelp',
    data() {
      return {
        helpData: protocolHelpData,
        activeProtocol: 'text_mode'
      }
    },
    methods: {
      formatKey(key) {
        return key.replace(/_/g, ' ').replace(/\b\w/g, (l) => l.toUpperCase())
      },
      formatCategory(category) {
        const categoryNames = {
          network: '🌐 Network Commands',
          tcp: '🔌 TCP Commands',
          uart: '📡 UART Commands',
          gpio: '⚡ GPIO Commands',
          mode: '🔄 Mode Commands',
          system: '⚙️ System Commands',
          events: '📢 Auto Events'
        }
        return categoryNames[category] || category
      },
      formatJSON(obj) {
        return JSON.stringify(obj, null, 2)
      }
    }
  }
</script>

<style scoped>
  .protocol-help {
    max-width: 1200px;
    margin: 0 auto;
    padding: 20px;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto,
      'Helvetica Neue', Arial, sans-serif;
  }

  h1 {
    color: #2c3e50;
    border-bottom: 3px solid #42b983;
    padding-bottom: 10px;
    margin-bottom: 30px;
  }

  h2 {
    color: #34495e;
    margin-top: 40px;
    margin-bottom: 20px;
  }

  h3 {
    color: #42b983;
    margin-top: 20px;
    margin-bottom: 15px;
  }

  section {
    margin-bottom: 50px;
  }

  /* Device Info */
  .device-info .info-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 15px;
    margin-bottom: 20px;
  }

  .info-item {
    padding: 15px;
    background: #f8f9fa;
    border-radius: 8px;
    border-left: 4px solid #42b983;
  }

  .features ul {
    list-style: none;
    padding: 0;
  }

  .features li {
    padding: 8px 0;
    padding-left: 25px;
    position: relative;
  }

  .features li::before {
    content: '✓';
    position: absolute;
    left: 0;
    color: #42b983;
    font-weight: bold;
  }

  /* Device Addressing */
  .id-types {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 20px;
    margin-top: 20px;
  }

  .id-card {
    padding: 20px;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    border-radius: 10px;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  }

  .id-card h4 {
    margin-top: 0;
    color: white;
  }

  /* Protocol Tabs */
  .protocol-tabs {
    display: flex;
    gap: 10px;
    margin-bottom: 20px;
    border-bottom: 2px solid #e0e0e0;
  }

  .protocol-tabs button {
    padding: 12px 24px;
    border: none;
    background: transparent;
    cursor: pointer;
    font-size: 16px;
    font-weight: 500;
    color: #666;
    border-bottom: 3px solid transparent;
    transition: all 0.3s ease;
  }

  .protocol-tabs button:hover {
    color: #42b983;
  }

  .protocol-tabs button.active {
    color: #42b983;
    border-bottom-color: #42b983;
  }

  .protocol-header {
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    padding: 30px;
    border-radius: 10px;
    margin-bottom: 30px;
  }

  .protocol-header h2 {
    margin-top: 0;
    color: white;
  }

  .format-info {
    margin-top: 15px;
    background: rgba(255, 255, 255, 0.2);
    padding: 15px;
    border-radius: 5px;
  }

  .format-info code {
    color: #ffd700;
    font-size: 14px;
  }

  .badge.default {
    display: inline-block;
    margin-top: 15px;
    padding: 5px 15px;
    background: #42b983;
    border-radius: 20px;
    font-size: 12px;
    font-weight: bold;
  }

  /* Commands */
  .category {
    margin-bottom: 40px;
  }

  .command-card,
  .event-card {
    background: white;
    border: 1px solid #e0e0e0;
    border-radius: 10px;
    padding: 25px;
    margin-bottom: 20px;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.05);
    transition: box-shadow 0.3s ease;
  }

  .command-card:hover,
  .event-card:hover {
    box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
  }

  .command-header,
  .event-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 15px;
  }

  .command-header h4,
  .event-header h4 {
    margin: 0;
    color: #2c3e50;
    font-family: 'Courier New', monospace;
    font-size: 18px;
  }

  .command-type,
  .event-badge {
    padding: 4px 12px;
    background: #42b983;
    color: white;
    border-radius: 15px;
    font-size: 12px;
    font-weight: bold;
    text-transform: uppercase;
  }

  .event-badge {
    background: #f39c12;
  }

  .description {
    color: #666;
    margin-bottom: 15px;
  }

  .parameters ul {
    list-style: none;
    padding-left: 0;
  }

  .parameters li {
    padding: 5px 0;
    padding-left: 20px;
    position: relative;
  }

  .parameters li::before {
    content: '›';
    position: absolute;
    left: 0;
    color: #42b983;
    font-weight: bold;
  }

  .json-structure {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
    margin: 20px 0;
  }

  .request,
  .response,
  .text-response,
  .event-format {
    margin: 15px 0;
  }

  pre {
    background: #f5f7fa;
    padding: 15px;
    border-radius: 5px;
    overflow-x: auto;
    border-left: 4px solid #42b983;
    font-size: 13px;
  }

  code {
    background: #f5f7fa;
    padding: 2px 6px;
    border-radius: 3px;
    font-family: 'Courier New', monospace;
    color: #e83e8c;
    font-size: 14px;
  }

  .example {
    margin: 20px 0;
    padding: 15px;
    background: #f8f9fa;
    border-radius: 5px;
    border-left: 4px solid #3498db;
  }

  .example-content {
    margin-top: 10px;
  }

  .request-example,
  .response-example {
    margin: 8px 0;
  }

  .label {
    display: inline-block;
    font-weight: bold;
    color: #555;
    min-width: 80px;
  }

  .note {
    margin-top: 15px;
    padding: 12px;
    background: #fff3cd;
    border-left: 4px solid #ffc107;
    border-radius: 5px;
    color: #856404;
  }

  .defaults {
    margin-top: 15px;
    padding: 15px;
    background: #e7f3ff;
    border-left: 4px solid #2196f3;
    border-radius: 5px;
  }

  .defaults ul {
    list-style: none;
    padding: 0;
    margin-top: 10px;
  }

  .defaults li {
    padding: 5px 0;
  }

  /* Quick Start */
  .steps {
    display: grid;
    gap: 20px;
  }

  .step-card {
    display: flex;
    align-items: flex-start;
    gap: 20px;
    padding: 20px;
    background: white;
    border: 1px solid #e0e0e0;
    border-radius: 10px;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.05);
  }

  .step-number {
    flex-shrink: 0;
    width: 50px;
    height: 50px;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 24px;
    font-weight: bold;
  }

  .step-content h3 {
    margin-top: 0;
    color: #2c3e50;
  }

  .step-content p {
    color: #666;
    margin: 10px 0 0 0;
  }

  /* Tips */
  .tips-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 20px;
  }

  .tip-card {
    padding: 20px;
    background: white;
    border: 1px solid #e0e0e0;
    border-radius: 10px;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.05);
    transition: transform 0.3s ease, box-shadow 0.3s ease;
  }

  .tip-card:hover {
    transform: translateY(-5px);
    box-shadow: 0 4px 8px rgba(0, 0, 0, 0.15);
  }

  .tip-card h4 {
    color: #42b983;
    margin-top: 0;
  }

  .tip-card p {
    color: #666;
    margin-bottom: 0;
  }

  /* Responsive */
  @media (max-width: 768px) {
    .json-structure {
      grid-template-columns: 1fr;
    }

    .protocol-tabs {
      flex-direction: column;
    }

    .id-types,
    .tips-grid {
      grid-template-columns: 1fr;
    }
  }
</style>
