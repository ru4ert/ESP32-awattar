// ─────────────────────────────────────────────────────────────────────────────
// gcal-bridge.gs – Google Apps Script, das den OAuth-2.0-Authorization-Code-
// Flow für den ESP32-Kalender-Preisalarm server-seitig abwickelt.
//
// Warum das nötig ist: Google erlaubt Calendar-Scopes NICHT über den nativen
// Device-Authorization-Flow (nur email/openid/profile/drive.appdata/
// drive.file/youtube sind dort zugelassen – siehe
// https://developers.google.com/identity/protocols/oauth2/limited-input-device).
// Der ESP32 kann selbst keinen öffentlichen HTTPS-Redirect-Endpunkt hosten,
// den Google für den echten Authorization-Code-Flow braucht – das übernimmt
// dieses Script (läuft kostenlos unter deinem eigenen Google-Account).
//
// Ablauf:
//   1. ESP32 zeigt einen QR-Code mit "<diese-URL>?start=<zufällige SessionId>"
//   2. Handy scannt → doGet(start=...) → baut die volle Google-Consent-URL
//      und leitet den Browser dorthin weiter
//   3. Nutzer bestätigt bei Google → Google ruft "<diese-URL>?code=...&state=..."
//      auf → doGet(code=...) tauscht den Code gegen ein Refresh-Token, holt
//      die E-Mail-Adresse und legt beides kurzzeitig (10 Min) im Script-Cache ab
//   4. ESP32 pollt "<diese-URL>?poll=<SessionId>" → bekommt einmalig
//      {"email":"...","refreshToken":"..."} zurück (danach aus dem Cache
//      gelöscht) oder "pending", solange noch nichts vorliegt
//
// ── Setup ──────────────────────────────────────────────────────────────────
// 1. script.google.com → New project → diesen Code einfügen.
// 2. Deploy → New deployment → Type "Web app" → Execute as "Me",
//    Who has access "Anyone" → Deploy. Die "...​/exec"-URL kopieren.
// 3. Google Cloud Console → APIs & Services → Credentials → Create
//    Credentials → OAuth client ID → Application type "Web application" →
//    bei "Authorized redirect URIs" genau diese exec-URL eintragen → Create.
// 4. Client-ID/-Secret unten eintragen, REDIRECT_URI = dieselbe exec-URL,
//    Script speichern und über "Manage deployments" → Edit → New version
//    NEU deployen (URL bleibt dabei gleich).
// 5. Client-ID/-Secret + exec-URL in include/secrets.h eintragen
//    (GOOGLE_CLIENT_ID / GOOGLE_CLIENT_SECRET / GCAL_BRIDGE_URL).
// ─────────────────────────────────────────────────────────────────────────────

var CLIENT_ID     = "TODO.apps.googleusercontent.com";
var CLIENT_SECRET = "TODO";
var REDIRECT_URI  = "TODO";  // die eigene .../exec-URL aus Schritt 2
var SCOPE         = "https://www.googleapis.com/auth/calendar.events email openid";
var CACHE_TTL_SEC = 1800;    // 30 Min – muss zu PairingState::expiresAt im ESP32 passen

function doGet(e) {
    var p = e.parameter;
    if (p.start) return handleStart(p.start);
    if (p.code)  return handleCallback(p.code, p.state);
    if (p.poll)  return handlePoll(p.poll);
    return ContentService.createTextOutput("unbekannte Anfrage");
}

function handleStart(sessionId) {
    var authUrl = "https://accounts.google.com/o/oauth2/v2/auth"
        + "?client_id=" + encodeURIComponent(CLIENT_ID)
        + "&redirect_uri=" + encodeURIComponent(REDIRECT_URI)
        + "&response_type=code"
        + "&access_type=offline&prompt=consent"
        + "&scope=" + encodeURIComponent(SCOPE)
        + "&state=" + encodeURIComponent(sessionId);
    // Apps-Script-Output läuft in einem gesandboxten iframe – ein simples
    // window.location.href navigiert dort oft nur den inneren Frame, nicht
    // den echten Browser-Tab. window.top + ein echter Klick-Link (target=
    // "_top") als sichtbarer Fallback stellen sicher, dass es trotzdem klappt.
    return HtmlService.createHtmlOutput(
        '<html><body style="font-family:sans-serif;text-align:center;margin-top:40px">' +
        '<p>Weiterleitung zu Google …</p>' +
        '<p><a href="' + authUrl + '" target="_top">Falls nichts passiert: hier tippen</a></p>' +
        '<script>window.top.location.href=' + JSON.stringify(authUrl) + ';</script>' +
        '</body></html>');
}

function handleCallback(code, sessionId) {
    var resp = UrlFetchApp.fetch("https://oauth2.googleapis.com/token", {
        method: "post",
        payload: {
            code: code,
            client_id: CLIENT_ID,
            client_secret: CLIENT_SECRET,
            redirect_uri: REDIRECT_URI,
            grant_type: "authorization_code"
        },
        muteHttpExceptions: true
    });
    var data = JSON.parse(resp.getContentText());
    if (!data.refresh_token) {
        return HtmlService.createHtmlOutput(
            "<h3>Fehler: kein Refresh-Token erhalten.</h3><p>" +
            (data.error_description || data.error || "unbekannter Fehler") +
            "</p><p>Falls du diesen Account schon einmal gekoppelt hattest: " +
            "Zugriff unter myaccount.google.com/permissions widerrufen und " +
            "erneut versuchen.</p>");
    }

    var uiResp = UrlFetchApp.fetch("https://www.googleapis.com/oauth2/v3/userinfo", {
        headers: { Authorization: "Bearer " + data.access_token }
    });
    var email = JSON.parse(uiResp.getContentText()).email || "Konto";

    CacheService.getScriptCache().put(
        sessionId,
        JSON.stringify({ email: email, refreshToken: data.refresh_token }),
        CACHE_TTL_SEC
    );

    return HtmlService.createHtmlOutput(
        "<h3>Fertig! Du kannst dieses Fenster jetzt schließen.</h3>" +
        "<p>Konto: " + email + "</p>");
}

function handlePoll(sessionId) {
    var cache = CacheService.getScriptCache();
    var val = cache.get(sessionId);
    if (!val) return ContentService.createTextOutput("pending");
    cache.remove(sessionId);  // nur einmal abholbar
    return ContentService.createTextOutput(val)
        .setMimeType(ContentService.MimeType.JSON);
}
