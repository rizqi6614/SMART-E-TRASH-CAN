/* =========================================================
   SMART BIN IoT WEB DASHBOARD JAVASCRIPT
   SUPPORTING 3 BINS, 6 ULTRASONIC SENSORS, 3 SERVOS & MQTT
   ========================================================= */

// Global Dashboard State
const state = {
  serialNumber: new URLSearchParams(window.location.search).get('device') || localStorage.getItem('smartbin_device_id') || '12345678',
  esp32Online: true,
  wifiRssi: -58,
  ledStatus: 'HIJAU (Normal)',
  dfplayerState: 'Standby',
  lastSyncTime: new Date(),
  
  // Bin 1 (Organik): isi (cm), orang (cm), servo ("Tertutup"/"Terbuka"), status ("NORMAL"/"PENUH")
  tong1: { distance: 13, orang: 18, percent: 35, servo: 'Tertutup', status: 'NORMAL' },

  // Bin 2 (Anorganik)
  tong2: { distance: 11, orang: 22, percent: 60, servo: 'Tertutup', status: 'NORMAL' },

  // Bin 3 (B3)
  tong3: { distance: 6, orang: 8, percent: 92, servo: 'Tertutup', status: 'PENUH' },

  // LED Matrix Text
  ledText: 'Ayo Buang Sampah'
};

// DOM Element References
const elements = {
  esp32StatusBadge: document.getElementById('esp32-status-badge'),
  esp32StatusText: document.getElementById('esp32-status-text'),
  systemClock: document.getElementById('system-clock'),
  valRssi: document.getElementById('val-rssi'),
  valLedStatus: document.getElementById('val-led-status'),
  valDfplayer: document.getElementById('val-dfplayer'),
  valLastSync: document.getElementById('val-last-sync'),

  // Tong 1 Elements
  fillTong1: document.getElementById('fill-tong1'),
  percentTong1: document.getElementById('percent-tong1'),
  distTong1: document.getElementById('dist-tong1'),
  orangTong1: document.getElementById('orang-tong1'),
  servoTong1: document.getElementById('servo-tong1'),
  badgeTong1: document.getElementById('badge-tong1'),

  // Tong 2 Elements
  fillTong2: document.getElementById('fill-tong2'),
  percentTong2: document.getElementById('percent-tong2'),
  distTong2: document.getElementById('dist-tong2'),
  orangTong2: document.getElementById('orang-tong2'),
  servoTong2: document.getElementById('servo-tong2'),
  badgeTong2: document.getElementById('badge-tong2'),

  // Tong 3 Elements
  fillTong3: document.getElementById('fill-tong3'),
  percentTong3: document.getElementById('percent-tong3'),
  distTong3: document.getElementById('dist-tong3'),
  orangTong3: document.getElementById('orang-tong3'),
  servoTong3: document.getElementById('servo-tong3'),
  badgeTong3: document.getElementById('badge-tong3'),

  // LED Matrix & Logs
  ledMatrixText: document.getElementById('led-matrix-text'),
  activityLogContainer: document.getElementById('activity-log-container'),

  // Simulator Drawer
  simulatorModal: document.getElementById('simulator-modal'),
  btnOpenSimulator: document.getElementById('btn-open-simulator'),
  btnCloseSimulator: document.getElementById('btn-close-simulator'),
  btnSimReset: document.getElementById('btn-sim-reset'),
  btnSimApply: document.getElementById('btn-sim-apply')
};

// Helper: Convert Distance (cm) to Fill % (Dinamis menyesuaikan tinggi tong)
function distToPercent(dist) {
  const maxDist = dist > 25 ? 40 : 25; // cm (Empty)
  const minDist = 10;                  // cm (Full)
  if (dist >= maxDist) return 0;
  if (dist <= minDist) return 100;
  return Math.round(((maxDist - dist) / (maxDist - minDist)) * 100);
}

// Update UI Components (Debounced using requestAnimationFrame to prevent rendering lag)
let updatePending = false;
function updateDashboardUI() {
  if (!updatePending) {
    updatePending = true;
    requestAnimationFrame(() => {
      renderDashboardUI();
      updatePending = false;
    });
  }
}

