// license:GPLv3+

#include <cstdlib>
#include <cstring>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <format>
#include <atomic>

#include "plugins/VPXPlugin.h"
#include "plugins/ControllerPlugin.h"
#include "plugins/LoggingPlugin.h"

#define MG_ENABLE_LOG 0
#include <mongoose/mongoose.h>

using namespace std;

namespace WebDisplay {

static const MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;
static uint32_t endpointId;

static unsigned int getVpxApiId;
static unsigned int onGameStartId;
static unsigned int onGameEndId;
static unsigned int onDmdSrcChangedId;
static unsigned int getDmdSrcMsgId;

static struct mg_mgr mgr;
static std::thread serverThread;
static std::atomic<bool> serverRunning{false};
static std::atomic<bool> gameRunning{false};

static std::mutex displayMutex;
static vector<DisplaySrcId> displaySources;

static std::mutex wsMutex;
static vector<struct mg_connection*> wsConnections;

MSGPI_INT_VAL_SETTING(portProp, "Port", "Port", "Web server port", true, 1024, 65535, 4000);

LPI_USE_CPP();
#define LOGD WebDisplay::LPI_LOGD_CPP
#define LOGI WebDisplay::LPI_LOGI_CPP
#define LOGW WebDisplay::LPI_LOGW_CPP
#define LOGE WebDisplay::LPI_LOGE_CPP

LPI_IMPLEMENT_CPP

static const char* WINDOW_NAMES[] = { "Playfield", "Backglass", "ScoreView", "Topper" };

static void ConvertDisplayFrameToRGBA(const DisplaySrcId& src, vector<uint8_t>& rgba)
{
   DisplayFrame frame = src.GetRenderFrame(src.id);
   if (!frame.frame) return;

   unsigned int pixels = src.width * src.height;
   rgba.resize(pixels * 4);

   switch (src.frameFormat) {
      case CTLPI_DISPLAY_FORMAT_LUM32F: {
         const float* lum = static_cast<const float*>(frame.frame);
         for (unsigned int i = 0; i < pixels; i++) {
            uint8_t v = (uint8_t)(lum[i] * 255.0f);
            rgba[i * 4] = v;
            rgba[i * 4 + 1] = v;
            rgba[i * 4 + 2] = v;
            rgba[i * 4 + 3] = 255;
         }
         break;
      }
      case CTLPI_DISPLAY_FORMAT_SRGB888: {
         const uint8_t* src888 = static_cast<const uint8_t*>(frame.frame);
         for (unsigned int i = 0; i < pixels; i++) {
            rgba[i * 4]     = src888[i * 3];
            rgba[i * 4 + 1] = src888[i * 3 + 1];
            rgba[i * 4 + 2] = src888[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
         }
         break;
      }
      case CTLPI_DISPLAY_FORMAT_SRGB565: {
         const uint16_t* src565 = static_cast<const uint16_t*>(frame.frame);
         for (unsigned int i = 0; i < pixels; i++) {
            uint16_t p = src565[i];
            rgba[i * 4]     = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
            rgba[i * 4 + 1] = (uint8_t)(((p >> 5) & 0x3F) * 255 / 63);
            rgba[i * 4 + 2] = (uint8_t)((p & 0x1F) * 255 / 31);
            rgba[i * 4 + 3] = 255;
         }
         break;
      }
   }
}

// Binary WebSocket frame protocol:
// [1 byte: type] [1 byte: source id] [2 bytes: width LE] [2 bytes: height LE] [width*height*4 bytes: RGBA data]
// type 0 = display source, type 1 = window capture
static void SendFrameToWS(struct mg_connection* c, uint8_t type, uint8_t sourceId, uint16_t width, uint16_t height, const void* rgbaData)
{
   size_t headerSize = 6;
   size_t dataSize = (size_t)width * height * 4;
   vector<uint8_t> packet(headerSize + dataSize);
   packet[0] = type;
   packet[1] = sourceId;
   memcpy(&packet[2], &width, 2);
   memcpy(&packet[4], &height, 2);
   memcpy(&packet[6], rgbaData, dataSize);
   mg_ws_send(c, (const char*)packet.data(), packet.size(), WEBSOCKET_OP_BINARY);
}

static void BroadcastFrames()
{
   static int broadcastLogCount = 0;
   std::lock_guard<std::mutex> wsLock(wsMutex);
   if (wsConnections.empty() || !vpxApi)
      return;

   for (int wnd = VPXWINDOW_Backglass; wnd <= VPXWINDOW_Topper; wnd++)
   {
      int w = 0, h = 0;
      unsigned int frameId = 0;
      const void* data = nullptr;
      int result = vpxApi->GetWindowFrame((VPXWindowId)wnd, &w, &h, &frameId, &data);
      if (broadcastLogCount < 30 && broadcastLogCount % 10 == 0)
         LOGI(std::format("WebCapture: GetWindowFrame wnd={} result={} w={} h={} frameId={} data={}", wnd, result, w, h, frameId, (data ? "yes" : "null")));
      if (result && data)
      {
         for (auto* c : wsConnections)
            SendFrameToWS(c, 1, (uint8_t)wnd, (uint16_t)w, (uint16_t)h, data);
      }
   }
   broadcastLogCount++;

   {
      std::lock_guard<std::mutex> lock(displayMutex);
      for (size_t i = 0; i < displaySources.size(); i++)
      {
         auto& src = displaySources[i];
         vector<uint8_t> rgba;
         ConvertDisplayFrameToRGBA(src, rgba);
         if (!rgba.empty())
         {
            for (auto* c : wsConnections)
               SendFrameToWS(c, 0, (uint8_t)i, (uint16_t)src.width, (uint16_t)src.height, rgba.data());
         }
      }
   }
}

static const char* HTML_PAGE = R"html(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>VPX Web Display</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#111;color:#eee;font-family:sans-serif;padding:20px}
h1{margin-bottom:20px;font-size:1.4em}
.section{margin-bottom:24px}
.section h2{font-size:1.1em;margin-bottom:12px;color:#ccc}
.displays{display:flex;flex-wrap:wrap;gap:20px}
.display{background:#222;border-radius:8px;padding:12px;display:flex;flex-direction:column;align-items:center}
.display h3{margin-bottom:8px;font-size:0.9em;color:#aaa}
.display canvas{background:#000;image-rendering:pixelated}
#status{color:#888;margin-bottom:16px;font-size:0.85em}
</style>
</head>
<body>
<h1>VPX Web Display</h1>
<div id="status">Connecting...</div>
<div class="section" id="windows-section" style="display:none">
<h2>Windows</h2>
<div class="displays" id="windows"></div>
</div>
<div class="section" id="dmd-section" style="display:none">
<h2>Display Sources</h2>
<div class="displays" id="dmds"></div>
</div>
<script>
const WINDOW_NAMES = ['Playfield', 'Backglass', 'ScoreView', 'Topper'];
const windows = {};
const dmds = {};
let ws;
let frameCount = 0;

function getOrCreateCanvas(container, key, label) {
  let entry = container === 'windows' ? windows[key] : dmds[key];
  if (entry) return entry;
  const div = document.createElement('div');
  div.className = 'display';
  div.id = container + '-' + key;
  const h3 = document.createElement('h3');
  h3.textContent = label;
  const canvas = document.createElement('canvas');
  div.appendChild(h3);
  div.appendChild(canvas);
  document.getElementById(container).appendChild(div);
  document.getElementById(container + '-section').style.display = '';
  entry = { canvas, ctx: canvas.getContext('2d'), label: h3 };
  if (container === 'windows') windows[key] = entry;
  else dmds[key] = entry;
  return entry;
}

function handleFrame(data) {
  const view = new DataView(data);
  const type = view.getUint8(0);
  const sourceId = view.getUint8(1);
  const width = view.getUint16(2, true);
  const height = view.getUint16(4, true);
  const pixels = new Uint8ClampedArray(data, 6);

  let entry;
  if (type === 1) {
    const name = WINDOW_NAMES[sourceId] || ('Window ' + sourceId);
    entry = getOrCreateCanvas('windows', 'w' + sourceId, name);
  } else {
    entry = getOrCreateCanvas('dmds', 'd' + sourceId, 'DMD ' + sourceId + ' (' + width + 'x' + height + ')');
  }

  if (entry.canvas.width !== width || entry.canvas.height !== height) {
    entry.canvas.width = width;
    entry.canvas.height = height;
    const scale = Math.max(1, Math.min(3, Math.floor(600 / Math.max(width, height))));
    entry.canvas.style.width = (width * scale) + 'px';
    entry.canvas.style.height = (height * scale) + 'px';
  }

  const imgData = new ImageData(pixels, width, height);
  entry.ctx.putImageData(imgData, 0, 0);
  frameCount++;
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(proto + '//' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { document.getElementById('status').textContent = 'Connected'; };
  ws.onmessage = (e) => { if (e.data instanceof ArrayBuffer) handleFrame(e.data); };
  ws.onclose = () => {
    document.getElementById('status').textContent = 'Disconnected - reconnecting...';
    setTimeout(connect, 2000);
  };
}

setInterval(() => {
  document.getElementById('status').textContent = 'Connected - ' + frameCount + ' fps';
  frameCount = 0;
}, 1000);

connect();
</script>
</body>
</html>)html";

static void EventHandler(struct mg_connection* c, int ev, void* ev_data)
{
   if (ev == MG_EV_HTTP_MSG) {
      struct mg_http_message* hm = (struct mg_http_message*)ev_data;

      if (mg_match(hm->uri, mg_str("/ws"), nullptr)) {
         mg_ws_upgrade(c, hm, nullptr);
         std::lock_guard<std::mutex> lock(wsMutex);
         wsConnections.push_back(c);
         LOGI("WebSocket client connected"s);
      }
      else if (mg_match(hm->uri, mg_str("/"), nullptr)) {
         mg_http_reply(c, 200,
            "Content-Type: text/html\r\n"
            "Cache-Control: no-cache\r\n",
            "%s", HTML_PAGE);
      }
      else {
         mg_http_reply(c, 404, "", "Not found");
      }
   }
   else if (ev == MG_EV_CLOSE) {
      std::lock_guard<std::mutex> lock(wsMutex);
      wsConnections.erase(std::remove(wsConnections.begin(), wsConnections.end(), c), wsConnections.end());
   }
}

static void ServerThread()
{
   mg_mgr_init(&mgr);
   string listenUrl = std::format("http://0.0.0.0:{}", portProp_Val);
   struct mg_connection* listener = mg_http_listen(&mgr, listenUrl.c_str(), EventHandler, nullptr);
   if (!listener) {
      LOGE(std::format("Failed to start web server on port {}", portProp_Val));
      mg_mgr_free(&mgr);
      serverRunning = false;
      return;
   }
   LOGI(std::format("Web Display server started on port {}", portProp_Val));

   auto lastBroadcast = std::chrono::steady_clock::now();
   while (serverRunning)
   {
      mg_mgr_poll(&mgr, 16);
      auto now = std::chrono::steady_clock::now();
      if (gameRunning && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBroadcast).count() >= 33)
      {
         BroadcastFrames();
         lastBroadcast = now;
      }
   }
   mg_mgr_free(&mgr);
   LOGI("Web Display server stopped"s);
}

static void RefreshDisplaySources()
{
   GetDisplaySrcMsg getSrcMsg = { 256, 0, new DisplaySrcId[256] };
   msgApi->BroadcastMsg(endpointId, getDmdSrcMsgId, &getSrcMsg);

   std::lock_guard<std::mutex> lock(displayMutex);
   displaySources.clear();
   for (unsigned int i = 0; i < getSrcMsg.count && i < 256; i++)
      displaySources.push_back(getSrcMsg.entries[i]);
   delete[] getSrcMsg.entries;

   LOGI(std::format("Display sources updated: {} found", displaySources.size()));
}

static void onDmdSrcChanged(const unsigned int msgId, void* userData, void* msgData)
{
   RefreshDisplaySources();
}

static void onGameStart(const unsigned int msgId, void* userData, void* msgData)
{
   gameRunning = true;
   LOGI("WebCapture: onGameStart fired"s);
   RefreshDisplaySources();
}

static void onGameEnd(const unsigned int msgId, void* userData, void* msgData)
{
   gameRunning = false;
   std::lock_guard<std::mutex> lock(displayMutex);
   displaySources.clear();
}

}

using namespace WebDisplay;

MSGPI_EXPORT void MSGPIAPI WebDisplayPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;

   LPISetup(endpointId, msgApi);

   msgApi->BroadcastMsg(endpointId, getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API), &vpxApi);

   msgApi->RegisterSetting(endpointId, &portProp);

   msgApi->SubscribeMsg(endpointId, onGameStartId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_GAME_START), onGameStart, nullptr);
   msgApi->SubscribeMsg(endpointId, onGameEndId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_GAME_END), onGameEnd, nullptr);
   msgApi->SubscribeMsg(endpointId, onDmdSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_ON_SRC_CHG_MSG), onDmdSrcChanged, nullptr);
   getDmdSrcMsgId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG);

   serverRunning = true;
   serverThread = std::thread(ServerThread);

   LOGI("WebDisplay plugin loaded"s);
}

MSGPI_EXPORT void MSGPIAPI WebDisplayPluginUnload()
{
   serverRunning = false;
   if (serverThread.joinable())
      serverThread.join();

   {
      std::lock_guard<std::mutex> lock(wsMutex);
      wsConnections.clear();
   }
   {
      std::lock_guard<std::mutex> lock(displayMutex);
      displaySources.clear();
   }

   msgApi->UnsubscribeMsg(onGameStartId, onGameStart, nullptr);
   msgApi->UnsubscribeMsg(onGameEndId, onGameEnd, nullptr);
   msgApi->UnsubscribeMsg(onDmdSrcChangedId, onDmdSrcChanged, nullptr);

   msgApi->ReleaseMsgID(getVpxApiId);
   msgApi->ReleaseMsgID(onGameStartId);
   msgApi->ReleaseMsgID(onGameEndId);
   msgApi->ReleaseMsgID(onDmdSrcChangedId);
   msgApi->ReleaseMsgID(getDmdSrcMsgId);

   vpxApi = nullptr;
   msgApi = nullptr;
}
