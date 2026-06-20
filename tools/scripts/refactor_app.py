import re

# Refactor app.js
with open("tools/feature_manager_web/app.js", "r") as f:
    app = f.read()

# Remove myTrim
app = re.sub(r'function myTrim\(s\) \{.*?\n\}\n', '', app, flags=re.DOTALL)
# Remove myDecodeURIComponent
app = re.sub(r'function myDecodeURIComponent\(str\) \{.*?\n\}\n', '', app, flags=re.DOTALL)

# Replace usages
app = app.replace('var key = myTrim(kStr);', 'var key = kStr.trim();')
app = app.replace('var val = myTrim(line.substring(colon + 1));', 'var val = line.substring(colon + 1).trim();')
app = app.replace('p = myDecodeURIComponent(p);', 'p = decodeURIComponent(p);')

with open("tools/feature_manager_web/app.js", "w") as f:
    f.write(app)

# Refactor url.js
with open("src/js/url.js", "r") as f:
    url_js = f.read()

url_js = re.sub(r'function decodeURIComponent\(str\) \{.*?\n\}\n\n', '', url_js, flags=re.DOTALL)
url_js = re.sub(r'function encodeURIComponent\(str\) \{.*?\n\}\n\n', '', url_js, flags=re.DOTALL)

with open("src/js/url.js", "w") as f:
    f.write(url_js)