function renderDashboardUI() {
  // ESP32 Status Badge
  if (state.esp32Online) {
    elements.esp32StatusBadge.className = 'status-badge online';
    elements.esp32StatusText.textContent = 'ESP32 ONLINE';
  } else {
    elements.esp32StatusBadge.className = 'status-badge offline';
    elements.esp32StatusText.textContent = 'ESP32 OFFLINE';
  }

  // Top KPI Metrics
  elements.valRssi.textContent = `${state.wifiRssi} dBm`;
  
  // LED Status (Green vs Red)
  elements.valLedStatus.textContent = state.ledStatus;
  if (state.ledStatus.includes('MERAH')) {
    elements.valLedStatus.className = 'kpi-value text-red';
  } else {
    elements.valLedStatus.className = 'kpi-value text-green';
  }

  elements.valDfplayer.textContent = state.dfplayerState;
  
  // Calculate Last Sync
  const secondsAgo = Math.floor((new Date() - state.lastSyncTime) / 1000);
  elements.valLastSync.textContent = secondsAgo < 5 ? 'Just Now' : `${secondsAgo}s ago`;

  // --- Tong 1 (Organik) ---
  state.tong1.percent = distToPercent(state.tong1.distance);
  elements.fillTong1.style.height = `${state.tong1.percent}%`;
  elements.percentTong1.textContent = `${state.tong1.percent}%`;
  elements.distTong1.textContent = `${state.tong1.distance} cm`;
  elements.orangTong1.textContent = `${state.tong1.orang} cm`;
  elements.servoTong1.textContent = state.tong1.servo;
  elements.servoTong1.className = (state.tong1.servo && state.tong1.servo.toLowerCase() === 'terbuka') ? 'servo-pill open' : 'servo-pill';

  if (state.tong1.percent >= 80 || state.tong1.status === 'PENUH') {
    elements.badgeTong1.className = 'badge badge-full';
    elements.badgeTong1.textContent = 'PENUH';
  } else {
    elements.badgeTong1.className = 'badge badge-normal';
    elements.badgeTong1.textContent = 'NORMAL';
  }

  // --- Tong 2 (Anorganik) ---
  state.tong2.percent = distToPercent(state.tong2.distance);
  elements.fillTong2.style.height = `${state.tong2.percent}%`;
  elements.percentTong2.textContent = `${state.tong2.percent}%`;
  elements.distTong2.textContent = `${state.tong2.distance} cm`;
  elements.orangTong2.textContent = `${state.tong2.orang} cm`;
  elements.servoTong2.textContent = state.tong2.servo;
  elements.servoTong2.className = (state.tong2.servo && state.tong2.servo.toLowerCase() === 'terbuka') ? 'servo-pill open' : 'servo-pill';

  if (state.tong2.percent >= 80 || state.tong2.status === 'PENUH') {
    elements.badgeTong2.className = 'badge badge-full';
    elements.badgeTong2.textContent = 'PENUH';
  } else {
    elements.badgeTong2.className = 'badge badge-normal';
    elements.badgeTong2.textContent = 'NORMAL';
  }

  // --- Tong 3 (B3) ---
  state.tong3.percent = distToPercent(state.tong3.distance);
  elements.fillTong3.style.height = `${state.tong3.percent}%`;
  elements.percentTong3.textContent = `${state.tong3.percent}%`;
  elements.distTong3.textContent = `${state.tong3.distance} cm`;
  elements.orangTong3.textContent = `${state.tong3.orang} cm`;
  elements.servoTong3.textContent = state.tong3.servo;
  elements.servoTong3.className = (state.tong3.servo && state.tong3.servo.toLowerCase() === 'terbuka') ? 'servo-pill open' : 'servo-pill';

  if (state.tong3.percent >= 80 || state.tong3.status === 'PENUH') {
    elements.badgeTong3.className = 'badge badge-full';
    elements.badgeTong3.textContent = 'PENUH';
  } else {
    elements.badgeTong3.className = 'badge badge-normal';
    elements.badgeTong3.textContent = 'NORMAL';
  }

  // Update Combined System LED based on any bin full
  if (state.tong1.percent >= 80 || state.tong2.percent >= 80 || state.tong3.percent >= 80 ||
      state.tong1.status === 'PENUH' || state.tong2.status === 'PENUH' || state.tong3.status === 'PENUH') {
    state.ledStatus = 'MERAH (Penuh)';
  } else {
    state.ledStatus = 'HIJAU (Normal)';
  }

  // MD_Parola LED Matrix Text
  elements.ledMatrixText.textContent = state.ledText;

  // Tampilkan Device ID/Serial Number aktif di header
  const subtitleEl = document.querySelector('.header-titles p');
  if (subtitleEl) {
    subtitleEl.textContent = `Real-time telemetry & automatic waste management system | Device ID: ${state.serialNumber}`;
  }
}

