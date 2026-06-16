var http = require('http');
var cp = require('child_process');
var crypto = require('crypto');

// The GNOME HIG (Adwaita) replica CSS
var GNOME_CSS = "<style>" +
  ":root {" +
  "  --window-bg: #fafafa;" +
  "  --header-bg: #ebebeb;" +
  "  --text-color: #242424;" +
  "  --accent: #3584e4;" +
  "  --border: #d2d2d2;" +
  "}" +
  "@media (prefers-color-scheme: dark) {" +
  "  :root {" +
  "    --window-bg: #242424;" +
  "    --header-bg: #303030;" +
  "    --text-color: #ffffff;" +
  "    --border: #1e1e1e;" +
  "  }" +
  "}" +
  "body, html {" +
  "  margin: 0; padding: 0;" +
  "  font-family: 'Cantarell', 'Inter', system-ui, sans-serif;" +
  "  background-color: var(--window-bg);" +
  "  color: var(--text-color);" +
  "  height: 100vh;" +
  "  display: flex;" +
  "  flex-direction: column;" +
  "}" +
  "curica-header {" +
  "  display: flex;" +
  "  align-items: center;" +
  "  justify-content: center;" +
  "  background-color: var(--header-bg);" +
  "  border-bottom: 1px solid var(--border);" +
  "  height: 46px;" +
  "  font-weight: bold;" +
  "  -webkit-app-region: drag;" +
  "  user-select: none;" +
  "}" +
  "curica-content {" +
  "  padding: 24px;" +
  "  flex: 1;" +
  "  overflow-y: auto;" +
  "}" +
  "curica-button {" +
  "  display: inline-block;" +
  "  padding: 6px 12px;" +
  "  background-color: var(--header-bg);" +
  "  border: 1px solid var(--border);" +
  "  border-radius: 6px;" +
  "  cursor: pointer;" +
  "  font-weight: 600;" +
  "  transition: background 0.2s;" +
  "}" +
  "curica-button:hover {" +
  "  background-color: var(--border);" +
  "}" +
  "curica-button.suggested {" +
  "  background-color: var(--accent);" +
  "  color: white;" +
  "  border-color: var(--accent);" +
  "}" +
  "curica-button.suggested:hover {" +
  "  background-color: #1c71d8;" +
  "}" +
  "</style>";

function Window(options) {
    this.options = options || {};
    this.html = this.options.html || "<h1>Curica Environment</h1>";
    this.title = this.options.title || "Curica App";
    this.token = crypto.randomBytes(32).toString('hex');
    this.port = 0;

    var self = this;

    this.server = http.createServer(function(req, res) {
        var tokenQuery = "?token=" + self.token;
        if (req.url.indexOf(tokenQuery) === -1) {
            res.writeHead(403);
            res.end("403 Forbidden: Invalid Token");
            return;
        }

        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end("<!DOCTYPE html>\n" +
"<html>\n" +
"<head>\n" +
"    <title>" + self.title + "</title>\n" +
"    " + GNOME_CSS + "\n" +
"</head>\n" +
"<body>\n" +
"    " + self.html + "\n" +
"</body>\n" +
"</html>\n");
    });

    this.server.listen(0, '127.0.0.1', function() {
