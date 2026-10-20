# Widevine / DRM test (0.80 investigation)
`drm.html` plays Shaka's public Widevine stream (angel-one-widevine, audio and video encrypted, license
`cwip-shaka-proxy.appspot.com/no_auth`) and reports EME support. Serve this folder on localhost (EME needs a secure
context): `python3 -m http.server 8779 --bind 127.0.0.1`, then
`./run.sh NAME /path/to/chrome headless|xvfb` - navigates by CDP, waits 20 s, prints the player state, saves
`Page.captureScreenshot` and the last `Page.startScreencast` frame and their mean/stddev (black = 0).
Chromium without a CDM: `PREP='mkdir -p $D/WidevineCdm && echo "{\"Path\":\"/opt/google/chrome/WidevineCdm\"}" > $D/WidevineCdm/latest-component-updated-widevine-cdm'`.
