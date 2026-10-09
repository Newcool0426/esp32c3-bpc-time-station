/*
 * wifi_provision.cpp - 热点 + 网页配网实现（多组 Wi-Fi）
 *
 * NVS(namespace "bpcwifi", key "list") 中保存多组凭据：
 *   每组一行 "ssid\tpass"，组间用 '\n' 分隔。
 * 开机/断线时扫描周围网络，按信号强度自动连接已知网络。
 */
#include "wifi_provision.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

/* --------------------------------------------------------------- 状态 ---- */
static ProvisionDisplayFn g_disp = nullptr;
static WebServer*  g_server = nullptr;
static DNSServer*  g_dns    = nullptr;
static bool        g_configured = false;
static String      g_newSsid;
static String      g_newPass;
static String      g_options;      /* 扫描得到的 <option> 列表 */

static void show(const char* a, const char* b, const char* c)
{
    if (g_disp) g_disp(a, b, c);
}

static String htmlEscape(const String& in)
{
    String o;
    o.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];
        switch (c) {
            case '&':  o += F("&amp;");  break;
            case '<':  o += F("&lt;");   break;
            case '>':  o += F("&gt;");   break;
            case '"':  o += F("&quot;"); break;
            case '\'': o += F("&#39;");  break;
            default:   o += c;
        }
    }
    return o;
}

/* --------------------------------------------------------------- 存储 ---- */
static const char* NVS_NS  = "bpcwifi";
static const char* NVS_KEY = "list";

static String loadRaw()
{
    Preferences p;
    p.begin(NVS_NS, true);
    String s = p.getString(NVS_KEY, "");
    if (s.isEmpty()) {
        /* 兼容旧的单组存储 */
        String o = p.getString("ssid", "");
        if (!o.isEmpty()) s = o + "\t" + p.getString("pass", "");
    }
    p.end();
    return s;
}

static void saveRaw(const String& s)
{
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString(NVS_KEY, s);
    p.remove("ssid");
    p.remove("pass");
    p.end();
}

int wifiProvisionLoadList(String ssids[], String passes[], int maxN)
{
    String raw = loadRaw();
    int n = 0, pos = 0;
    while (pos < (int)raw.length() && n < maxN) {
        int nl = raw.indexOf('\n', pos);
        String line = (nl < 0) ? raw.substring(pos) : raw.substring(pos, nl);
        pos = (nl < 0) ? raw.length() : nl + 1;
        line.trim();
        if (line.isEmpty()) continue;
        int tab = line.indexOf('\t');
        String s = (tab < 0) ? line : line.substring(0, tab);
        String p = (tab < 0) ? String("") : line.substring(tab + 1);
        s.trim();
        if (s.isEmpty()) continue;
        ssids[n] = s;
        passes[n] = p;
        ++n;
    }
    return n;
}

int wifiProvisionCount()
{
    String ss[WIFI_MAX_NETS], pp[WIFI_MAX_NETS];
    return wifiProvisionLoadList(ss, pp, WIFI_MAX_NETS);
}

bool wifiProvisionAdd(const String& ssid, const String& pass)
{
    if (ssid.isEmpty()) return false;

    String ss[WIFI_MAX_NETS], pp[WIFI_MAX_NETS];
    int n = wifiProvisionLoadList(ss, pp, WIFI_MAX_NETS);

    int idx = -1;
    for (int i = 0; i < n; ++i) if (ss[i] == ssid) { idx = i; break; }

    if (idx < 0) {
        if (n >= WIFI_MAX_NETS) {           /* 满了：丢弃最旧的一组 */
            for (int i = 1; i < n; ++i) { ss[i - 1] = ss[i]; pp[i - 1] = pp[i]; }
            --n;
        }
        idx = n++;
    }
    ss[idx] = ssid;
    pp[idx] = pass;

    String raw;
    for (int i = 0; i < n; ++i) {
        if (i) raw += '\n';
        raw += ss[i];
        raw += '\t';
        raw += pp[i];
    }
    saveRaw(raw);
    Serial.printf("WiFi saved (%d): %s\n", n, ssid.c_str());
    return true;
}

void wifiProvisionClear()
{
    Preferences p;
    p.begin(NVS_NS, false);
    p.clear();
    p.end();
}

void wifiProvisionSetDisplay(ProvisionDisplayFn fn) { g_disp = fn; }

