# Browser-based web UI: how QLC+ serves it

QLC+ 5 can serve the web UI in the repository's `webui/` directory itself, so
a browser only needs the QLC+ host and one port - no separate web server.
The UI is plain static content (`index.html`, `api/`, `assets/`, `vendor/`,
...); it talks to QLC+ over the existing WebSocket control API
(`controlapi/`, `docs/api-spec/`). The HTTP side is `WebServer`
(`controlapi/src/webserver.{h,cpp}`): a deliberately minimal HTTP/1.1
`GET`/`HEAD` static-file server on `QTcpServer` - one request per
connection, `Connection: close`, `Cache-Control: no-cache` on everything
plus `ETag`/`Last-Modified` on files (a matching `If-None-Match` or
`If-Modified-Since` gets `304 Not Modified`, so a reload re-downloads only
what changed),
`Content-Type` by extension, `/` -> `index.html`, `403` for anything that
resolves outside the root, `404` otherwise. It also generates
`GET /qlcplus-config.json` -> `{"apiPort": <port the API actually listens
on>, "apiHost": null}` so the UI finds the WebSocket API without a
hard-coded port (`apiHost: null` means "same host the page came from").

## Flags and ports

| Flag | Meaning | Default |
| --- | --- | --- |
| `--webui` | Start the web UI's HTTP server. Implies `--api`. | off |
| `--webui-port <n>` | HTTP port. Implies `--webui`. | `9011` (`WEB_SERVER_DEFAULT_PORT`, `webserver.h`) |
| `--webui-root <dir>` | Serve this directory instead of the installed one. Implies `--webui`. | installed `WebUI` dir |
| `--api` / `--api-port <n>` | WebSocket control API the UI connects to. | `9010` (`API_SERVER_DEFAULT_PORT`, `apiserver.h`) |

Both servers bind all interfaces, like the API server always has. The flags
are `--webui*`, not `--web*`: `-w/--web` and `-wp/--web-port` already belong
to the legacy webaccess remote (port 9999) and QCommandLineParser refuses
duplicate option names. For the same reason the install directory is
`WebUI`, not `web` - `Web` is the legacy remote's file directory and the two
would be the same folder on Windows/macOS.

Installed location (`WEBUIDIR` in `variables.cmake`, installed by
`webui/CMakeLists.txt`, only when `qmlui` is ON): `C:\qlcplus\WebUI` on
Windows (next to `qlcplus5.exe`, like `Plugins\` and `Meshes\`),
`Contents/Resources/WebUI` in the macOS bundle, `share/qlcplus/webui` on
Linux. The Windows NSIS installer script lists the directory explicitly
(`platforms/windows/qlcplus5Qt6.nsi`). At runtime the default root is
resolved via `QLCFile::systemDirectory(WEBUIDIR)`, exactly like the other
data directories.

On start QLC+ logs `Web UI served from <root> on port <n>` (`qDebug`, so
visible with `-d`) plus `Web UI available at http://localhost:<n>/`
(`qInfo`). A missing root directory or a root without `index.html` is a
warning, not an error - the server still starts and the resulting 404s are
the diagnostic. A port already in use is a `qCritical` with the OS error;
pick another with `--webui-port`.

## Development workflow

Point the server straight at the source tree so UI edits need no rebuild
and no redeploy - just reload the browser (nothing is cached, see above):

```
qlcplus5.exe --webui --webui-root D:\path\to\qlcplus\webui
```

`dev-build-run.ps1` launches with `--webui` by default (after mirroring the
repository's `webui\` into `C:\qlcplus\WebUI`, deleting stale files); pass
`-WebUiRoot <dir>` to serve the source tree instead, `-NoWebUi` to skip the
web UI entirely, `-WebUiPort <n>` to move it. The server has its own QTest
suite, `controlapi/test/webserver/` (`webserver_test`, registered with
CTest like the other controlapi suites): every status code above, the
traversal cases (`/../x`, `/%2e%2e/`, backslashes), `HEAD`, the generated
config JSON and the content-type table are pinned down there - extend it
when changing the server's behaviour.
