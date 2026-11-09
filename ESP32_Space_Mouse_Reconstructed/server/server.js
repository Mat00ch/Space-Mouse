// Reconstructed server for the ESP32 Space Mouse.
const express = require('express');
const http = require('http');
const { WebSocketServer, WebSocket } = require('ws');
const path = require('path');

const app = express();
app.use(express.static(path.join(__dirname, 'public')));
const server = http.createServer(app);
const wss = new WebSocketServer({ server });
let latest = 'Waiting for ESP32 telemetry...';

wss.on('connection', (socket) => {
  socket.send(latest);
  socket.on('message', (raw) => {
    const message = raw.toString();
    if (message === 'RECALIBRATE') {
      for (const client of wss.clients) {
        if (client.readyState === WebSocket.OPEN) client.send('RECALIBRATE');
      }
      return;
    }
    if (message.startsWith('X:') && message.includes('Mode:')) latest = message;
    for (const client of wss.clients) {
      if (client !== socket && client.readyState === WebSocket.OPEN) client.send(message);
    }
  });
});

const PORT = Number(process.env.PORT || 3000);
server.listen(PORT, '0.0.0.0', () => {
  console.log(`Space Mouse dashboard: http://localhost:${PORT}`);
});
