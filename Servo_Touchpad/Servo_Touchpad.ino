#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h> // "Websockets" by Markus Sattler
#include <Servo.h>

// WiFi Configuration
const char* ssid = "WIFI_SSID";
const char* password = "PASSWORD";

// Static IP Configuration
IPAddress local_IP(192, 168, 1, 150); 
IPAddress gateway(192, 168, 1, 1);   
IPAddress subnet(255, 255, 255, 0);  
IPAddress dns(192, 168, 1, 1);       

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

Servo servoX;
Servo servoY;

const int pinX = D1; // GPIO5 -> X-Axis Servo Signal
const int pinY = D2; // GPIO4 -> Y-Axis Servo Signal

// Watchdog & Heartbeat Variables
unsigned long lastPing = 0;
unsigned long lastClientActivity = 0;
const unsigned long WATCHDOG_TIMEOUT = 3000; // 3 seconds absolute timeout
bool systemSafeParked = false;

// HTML/JS/CSS UI UI Packed in Flash Memory via PROGMEM
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>Servo Touchpad Controller</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background-color: #1e1e24;
            color: #fff;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100vh;
            margin: 0;
            overflow: hidden;
        }
        h2 { margin-bottom: 5px; color: #00adb5; font-weight:300; letter-spacing:2px; }
        .status-container { display: flex; gap: 15px; margin-bottom: 15px; }
        .badge {
            font-size: 1rem; background: #393e46; padding: 8px 16px;
            border-radius: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.1);
            display: flex; align-items: center; gap: 8px;
        }
        .indicator { width: 10px; height: 10px; border-radius: 50%; background-color: #ff4a4a; transition: background-color 0.3s ease; }
        .online { background-color: #4ade80; }
        
        .controls-pane { display: flex; gap: 20px; background: #2b2e3a; padding: 12px 24px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 10px rgba(0,0,0,0.2); }
        .switch-group { display: flex; align-items: center; gap: 10px; font-size: 0.95rem; font-weight: bold; }
        .switch { position: relative; display: inline-block; width: 44px; height: 24px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .3s; border-radius: 24px; }
        .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; }
        input:checked + .slider { background-color: #00adb5; }
        input:checked + .slider:before { transform: translateX(20px); }

        #touchpad { background: #222831; border: 3px solid #00adb5; border-radius: 15px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); cursor: crosshair; touch-action: none; }
    </style>
</head>
<body>

    <h2>SERVO TOUCHPAD</h2>
    
    <div class="status-container">
        <div class="badge">
            <div id="statusIndicator" class="indicator"></div>
            <span id="statusText">System Offline</span>
        </div>
        <div class="badge">X: <span id="valX">90</span>° | Y: <span id="valY">90</span>°</div>
    </div>

    <div class="controls-pane">
        <div class="switch-group">
            <span>Reverse X</span>
            <label class="switch">
                <input type="checkbox" id="revX">
                <span class="slider"></span>
            </label>
        </div>
        <div class="switch-group">
            <span>Reverse Y</span>
            <label class="switch">
                <input type="checkbox" id="revY">
                <span class="slider"></span>
            </label>
        </div>
    </div>

    <canvas id="touchpad" width="320" height="320"></canvas>

<script>
    const canvas = document.getElementById('touchpad');
    const ctx = canvas.getContext('2d');
    const valXDisplay = document.getElementById('valX');
    const valYDisplay = document.getElementById('valY');
    const statusIndicator = document.getElementById('statusIndicator');
    const statusText = document.getElementById('statusText');
    const revXCheck = document.getElementById('revX');
    const revYCheck = document.getElementById('revY');

    let isDrawing = false;
    let posX = canvas.width / 2;
    let posY = canvas.height / 2;
    
    let targetXAngle = 90;
    let targetYAngle = 90;
    let lastSentX = 90;
    let lastSentY = 90;
    
    let socket;
    let watchdogTimer;

    function resetToMiddle() {
        posX = canvas.width / 2;
        posY = canvas.height / 2;
        targetXAngle = 90;
        targetYAngle = 90;
        lastSentX = 90;
        lastSentY = 90;
        valXDisplay.innerText = "90";
        valYDisplay.innerText = "90";
        draw();
    }

    function setOffline() {
        statusIndicator.classList.remove('online');
        statusText.innerText = "System Offline";
        statusText.style.color = "#ff4a4a";
    }

    function connect() {
        socket = new WebSocket('ws://' + window.location.hostname + ':81/');
        
        socket.onopen = () => {
            statusIndicator.classList.add('online');
            statusText.innerText = "System Online";
            statusText.style.color = "#4ade80";
            resetWatchdog();
        };

        socket.onmessage = (event) => {
            if(event.data === "p") {
                resetWatchdog();
            }
            // Trigger UI realignment when hardware signals an active reset condition
            if(event.data === "reset") {
                resetToMiddle();
            }
        };

        socket.onclose = () => {
            setOffline();
            setTimeout(connect, 2000); // Reconnect drop-protection
        };
    }

    function resetWatchdog() {
        clearTimeout(watchdogTimer);
        watchdogTimer = setTimeout(() => {
            if(socket) socket.close();
            setOffline();
        }, 3000); // Trigger visual drops if missing 3 pings
    }

    function draw() {
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        
        ctx.strokeStyle = '#393e46';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(canvas.width / 2, 0); ctx.lineTo(canvas.width / 2, canvas.height);
        ctx.moveTo(0, canvas.height / 2); ctx.lineTo(canvas.width, canvas.height / 2);
        ctx.stroke();

        ctx.beginPath();
        ctx.arc(posX, posY, 12, 0, Math.PI * 2);
        ctx.fillStyle = '#00adb5';
        ctx.fill();
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 2;
        ctx.stroke();
    }

    function updateCoordinates(clientX, clientY) {
        const rect = canvas.getBoundingClientRect();
        
        posX = Math.max(0, Math.min(canvas.width, clientX - rect.left));
        posY = Math.max(0, Math.min(canvas.height, clientY - rect.top));

        let rawX = Math.round((posX / canvas.width) * 180);
        let rawY = Math.round((posY / canvas.height) * 180); 

        targetXAngle = revXCheck.checked ? (180 - rawX) : rawX;
        targetYAngle = revYCheck.checked ? (180 - rawY) : rawY;

        valXDisplay.innerText = targetXAngle;
        valYDisplay.innerText = targetYAngle;
        draw();
    }

    revXCheck.addEventListener('change', () => { 
        let rawX = Math.round((posX / canvas.width) * 180);
        targetXAngle = revXCheck.checked ? (180 - rawX) : rawX;
        valXDisplay.innerText = targetXAngle;
    });
    revYCheck.addEventListener('change', () => { 
        let rawY = Math.round((posY / canvas.height) * 180);
        targetYAngle = revYCheck.checked ? (180 - rawY) : rawY;
        valYDisplay.innerText = targetYAngle;
    });

    // 40ms high frequency transmission sweep
    setInterval(function transmitDataPool() {
        if (socket && socket.readyState === WebSocket.OPEN) {
            if (targetXAngle !== lastSentX || targetYAngle !== lastSentY) {
                lastSentX = targetXAngle;
                lastSentY = targetYAngle;
                
                // Pack values cleanly comma-separated: "X,Y"
                socket.send(lastSentX + "," + lastSentY);
            }
        }
    }, 40);

    // Mouse Action Listeners
    canvas.addEventListener('mousedown', (e) => { isDrawing = true; updateCoordinates(e.clientX, e.clientY); });
    window.addEventListener('mousemove', (e) => { if (isDrawing) updateCoordinates(e.clientX, e.clientY); });
    window.addEventListener('mouseup', () => { isDrawing = false; });

    // Smartphone Mobile Touch Optimizations
    canvas.addEventListener('touchstart', (e) => { isDrawing = true; updateCoordinates(e.touches[0].clientX, e.touches[0].clientY); e.preventDefault(); });
    window.addEventListener('touchmove', (e) => { if (isDrawing) updateCoordinates(e.touches[0].clientX, e.touches[0].clientY); }, { passive: false });
    window.addEventListener('touchend', () => { isDrawing = false; });

    draw();
    connect();
</script>
</body>
</html>
)rawliteral";

// Hardware Incoming Packet Process
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        lastClientActivity = millis(); // Clear the timeout countdown instantly
        systemSafeParked = false;     // Wake up from safety state if parked

        int targetX, targetY;
        // Parse "X,Y" format reliably out of raw text payloads
        if (sscanf((char*)payload, "%d,%d", &targetX, &targetY) == 2) {
            if (targetX >= 0 && targetX <= 180 && targetY >= 0 && targetY <= 180) {
                servoX.write(targetX);
                servoY.write(targetY);
            }
        }
    } else if (type == WStype_CONNECTED) {
        lastClientActivity = millis();
        systemSafeParked = false;
        Serial.printf("[%u] Touchpad Controller Connected.\n", num);
    } else if (type == WStype_DISCONNECTED) {
        Serial.printf("[%u] Touchpad Controller Dropped.\n", num);
    }
}