/* ------------------------------------------------------------- 连接 ------ */
bool wifiProvisionConnect(const String& ssid, const String& pass, uint32_t timeoutMs)
{
    Serial.printf("WiFi connect -> \"%s\"\n", ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    uint32_t lastDisp = 0;
    char shortSsid[18];
    snprintf(shortSsid, sizeof(shortSsid), "%.15s", ssid.c_str());

    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            show("WiFi OK", WiFi.localIP().toString().c_str(), "NTP ...");
            Serial.printf("WiFi connected, IP = %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        if (millis() - lastDisp > 400) {
            lastDisp = millis();
            show("Connecting", shortSsid, "...");
        }
        delay(50);
    }

    show("WiFi", "failed", shortSsid);
    Serial.printf("WiFi connect to \"%s\" failed\n", ssid.c_str());
    return false;
}

/* 扫描周围网络，自动连接已知网络中信号最好的一组 */
bool wifiProvisionConnectAny(uint32_t perNetTimeoutMs, String& outSsid, String& outPass)
{
    String ss[WIFI_MAX_NETS], pp[WIFI_MAX_NETS];
    int n = wifiProvisionLoadList(ss, pp, WIFI_MAX_NETS);
    if (n <= 0) return false;

    WiFi.mode(WIFI_STA);
    delay(50);
    int found = WiFi.scanNetworks();

    int rssi[WIFI_MAX_NETS];
    for (int i = 0; i < n; ++i) rssi[i] = -1000;
    for (int j = 0; j < found; ++j) {
        String s = WiFi.SSID(j);
        int r = WiFi.RSSI(j);
        for (int i = 0; i < n; ++i) if (ss[i] == s && r > rssi[i]) rssi[i] = r;
    }
    WiFi.scanDelete();

    /* 已知网络中：范围内按信号从强到弱，范围外的排后面（可能是隐藏 SSID） */
    int order[WIFI_MAX_NETS];
    for (int i = 0; i < n; ++i) order[i] = i;
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b)
            if (rssi[order[b]] > rssi[order[a]]) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    for (int k = 0; k < n; ++k) {
        int i = order[k];
        Serial.printf("try WiFi \"%s\" (rssi=%d)\n", ss[i].c_str(), rssi[i]);
        if (wifiProvisionConnect(ss[i], pp[i], perNetTimeoutMs)) {
            outSsid = ss[i];
            outPass = pp[i];
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------- 网页 ------ */
static String configPage()
{
    String saved;
    {
        String ss[WIFI_MAX_NETS], pp[WIFI_MAX_NETS];
        int n = wifiProvisionLoadList(ss, pp, WIFI_MAX_NETS);
        if (n == 0) saved = F("<li><small>（暂无）</small></li>");
        for (int i = 0; i < n; ++i) saved += "<li>" + htmlEscape(ss[i]) + "</li>";
    }

    String h;
    h.reserve(2000);
    h += F("<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">");
    h += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    h += F("<title>BPC 授时站 Wi-Fi 设置</title><style>");
    h += F("body{font-family:-apple-system,'Segoe UI',Roboto,sans-serif;margin:24px;max-width:420px;color:#222}");
    h += F("h2{font-size:20px;margin:0 0 6px}h3{font-size:15px;margin:22px 0 6px;color:#444}");
    h += F("label{display:block;margin:14px 0 4px;font-size:14px;color:#444}");
    h += F("input{width:100%;padding:10px;font-size:16px;box-sizing:border-box;border:1px solid #ccc;border-radius:6px}");
    h += F("button{margin-top:14px;width:100%;padding:12px;font-size:16px;background:#0a8f6a;color:#fff;border:0;border-radius:6px}");
    h += F("button.grey{background:#888}ul{padding-left:20px;color:#333}");
    h += F("small{color:#666}</style></head><body>");
    h += F("<h2>BPC 授时站 · Wi-Fi 设置</h2>");
    h += F("<p><small>可保存多组 Wi-Fi；开机/断线时自动连接范围内信号最好的一组。</small></p>");
    h += F("<h3>已保存的网络</h3><ul>");
    h += saved;
    h += F("</ul>");
    h += F("<form action=\"/clear\" method=\"POST\"><button class=\"grey\" type=\"submit\">清除全部</button></form>");
    h += F("<h3>添加 / 更新网络</h3>");
    h += F("<form action=\"/save\" method=\"POST\">");
    h += F("<label>Wi-Fi 名称 (SSID)</label>");
    h += F("<input name=\"ssid\" list=\"nets\" required autocomplete=\"off\" placeholder=\"SSID\">");
    h += F("<datalist id=\"nets\">");
    h += g_options;
    h += F("</datalist>");
    h += F("<label>密码</label>");
    h += F("<input name=\"pass\" type=\"password\" placeholder=\"Wi-Fi 密码\">");
    h += F("<button type=\"submit\">保存并连接</button>");
    h += F("</form></body></html>");
    return h;
}

static void handleRoot()  { g_server->send(200, "text/html", configPage()); }

static void handleSave()
{
    g_newSsid = g_server->arg("ssid");
    g_newPass = g_server->arg("pass");
    g_newSsid.trim();

    if (g_newSsid.isEmpty()) {
        g_server->send(400, "text/html", F("<meta charset=\"utf-8\"><h3>SSID 不能为空</h3><a href=\"/\">返回</a>"));
        return;
    }

    wifiProvisionAdd(g_newSsid, g_newPass);
    g_configured = true;

    g_server->send(200, "text/html", F("<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                                       "<h3>已保存</h3><p>设备正在连接 Wi-Fi，请稍候…</p>"));
    Serial.printf("Provisioned: SSID=\"%s\"\n", g_newSsid.c_str());
}

static void handleClear()
{
    wifiProvisionClear();
    g_server->send(200, "text/html", F("<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                                       "<h3>已清除全部 Wi-Fi</h3><a href=\"/\">返回</a>"));
    Serial.println("Provisioned: cleared all WiFi");
}

/* ------------------------------------------------------------- 配网热点 -- */
bool wifiProvisionRunPortal(uint32_t timeoutMs, String& outSsid, String& outPass)
{
    g_configured = false;
    g_newSsid = "";
    g_newPass = "";

    /* 先扫描可用网络，供页面下拉选择 */
    WiFi.mode(WIFI_STA);
    delay(50);
    int n = WiFi.scanNetworks();
    g_options = "";
    if (n > 0) {
        for (int i = 0; i < n && i < 24; ++i) {
            String s = WiFi.SSID(i);
            if (s.isEmpty()) continue;
            g_options += F("<option value=\"");
            g_options += htmlEscape(s);
            g_options += F("\">");
        }
    }
    WiFi.scanDelete();
    Serial.printf("Scan found %d networks\n", n);

    /* 开启开放热点 */
    uint32_t mac = (uint32_t)ESP.getEfuseMac();
    char ap[40];
    snprintf(ap, sizeof(ap), "%s-%04X", AP_SSID_PREFIX, (unsigned)(mac & 0xFFFF));

    WiFi.mode(WIFI_AP);
    bool apOk = WiFi.softAP(ap);
    delay(100);
    IPAddress ip = WiFi.softAPIP();

    g_server = new WebServer(80);
    g_dns    = new DNSServer();
    g_dns->start(53, "*", ip);
    g_server->on("/", HTTP_GET, handleRoot);
    g_server->on("/save", HTTP_POST, handleSave);
    g_server->on("/clear", HTTP_POST, handleClear);
    g_server->onNotFound(handleRoot);       /* 捕获门户探测请求 */
    g_server->begin();

    Serial.printf("[portal] softAP(\"%s\")=%d ip=%s mac=%s\n",
                  ap, (int)apOk, ip.toString().c_str(),
                  WiFi.softAPmacAddress().c_str());

    uint32_t start = millis();
    uint32_t lastDisp = 0;
    uint32_t lastLog = 0;
    bool ok = false;

    for (;;) {
        g_dns->processNextRequest();
        g_server->handleClient();

        if (g_configured) { ok = true; break; }
        if (millis() - start > timeoutMs) break;

        if (millis() - lastDisp > 500) {
            lastDisp = millis();
            char ssidLine[44];
            snprintf(ssidLine, sizeof(ssidLine), "AP:%s", ap);
            show("WiFi 设置", ssidLine, ip.toString().c_str());
        }
        if (millis() - lastLog > 3000) {
            lastLog = millis();
            Serial.printf("[portal] waiting... clients=%d\n", WiFi.softAPgetStationNum());
        }
        delay(2);
    }

    g_server->stop();
    delete g_server; g_server = nullptr;
    g_dns->stop();
    delete g_dns; g_dns = nullptr;
    WiFi.softAPdisconnect(true);

    if (ok) { outSsid = g_newSsid; outPass = g_newPass; }
    return ok;
}