// Add New Log to Activity Feed
function addActivityLog(message, type = 'success') {
  const now = new Date();
  const timeStr = now.toTimeString().split(' ')[0];

  const logItem = document.createElement('div');
  logItem.className = `log-item ${type}`;
  logItem.innerHTML = `
    <div class="log-content">
      <p class="log-text">${message}</p>
    </div>
    <span class="log-time">${timeStr}</span>
  `;

  elements.activityLogContainer.prepend(logItem);

  if (elements.activityLogContainer.children.length > 10) {
    elements.activityLogContainer.removeChild(elements.activityLogContainer.lastChild);
  }
}

// =========================================================
// REALTIME TELEMETRY CHART (CHART.JS)
// =========================================================
let capacityChart;

function initChart() {
  const ctx = document.getElementById('capacityChart').getContext('2d');
  
  const initialLabels = [];
  const initialDataTong1 = [30, 32, 35, 34, 35, 38, 35];
  const initialDataTong2 = [50, 52, 55, 58, 60, 60, 60];
  const initialDataTong3 = [80, 84, 88, 90, 92, 92, 92];

  const now = new Date();
  for (let i = 6; i >= 0; i--) {
    const t = new Date(now.getTime() - i * 5000);
    initialLabels.push(t.toTimeString().split(' ')[0]);
  }

  capacityChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: initialLabels,
      datasets: [
        {
          label: 'Organik (%)',
          data: initialDataTong1,
          borderColor: '#00e676',
          backgroundColor: 'rgba(0, 230, 118, 0.08)',
          borderWidth: 2.5,
          tension: 0.4,
          fill: true,
          pointRadius: 3,
          pointHoverRadius: 6
        },
        {
          label: 'Anorganik (%)',
          data: initialDataTong2,
          borderColor: '#ffb300',
          backgroundColor: 'rgba(255, 179, 0, 0.05)',
          borderWidth: 2.5,
          tension: 0.4,
          fill: true,
          pointRadius: 3,
          pointHoverRadius: 6
        },
        {
          label: 'B3 (%)',
          data: initialDataTong3,
          borderColor: '#ff5252',
          backgroundColor: 'rgba(255, 82, 82, 0.05)',
          borderWidth: 2.5,
          tension: 0.4,
          fill: true,
          pointRadius: 3,
          pointHoverRadius: 6
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false },
        tooltip: {
          mode: 'index',
          intersect: false,
          backgroundColor: '#0c1a14',
          borderColor: '#1e382e',
          borderWidth: 1,
          titleColor: '#ffffff',
          bodyColor: '#83a396'
        }
      },
      scales: {
        x: {
          grid: { color: 'rgba(20, 42, 34, 0.6)' },
          ticks: { color: '#506e62', font: { size: 10 } }
        },
        y: {
          min: 0,
          max: 100,
          grid: { color: 'rgba(20, 42, 34, 0.6)' },
          ticks: { color: '#506e62', font: { size: 10 }, callback: v => v + '%' }
        }
      }
    }
  });
}

function addChartData() {
  if (!capacityChart) return;
  const timeStr = new Date().toTimeString().split(' ')[0];

  capacityChart.data.labels.push(timeStr);
  capacityChart.data.datasets[0].data.push(state.tong1.percent);
  capacityChart.data.datasets[1].data.push(state.tong2.percent);
  capacityChart.data.datasets[2].data.push(state.tong3.percent);

  if (capacityChart.data.labels.length > 10) {
    capacityChart.data.labels.shift();
    capacityChart.data.datasets.forEach(ds => ds.data.shift());
  }

  capacityChart.update('none');
}

// =========================================================
// MQTT WEBSOCKET CONNECTOR & TOPIC HANDLERS
// =========================================================
let mqttClientInstance = null;