void setup() {
    Serial.begin(115200);

    servoX.attach(pinX, 500, 2400);
    servoY.attach(pinY, 500, 2400);
    servoX.write(90);
    servoY.write(90);

    WiFi.mode(WIFI_STA);
    if (!WiFi.config(local_IP, gateway, subnet, dns)) {
        Serial.println("[ERROR] Static IP Setup Failed!");
    }

    WiFi.begin(ssid, password);
    Serial.print("Connecting to local network");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nWiFi Connected Successfully!");
    Serial.print("Target Address: http://");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", htmlPage);
    });

    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    
    lastClientActivity = millis();
}

void loop() {
    webSocket.loop();
    server.handleClient();

    unsigned long currentMillis = millis();

    // 1. Hardware Heartbeat out to Browser UI client (1 second intervals)
    if (currentMillis - lastPing > 1000) {
        webSocket.broadcastTXT("p"); 
        lastPing = currentMillis;
    }

    // 2. Hardware Safety Watchdog Cutoff check
    if (!systemSafeParked && (currentMillis - lastClientActivity > WATCHDOG_TIMEOUT)) {
        Serial.println("[CRITICAL WATCHDOG] No active touch framing or connection! Parking Servos.");
        servoX.write(90);
        servoY.write(90);
        
        // Push structural notice back to client app forcing visual middle realignment
        webSocket.broadcastTXT("reset");
        
        systemSafeParked = true; // Lock down until next text framing transmission wakes it up
    }
}
