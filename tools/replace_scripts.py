import sys

with open('src/builtins.c', 'r') as f:
    content = f.read()

# Replace scripts_ prefix with src_js_ for JS headers
content = content.replace('scripts_path_js', 'src_js_path_js')
content = content.replace('scripts_url_js', 'src_js_url_js')
content = content.replace('scripts_http_js', 'src_js_http_js')
content = content.replace('scripts_stream_js', 'src_js_stream_js')
content = content.replace('scripts_events_js', 'src_js_events_js')
content = content.replace('scripts_child_process_js', 'src_js_child_process_js')
content = content.replace('scripts_webview_js', 'src_js_webview_js')
content = content.replace('scripts_tty_js', 'src_js_tty_js')
content = content.replace('scripts_readline_js', 'src_js_readline_js')
content = content.replace('scripts_dgram_js', 'src_js_dgram_js')
content = content.replace('scripts_zlib_js', 'src_js_zlib_js')
content = content.replace('scripts_fetch_js', 'src_js_fetch_js')
content = content.replace('scripts_test_runner_js', 'src_js_test_runner_js')

with open('src/builtins.c', 'w') as f:
    f.write(content)
print("Updated src/builtins.c")