function sendServoCommand(tongNum, action) {
  if (!mqttClientInstance || !mqttClientInstance.connected) {
    addActivityLog(`Gagal: Belum terhubung ke broker MQTT.`, 'danger');
    return;
  }
  const cmdTopic = `smartbin/${state.serialNumber}/cmd/servo${tongNum}`;
  mqttClientInstance.publish(cmdTopic, action);
  addActivityLog(`Perintah dikirim: Tong ${tongNum} -> ${action}`, 'warning');
}

function setupMQTT() {
  const brokerUrl = 'wss://smartnose:sPMWfLIuQ2JT72FV@smartnose.cloud.shiftr.io';
  console.log('[MQTT] Connecting to shiftr.io broker:', brokerUrl);

  try {
    const client = mqtt.connect(brokerUrl, {
      clientId: 'smartbin-webui-' + Math.random().toString(16).substring(2, 8),
      clean: true,
      connectTimeout: 4000
    });
    mqttClientInstance = client;

    client.on('connect', () => {
      console.log('[MQTT] Connected to shiftr.io Broker!');
      state.esp32Online = true;
      updateDashboardUI();

      // Subscribe ke topic dinamis berdasarkan serial number
      const topicFilter = `smartbin/${state.serialNumber}/#`;
      console.log(`[MQTT] Subscribing to: ${topicFilter}`);
      client.subscribe(topicFilter);
    });

    client.on('message', (topic, message) => {
      const payload = message.toString();
      console.log(`[MQTT RX] ${topic} -> ${payload}`);

      const parts = topic.split('/');
      // Pastikan topic memiliki format: smartbin/<serial_number>/<subtopic>
      if (parts[0] !== 'smartbin' || parts[1] !== state.serialNumber) {
        return; // Abaikan pesan dari serial number lain
      }

      state.lastSyncTime = new Date();
      const subTopic = parts.slice(2).join('/');

      switch (subTopic) {
        case 'status':
          state.esp32Online = (payload === 'online');
          break;
        case 'tong1/isi':
          state.tong1.distance = parseFloat(payload) || state.tong1.distance;
          break;
        case 'tong1/orang':
          state.tong1.orang = parseFloat(payload) || state.tong1.orang;
          break;
        case 'tong1/servo':
          state.tong1.servo = payload;
          if (payload.toUpperCase() === 'TERBUKA') addActivityLog('Tong 1 (Organik) Servo TERBUKA', 'warning');
          break;
        case 'tong1/status':
          state.tong1.status = payload;
          if (payload.toUpperCase() === 'PENUH') addActivityLog('Tong 1 (Organik) TERDETEKSI PENUH!', 'danger');
          break;

        case 'tong2/isi':
          state.tong2.distance = parseFloat(payload) || state.tong2.distance;
          break;
        case 'tong2/orang':
          state.tong2.orang = parseFloat(payload) || state.tong2.orang;
          break;
        case 'tong2/servo':
          state.tong2.servo = payload;
          if (payload.toUpperCase() === 'TERBUKA') addActivityLog('Tong 2 (Anorganik) Servo TERBUKA', 'warning');
          break;
        case 'tong2/status':
          state.tong2.status = payload;
          if (payload.toUpperCase() === 'PENUH') addActivityLog('Tong 2 (Anorganik) TERDETEKSI PENUH!', 'danger');
          break;

        case 'tong3/isi':
          state.tong3.distance = parseFloat(payload) || state.tong3.distance;
          break;
        case 'tong3/orang':
          state.tong3.orang = parseFloat(payload) || state.tong3.orang;
          break;
        case 'tong3/servo':
          state.tong3.servo = payload;
          if (payload.toUpperCase() === 'TERBUKA') addActivityLog('Tong 3 (B3) Servo TERBUKA', 'warning');
          break;
        case 'tong3/status':
          state.tong3.status = payload;
          if (payload.toUpperCase() === 'PENUH') addActivityLog('Tong 3 (B3) TERDETEKSI PENUH! Audio Notifikasi Diputar.', 'danger');
          break;

        case 'led_system':
          state.ledStatus = payload;
          break;
        case 'matrix_text':
          state.ledText = payload;
          break;
        case 'dfplayer':
          state.dfplayerState = payload;
          break;
        case 'rssi':
          state.wifiRssi = parseInt(payload) || state.wifiRssi;
          break;
      }

      updateDashboardUI();
    });

    client.on('error', (err) => {
      console.warn('[MQTT] Connection error:', err);
    });
  } catch (err) {
    console.warn('[MQTT] Client error:', err);
  }
}

