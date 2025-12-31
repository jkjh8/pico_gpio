/** @format */

const http = require('http')
const fs = require('fs')
const path = require('path')

const PORT = 3000
const MIME_TYPES = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'text/javascript',
  '.svg': 'image/svg+xml',
  '.json': 'application/json'
}

// Mock Data
let mockData = {
  network: {
    network: {
      mac: '00:11:22:33:44:55',
      dhcp_enabled: true,
      ip: '192.168.1.100',
      subnet: '255.255.255.0',
      gateway: '192.168.1.1',
      dns: '8.8.8.8'
    }
  },
  control: {
    tcp_port: 5050,
    rs232_1_baud: 9600
  },
  gpio: {
    device_id: 1,
    rt_mode: 'text',
    auto_response: true
  }
}

const server = http.createServer((req, res) => {
  console.log(`${req.method} ${req.url}`)

  // API Routes
  if (req.url.startsWith('/api/')) {
    res.setHeader('Content-Type', 'application/json')

    if (req.url === '/api/network') {
      if (req.method === 'GET') {
        res.end(JSON.stringify(mockData.network))
      } else if (req.method === 'POST') {
        let body = ''
        req.on('data', (chunk) => (body += chunk))
        req.on('end', () => {
          const updates = JSON.parse(body)
          mockData.network.network = { ...mockData.network.network, ...updates }
          res.end(JSON.stringify({ status: 'ok', message: 'Network updated' }))
        })
      }
      return
    }

    if (req.url === '/api/control') {
      if (req.method === 'GET') {
        res.end(JSON.stringify(mockData.control))
      } else if (req.method === 'POST') {
        let body = ''
        req.on('data', (chunk) => (body += chunk))
        req.on('end', () => {
          mockData.control = { ...mockData.control, ...JSON.parse(body) }
          res.end(JSON.stringify({ status: 'ok', message: 'Control updated' }))
        })
      }
      return
    }

    if (req.url === '/api/gpio') {
      if (req.method === 'GET') {
        res.end(JSON.stringify(mockData.gpio))
      } else if (req.method === 'POST') {
        let body = ''
        req.on('data', (chunk) => (body += chunk))
        req.on('end', () => {
          mockData.gpio = { ...mockData.gpio, ...JSON.parse(body) }
          res.end(JSON.stringify({ status: 'ok', message: 'GPIO updated' }))
        })
      }
      return
    }

    if (req.url === '/api/restart') {
      res.end(JSON.stringify({ status: 'ok', message: 'Restarting...' }))
      return
    }

    res.statusCode = 404
    res.end(JSON.stringify({ error: 'Not found' }))
    return
  }

  // Static Files
  let filePath = '.' + req.url
  if (filePath === './') filePath = './index.html'

  const extname = path.extname(filePath)
  const contentType = MIME_TYPES[extname] || 'application/octet-stream'

  fs.readFile(filePath, (error, content) => {
    if (error) {
      if (error.code == 'ENOENT') {
        res.writeHead(404)
        res.end('File not found')
      } else {
        res.writeHead(500)
        res.end('Server Error: ' + error.code)
      }
    } else {
      res.writeHead(200, { 'Content-Type': contentType })
      res.end(content, 'utf-8')
    }
  })
})

server.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}/`)
})
