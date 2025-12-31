/** @format */

// --- State & Utilities ---
const state = {
  network: {},
  comm: {},
  gpio: {}
}

function showLoading(msg = 'Loading...') {
  document.getElementById('loading-message').textContent = msg
  document.getElementById('loading-overlay').classList.remove('hidden')
}

function hideLoading() {
  document.getElementById('loading-overlay').classList.add('hidden')
}

function showToast(message, type = 'positive') {
  const container = document.getElementById('toast-container')
  const toast = document.createElement('div')
  toast.className = `toast ${type}`
  // Icon based on type
  const iconId = type === 'positive' ? 'check_circle' : 'cancel'
  toast.innerHTML = `
          <svg class="icon-sm" style="fill:white"><use href="icon.svg#${iconId}"></use></svg>
          <span>${message}</span>
      `
  container.appendChild(toast)
  setTimeout(() => toast.remove(), 3000)
}

// --- Navigation ---
const sidebar = document.getElementById('sidebar')
const mainContent = document.getElementById('main-content')
const menuToggle = document.getElementById('menu-toggle')
const navItems = document.querySelectorAll('.nav-item')
const pages = document.querySelectorAll('.page')

// Sidebar Toggle Removed (Fixed Sidebar)

navItems.forEach((item) => {
  item.addEventListener('click', () => {
    // Update Nav
    navItems.forEach((n) => n.classList.remove('active'))
    item.classList.add('active')

    // Show Page
    const targetId = item.dataset.target
    pages.forEach((p) => p.classList.add('hidden'))
    document.getElementById(targetId).classList.remove('hidden')
  })
})

// --- API Calls ---
async function fetchNetwork() {
  try {
    const res = await fetch('/api/network')
    const data = await res.json()
    state.network = data.network || {}

    // Update UI
    document.getElementById('header-mac').textContent =
      state.network.mac || 'N/A'
    document.getElementById('net-dhcp').checked = state.network.dhcp_enabled
    document.getElementById('net-ip').value = state.network.ip || ''
    document.getElementById('net-mask').value = state.network.subnet || ''
    document.getElementById('net-gateway').value = state.network.gateway || ''
    document.getElementById('net-dns').value = state.network.dns || ''

    // Update Summary
    document.getElementById('sum-ip').textContent = state.network.ip || 'N/A'
    document.getElementById('sum-mac').textContent = state.network.mac || 'N/A'
    document.getElementById('sum-dhcp').textContent = state.network.dhcp_enabled
      ? 'Enabled'
      : 'Disabled'
    document.getElementById('sum-mask').textContent =
      state.network.subnet || 'N/A'
    document.getElementById('sum-gateway').textContent =
      state.network.gateway || 'N/A'
    document.getElementById('sum-dns').textContent = state.network.dns || 'N/A'

    toggleNetworkInputs(state.network.dhcp_enabled)
  } catch (e) {
    console.error(e)
    showToast('Failed to fetch network status', 'negative')
  }
}

async function fetchComm() {
  try {
    const res = await fetch('/api/control')
    const data = await res.json()
    state.comm = data

    document.getElementById('comm-tcp').value = data.tcp_port
    document.getElementById('comm-baud').value = data.rs232_1_baud

    // Update Summary
    document.getElementById('sum-tcp').textContent = data.tcp_port
    document.getElementById('sum-baud').textContent = data.rs232_1_baud
  } catch (e) {
    console.error(e)
  }
}

async function fetchGpio() {
  try {
    const res = await fetch('/api/gpio')
    const data = await res.json()
    state.gpio = data

    document.getElementById('gpio-id').value = data.device_id
    document.getElementById('gpio-mode').value = data.comm_mode
    document.getElementById('gpio-auto').value = data.auto_response.toString()

    // Update Summary
    document.getElementById('sum-id').textContent = data.device_id
    document.getElementById('sum-mode').textContent =
      data.comm_mode.toUpperCase()
  } catch (e) {
    console.error(e)
  }
}

// --- Form Handlers ---

// Network Form
const dhcpToggle = document.getElementById('net-dhcp')
const netInputs = ['net-ip', 'net-mask', 'net-gateway', 'net-dns']

function toggleNetworkInputs(disabled) {
  netInputs.forEach((id) => {
    document.getElementById(id).disabled = disabled
  })
}

dhcpToggle.addEventListener('change', (e) => {
  toggleNetworkInputs(e.target.checked)
})

document
  .getElementById('network-form')
  .addEventListener('submit', async (e) => {
    e.preventDefault()
    showLoading('Setting up network...')

    const payload = {
      dhcp_enabled: dhcpToggle.checked,
      ip: document.getElementById('net-ip').value,
      subnet: document.getElementById('net-mask').value,
      gateway: document.getElementById('net-gateway').value,
      dns: document.getElementById('net-dns').value,
      mac: state.network.mac // preserve mac
    }

    try {
      const res = await fetch('/api/network', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      })
      if (!res.ok) throw new Error('Failed')

      showToast('Network settings updated! Reloading in 10s...', 'positive')
      setTimeout(() => window.location.reload(), 5000)
    } catch (err) {
      showToast('Failed to update network settings', 'negative')
    } finally {
      hideLoading()
    }
  })

// Reboot
document.getElementById('btn-reboot').addEventListener('click', async () => {
  if (!confirm('Are you sure you want to reboot the system?')) return

  showLoading('Rebooting system...')
  try {
    const res = await fetch('/api/restart')
    if (!res.ok) throw new Error('Failed')
    showToast('System is rebooting...', 'positive')
    setTimeout(() => window.location.reload(), 10000)
  } catch (err) {
    showToast('Failed to reboot', 'negative')
    hideLoading()
  }
})

// Comm Form
document.getElementById('comm-form').addEventListener('submit', async (e) => {
  e.preventDefault()
  showLoading('Applying Control Settings...')

  const payload = {
    tcp_port: parseInt(document.getElementById('comm-tcp').value),
    rs232_1_baud: parseInt(document.getElementById('comm-baud').value)
  }

  try {
    const res = await fetch('/api/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
    if (!res.ok) throw new Error('Failed')

    showToast('Control settings updated! Reloading...', 'positive')
    setTimeout(() => window.location.reload(), 5000)
  } catch (err) {
    showToast('Failed to update control settings', 'negative')
  } finally {
    hideLoading()
  }
})

// GPIO Form
document.getElementById('gpio-form').addEventListener('submit', async (e) => {
  e.preventDefault()
  showLoading('Applying GPIO Settings...')

  const payload = {
    device_id: parseInt(document.getElementById('gpio-id').value),
    rt_mode: document.getElementById('gpio-mode').value,
    auto_response: document.getElementById('gpio-auto').value === 'true'
  }

  try {
    const res = await fetch('/api/gpio', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
    if (!res.ok) throw new Error('Failed')

    showToast('GPIO settings updated!', 'positive')
  } catch (err) {
    showToast('Failed to update GPIO settings', 'negative')
  } finally {
    hideLoading()
  }
})

// --- Initialization ---
window.addEventListener('DOMContentLoaded', async () => {
  showLoading('Fetching status...')
  await Promise.all([fetchNetwork(), fetchComm(), fetchGpio()])
  hideLoading()
})