// =========================================================
// INTERACTIVE TEST SIMULATOR CONTROLS
// =========================================================
function initSimulatorControls() {
  elements.btnOpenSimulator.addEventListener('click', () => {
    elements.simulatorModal.classList.add('active');
  });

  elements.btnCloseSimulator.addEventListener('click', () => {
    elements.simulatorModal.classList.remove('active');
  });

  elements.simulatorModal.addEventListener('click', (e) => {
    if (e.target === elements.simulatorModal) {
      elements.simulatorModal.classList.remove('active');
    }
  });

  // Simulator Sliders & Inputs
  const simDeviceId = document.getElementById('sim-device-id');
  if (simDeviceId) {
    simDeviceId.value = state.serialNumber;
    simDeviceId.addEventListener('change', (e) => {
      const newId = e.target.value.trim() || '12345678';
      state.serialNumber = newId;
      localStorage.setItem('smartbin_device_id', newId);
      addActivityLog(`Device ID aktif: ${newId}`, 'warning');
      updateDashboardUI();
      if (mqttClientInstance && mqttClientInstance.connected) {
        mqttClientInstance.subscribe(`smartbin/${newId}/#`);
      }
    });
  }

  const simEsp32Status = document.getElementById('sim-esp32-status');
  const simRssi = document.getElementById('sim-rssi');
  const simRssiVal = document.getElementById('sim-rssi-val');

  const simDist1 = document.getElementById('sim-dist1');
  const simDist1Val = document.getElementById('sim-dist1-val');
  const simOrang1 = document.getElementById('sim-orang1');
  const simOrang1Val = document.getElementById('sim-orang1-val');
  const simServo1 = document.getElementById('sim-servo1');

  const simDist2 = document.getElementById('sim-dist2');
  const simDist2Val = document.getElementById('sim-dist2-val');
  const simOrang2 = document.getElementById('sim-orang2');
  const simOrang2Val = document.getElementById('sim-orang2-val');
  const simServo2 = document.getElementById('sim-servo2');

  const simDist3 = document.getElementById('sim-dist3');
  const simDist3Val = document.getElementById('sim-dist3-val');
  const simOrang3 = document.getElementById('sim-orang3');
  const simOrang3Val = document.getElementById('sim-orang3-val');
  const simServo3 = document.getElementById('sim-servo3');

  const simPresetMatrix = document.getElementById('sim-preset-matrix');
  const simLedText = document.getElementById('sim-led-text');
  const simDfplayer = document.getElementById('sim-dfplayer');

  // Input Listeners
  simRssi.addEventListener('input', e => { simRssiVal.textContent = e.target.value; state.wifiRssi = parseInt(e.target.value); updateDashboardUI(); });

  simDist1.addEventListener('input', e => { simDist1Val.textContent = e.target.value; state.tong1.distance = parseFloat(e.target.value); updateDashboardUI(); });
  simOrang1.addEventListener('input', e => { simOrang1Val.textContent = e.target.value; state.tong1.orang = parseFloat(e.target.value); updateDashboardUI(); });
  simServo1.addEventListener('change', e => { state.tong1.servo = e.target.value; updateDashboardUI(); });

  simDist2.addEventListener('input', e => { simDist2Val.textContent = e.target.value; state.tong2.distance = parseFloat(e.target.value); updateDashboardUI(); });
  simOrang2.addEventListener('input', e => { simOrang2Val.textContent = e.target.value; state.tong2.orang = parseFloat(e.target.value); updateDashboardUI(); });
  simServo2.addEventListener('change', e => { state.tong2.servo = e.target.value; updateDashboardUI(); });

  simDist3.addEventListener('input', e => { simDist3Val.textContent = e.target.value; state.tong3.distance = parseFloat(e.target.value); updateDashboardUI(); });
  simOrang3.addEventListener('input', e => { simOrang3Val.textContent = e.target.value; state.tong3.orang = parseFloat(e.target.value); updateDashboardUI(); });
  simServo3.addEventListener('change', e => { state.tong3.servo = e.target.value; updateDashboardUI(); });

  simEsp32Status.addEventListener('change', e => {
    state.esp32Online = (e.target.value === 'online');
    addActivityLog(`ESP32 Status berubah menjadi: ${e.target.value.toUpperCase()}`, state.esp32Online ? 'success' : 'danger');
    updateDashboardUI();
  });

  simPresetMatrix.addEventListener('change', e => {
    if (e.target.value !== 'custom') {
      simLedText.value = e.target.value;
      state.ledText = e.target.value;
    }
    updateDashboardUI();
  });

  simLedText.addEventListener('input', e => {
    state.ledText = e.target.value || 'Ayo Buang Sampah';
    updateDashboardUI();
  });

  simDfplayer.addEventListener('change', e => {
    state.dfplayerState = e.target.value;
    addActivityLog(`DFPlayer Audio MP3: ${state.dfplayerState}`, 'warning');
    updateDashboardUI();
  });

  elements.btnSimApply.addEventListener('click', () => {
    state.lastSyncTime = new Date();
    updateDashboardUI();
    addActivityLog('Telemetry Simulator diterapkan!', 'success');
    elements.simulatorModal.classList.remove('active');
  });

  elements.btnSimReset.addEventListener('click', () => {
    state.wifiRssi = -58;
    state.tong1 = { distance: 13, orang: 18, percent: 35, servo: 'Tertutup', status: 'NORMAL' };
    state.tong2 = { distance: 11, orang: 22, percent: 60, servo: 'Tertutup', status: 'NORMAL' };
    state.tong3 = { distance: 6, orang: 8, percent: 92, servo: 'Tertutup', status: 'PENUH' };
    state.ledText = 'Ayo Buang Sampah';
    state.dfplayerState = 'Standby';
    state.esp32Online = true;

    simRssi.value = -58; simRssiVal.textContent = -58;
    simDist1.value = 13; simDist1Val.textContent = 13; simOrang1.value = 18; simOrang1Val.textContent = 18; simServo1.value = 'Tertutup';
    simDist2.value = 11; simDist2Val.textContent = 11; simOrang2.value = 22; simOrang2Val.textContent = 22; simServo2.value = 'Tertutup';
    simDist3.value = 6; simDist3Val.textContent = 6; simOrang3.value = 8; simOrang3Val.textContent = 8; simServo3.value = 'Tertutup';
    simPresetMatrix.value = 'Ayo Buang Sampah';
    simLedText.value = 'Ayo Buang Sampah';
    simDfplayer.value = 'Standby';

    updateDashboardUI();
    addActivityLog('Data Simulator di-reset ke nilai standar.', 'success');
  });
}

