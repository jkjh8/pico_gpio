<!-- @format -->

<template>
  <div class="licenses-page">
    <div class="header">
      <h1>📜 Open Source Licenses</h1>
      <p class="subtitle">
        This project uses the following open source software components
      </p>
    </div>

    <!-- Project Info -->
    <section class="project-info">
      <div class="info-card">
        <h2>{{ licenseData.project.name }}</h2>
        <div class="info-grid">
          <div class="info-item">
            <span class="label">Version:</span>
            <span class="value">{{ licenseData.project.version }}</span>
          </div>
          <div class="info-item">
            <span class="label">Author:</span>
            <span class="value">{{ licenseData.project.author }}</span>
          </div>
          <div class="info-item">
            <span class="label">Repository:</span>
            <a
              :href="licenseData.project.repository"
              target="_blank"
              class="value link">
              {{ licenseData.project.repository }}
            </a>
          </div>
        </div>
        <p class="description">{{ licenseData.project.description }}</p>
      </div>
    </section>

    <!-- Legal Notice -->
    <section class="legal-notice">
      <div class="notice-box">
        <div class="icon">⚖️</div>
        <div class="content">
          <h3>Legal Notice</h3>
          <p>{{ licenseData.legal_notice }}</p>
          <p class="update-info">
            Last Updated: {{ formatDate(licenseData.last_updated) }}
          </p>
        </div>
      </div>
    </section>

    <!-- Open Source Components -->
    <section class="components">
      <h2>Open Source Components</h2>

      <div
        v-for="(component, index) in licenseData.open_source_licenses"
        :key="index"
        class="component-card">
        <div class="component-header">
          <div class="title-section">
            <h3>{{ component.name }}</h3>
            <span class="version-badge">v{{ component.version }}</span>
            <span
              :class="['license-badge', getLicenseClass(component.license)]">
              {{ component.license }}
            </span>
          </div>
          <button
            @click="toggleLicenseText(index)"
            class="toggle-btn">
            {{ showLicenseText[index] ? 'Hide License' : 'Show License' }}
          </button>
        </div>

        <div class="component-body">
          <p class="description">{{ component.description }}</p>

          <div class="details-grid">
            <div class="detail-item">
              <strong>Author:</strong>
              {{ component.author }}
            </div>
            <div class="detail-item">
              <strong>Usage:</strong>
              {{ component.usage }}
            </div>
            <div class="detail-item">
              <strong>URL:</strong>
              <a
                :href="component.url"
                target="_blank"
                class="link">
                {{ component.url }}
              </a>
            </div>
          </div>

          <div class="copyright">
            <strong>Copyright:</strong>
            <pre>{{ component.copyright }}</pre>
          </div>

          <transition name="slide">
            <div
              v-if="showLicenseText[index]"
              class="license-text">
              <h4>License Text</h4>
              <pre>{{ component.license_text }}</pre>
            </div>
          </transition>
        </div>
      </div>
    </section>

    <!-- Summary -->
    <section class="summary">
      <div class="summary-card">
        <h3>📊 Summary</h3>
        <div class="stats">
          <div class="stat">
            <div class="number">
              {{ licenseData.open_source_licenses.length }}
            </div>
            <div class="label">Open Source Components</div>
          </div>
          <div class="stat">
            <div class="number">{{ uniqueLicenses.length }}</div>
            <div class="label">Different Licenses</div>
          </div>
          <div class="stat">
            <div class="icon-large">✅</div>
            <div class="label">Fully Compliant</div>
          </div>
        </div>

        <div class="license-types">
          <h4>License Types Used:</h4>
          <div class="badges">
            <span
              v-for="license in uniqueLicenses"
              :key="license"
              :class="['license-badge', getLicenseClass(license)]">
              {{ license }}
            </span>
          </div>
        </div>
      </div>
    </section>

    <!-- Download Section -->
    <section class="actions">
      <button
        @click="downloadLicenses"
        class="btn-download">
        📥 Download All Licenses (TXT)
      </button>
      <button
        @click="copyToClipboard"
        class="btn-copy">
        📋 Copy to Clipboard
      </button>
    </section>
  </div>
</template>