// Live Clock Interval
function startClock() {
  setInterval(() => {
    const now = new Date();
    elements.systemClock.textContent = now.toTimeString().split(' ')[0];
    
    const secondsAgo = Math.floor((new Date() - state.lastSyncTime) / 1000);
    elements.valLastSync.textContent = secondsAgo < 5 ? 'Just Now' : `${secondsAgo}s ago`;
  }, 1000);
}

// Telemetry Chart Auto Update Interval
function startChartInterval() {
  setInterval(() => {
    addChartData();
  }, 4000);
}

// App Initialization
document.addEventListener('DOMContentLoaded', () => {
  console.log('[APP] Initializing Smart Bin IoT Dashboard...');
  
  updateDashboardUI();
  initChart();
  initSimulatorControls();
  startClock();
  startChartInterval();
  setupMQTT();

  // Interaktif Servo Control via MQTT
  [1, 2, 3].forEach(i => {
    const el = elements[`servoTong${i}`];
    if (el) {
      el.style.cursor = 'pointer';
      el.title = 'Klik untuk Buka/Tutup manual via MQTT';
      el.addEventListener('click', () => {
        const current = state[`tong${i}`].servo;
        const nextAction = (current && current.toLowerCase() === 'terbuka') ? 'TUTUP' : 'BUKA';
        sendServoCommand(i, nextAction);
      });
    }
  });

  // Sidebar Menu Navigation
  document.querySelectorAll('.sidebar-menu .nav-item').forEach(item => {
    item.addEventListener('click', (e) => {
      document.querySelectorAll('.sidebar-menu .nav-item').forEach(nav => nav.classList.remove('active'));
      item.classList.add('active');
      if (item.getAttribute('href') === '#pengaturan') {
        e.preventDefault();
        elements.simulatorModal.classList.add('active');
      }
    });
  });

  console.log('[APP] Dashboard Ready!');
});