<script>
  import licensesData from './licenses.json'

  export default {
    name: 'LicensesPage',
    data() {
      return {
        licenseData: licensesData,
        showLicenseText: {}
      }
    },
    computed: {
      uniqueLicenses() {
        const licenses = this.licenseData.open_source_licenses.map(
          (c) => c.license
        )
        return [...new Set(licenses)]
      }
    },
    methods: {
      formatDate(dateString) {
        const date = new Date(dateString)
        return date.toLocaleDateString('ko-KR', {
          year: 'numeric',
          month: 'long',
          day: 'numeric'
        })
      },

      getLicenseClass(license) {
        const classMap = {
          MIT: 'license-mit',
          'BSD-3-Clause': 'license-bsd',
          'Apache-2.0': 'license-apache',
          GPL: 'license-gpl'
        }
        return classMap[license] || 'license-default'
      },

      toggleLicenseText(index) {
        this.$set(this.showLicenseText, index, !this.showLicenseText[index])
      },

      downloadLicenses() {
        let content = `${this.licenseData.project.name}\n`
        content += `Open Source License Notices\n`
        content += `Generated: ${new Date().toLocaleString()}\n`
        content += `${'='.repeat(80)}\n\n`

        this.licenseData.open_source_licenses.forEach((component) => {
          content += `\n${'='.repeat(80)}\n`
          content += `${component.name} (${component.license})\n`
          content += `${'='.repeat(80)}\n\n`
          content += `${component.copyright}\n\n`
          content += `${component.license_text}\n\n`
        })

        const blob = new Blob([content], { type: 'text/plain' })
        const url = window.URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = 'OPEN_SOURCE_LICENSES.txt'
        document.body.appendChild(a)
        a.click()
        document.body.removeChild(a)
        window.URL.revokeObjectURL(url)
      },

      async copyToClipboard() {
        let content = ''
        this.licenseData.open_source_licenses.forEach((component) => {
          content += `${component.name}\n`
          content += `${component.copyright}\n`
          content += `License: ${component.license}\n\n`
        })

        try {
          await navigator.clipboard.writeText(content)
          alert('License information copied to clipboard!')
        } catch (err) {
          console.error('Failed to copy:', err)
        }
      }
    }
  }
</script>

<style scoped>
  .licenses-page {
    max-width: 1200px;
    margin: 0 auto;
    padding: 20px;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto,
      'Helvetica Neue', Arial, sans-serif;
  }

  /* Header */
  .header {
    text-align: center;
    margin-bottom: 40px;
    padding: 40px 20px;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    border-radius: 15px;
  }

  .header h1 {
    margin: 0 0 10px 0;
    font-size: 2.5em;
  }

  .subtitle {
    font-size: 1.2em;
    opacity: 0.9;
    margin: 0;
  }

  /* Project Info */
  .project-info {
    margin-bottom: 40px;
  }

  .info-card {
    background: white;
    border: 1px solid #e0e0e0;
    border-radius: 10px;
    padding: 30px;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
  }

  .info-card h2 {
    color: #2c3e50;
    margin-top: 0;
    margin-bottom: 20px;
  }

  .info-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 15px;
    margin-bottom: 20px;
  }

  .info-item {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .info-item .label {
    font-weight: bold;
    color: #666;
    min-width: 80px;
  }

  .info-item .value {
    color: #2c3e50;
  }

  .link {
    color: #667eea;
    text-decoration: none;
  }

  .link:hover {
    text-decoration: underline;
  }

  .description {
    color: #666;
    line-height: 1.6;
    margin: 0;
  }

  /* Legal Notice */
  .legal-notice {
    margin-bottom: 40px;
  }

  .notice-box {
    display: flex;
    gap: 20px;
    background: #fff3cd;
    border: 2px solid #ffc107;
    border-radius: 10px;
    padding: 25px;
  }

  .notice-box .icon {
    font-size: 3em;
    flex-shrink: 0;
  }

  .notice-box .content h3 {
    margin-top: 0;
    color: #856404;
  }

  .notice-box .content p {
    color: #856404;
    line-height: 1.6;
    margin: 10px 0;
  }

  .update-info {
    font-size: 0.9em;
    font-style: italic;
  }

  /* Components */
  .components h2 {
    color: #2c3e50;
    margin-bottom: 25px;
  }

  .component-card {
    background: white;
    border: 1px solid #e0e0e0;
    border-radius: 10px;
    margin-bottom: 25px;
    overflow: hidden;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.05);
    transition: box-shadow 0.3s ease;
  }

  .component-card:hover {
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.1);
  }

  .component-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 20px 25px;
    background: #f8f9fa;
    border-bottom: 1px solid #e0e0e0;
  }

  .title-section {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
  }

  .title-section h3 {
    margin: 0;
    color: #2c3e50;
    font-size: 1.3em;
  }

  .version-badge {
    padding: 4px 12px;
    background: #6c757d;
    color: white;
    border-radius: 15px;
    font-size: 0.85em;
    font-weight: 600;
  }

  .license-badge {
    padding: 4px 12px;
    color: white;
    border-radius: 15px;
    font-size: 0.85em;
    font-weight: 600;
  }

  .license-mit {
    background: #28a745;
  }

  .license-bsd {
    background: #17a2b8;
  }

  .license-apache {
    background: #dc3545;
  }

  .license-gpl {
    background: #ffc107;
    color: #333;
  }

  .license-default {
    background: #6c757d;
  }

  .toggle-btn {
    padding: 8px 16px;
    background: #667eea;
    color: white;
    border: none;
    border-radius: 5px;
    cursor: pointer;
    font-size: 0.9em;
    transition: background 0.3s ease;
  }

  .toggle-btn:hover {
    background: #5568d3;
  }

  .component-body {
    padding: 25px;
  }

  .component-body .description {
    margin-bottom: 20px;
    font-size: 1.05em;
  }

  .details-grid {
    display: grid;
    gap: 10px;
    margin-bottom: 20px;
  }

  .detail-item {
    color: #555;
    line-height: 1.6;
  }

  .detail-item strong {
    color: #2c3e50;
    min-width: 80px;
    display: inline-block;
  }

  .copyright {
    background: #f8f9fa;
    padding: 15px;
    border-radius: 5px;
    border-left: 4px solid #667eea;
    margin-bottom: 20px;
  }

  .copyright strong {
    color: #2c3e50;
  }

  .copyright pre {
    margin: 10px 0 0 0;
    white-space: pre-wrap;
    font-family: inherit;
    color: #555;
  }

  .license-text {
    background: #f8f9fa;
    padding: 20px;
    border-radius: 5px;
    border-left: 4px solid #28a745;
    margin-top: 20px;
  }

  .license-text h4 {
    margin-top: 0;
    color: #2c3e50;
  }

  .license-text pre {
    white-space: pre-wrap;
    word-wrap: break-word;
    line-height: 1.6;
    font-family: 'Courier New', monospace;
    font-size: 0.9em;
    color: #333;
    margin: 10px 0 0 0;
  }

  /* Slide Transition */
  .slide-enter-active,
  .slide-leave-active {
    transition: all 0.3s ease;
  }

  .slide-enter,
  .slide-leave-to {
    opacity: 0;
    transform: translateY(-10px);
  }

  /* Summary */
  .summary {
    margin-bottom: 40px;
  }

  .summary-card {
    background: white;
    border: 1px solid #e0e0e0;
    border-radius: 10px;
    padding: 30px;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
  }

  .summary-card h3 {
    color: #2c3e50;
    margin-top: 0;
    margin-bottom: 25px;
  }

  .stats {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 20px;
    margin-bottom: 30px;
  }

  .stat {
    text-align: center;
    padding: 20px;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    border-radius: 10px;
  }

  .stat .number {
    font-size: 3em;
    font-weight: bold;
    margin-bottom: 10px;
  }

  .stat .icon-large {
    font-size: 4em;
    margin-bottom: 10px;
  }

  .stat .label {
    font-size: 1.1em;
    opacity: 0.9;
  }

  .license-types h4 {
    color: #2c3e50;
    margin-bottom: 15px;
  }

  .badges {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
  }

  /* Actions */
  .actions {
    display: flex;
    gap: 15px;
    justify-content: center;
    margin-bottom: 40px;
  }

  .btn-download,
  .btn-copy {
    padding: 15px 30px;
    font-size: 1.1em;
    font-weight: 600;
    border: none;
    border-radius: 8px;
    cursor: pointer;
    transition: all 0.3s ease;
  }

  .btn-download {
    background: #28a745;
    color: white;
  }

  .btn-download:hover {
    background: #218838;
    transform: translateY(-2px);
    box-shadow: 0 4px 8px rgba(40, 167, 69, 0.3);
  }

  .btn-copy {
    background: #17a2b8;
    color: white;
  }

  .btn-copy:hover {
    background: #138496;
    transform: translateY(-2px);
    box-shadow: 0 4px 8px rgba(23, 162, 184, 0.3);
  }

  /* Responsive */
  @media (max-width: 768px) {
    .header h1 {
      font-size: 2em;
    }

    .component-header {
      flex-direction: column;
      align-items: flex-start;
      gap: 15px;
    }

    .actions {
      flex-direction: column;
    }

    .stats {
      grid-template-columns: 1fr;
    }
  }
</style>
