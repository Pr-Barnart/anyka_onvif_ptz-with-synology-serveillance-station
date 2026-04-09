# 📷 ANYKA Camera SD Hack with ONVIF + PTZ connected to SDynology Surveillance Station

This repository is based on the excellent work from:
👉 [https://github.com/MuhammedKalkan/Anyka-Camera-Firmware](https://github.com/MuhammedKalkan/Anyka-Camera-Firmware)

---

## 🎯 Goal

I wanted to connect my cheap **ANYKA-based IP cameras** to my old **Synology DS412+ Surveillance Station (SS)**.

The original repository provides a great SD-card hack, but it **does not include ONVIF with PTZ support**, which is required for full integration with Surveillance Station.

As a beginner, this turned into quite a journey — but I finally ended up with a **working ONVIF implementation including PTZ**, fully compatible with Synology 🎉

---

## 💾 SD Card Hack

If you have an ANYKA-based IP camera, setup is simple:

### Installation

1. Copy the `factory` directory to an SD card
2. Insert the SD card into the camera
3. Restart the camera

---

## ⚙️ Features Provided by the Hack

After boot, the camera provides:

* ✅ Telnet access
* ✅ SSH access
* ✅ PTZ daemon (`ptzdaemon`)
* ✅ RTSP stream

  ```
  rtsp://CAMERA_IP:554/vs0
  ```
* ✅ **ONVIF service with PTZ support**

  ```
  http://CAMERA_IP:8081
  ```

### 📝 Logging

ONVIF logs are written to:

```id="w36dn4"
factory/custom/log/onvif.log
```

---

## 📺 Connecting to Synology Surveillance Station

1. Open **Surveillance Station**
2. Go to **IP Camera**
3. Click **Add → Add Camera**

### Configuration

| Setting      | Value         |
| ------------ | ------------- |
| Install Type | Quick Install |
| Name         | Your choice   |
| IP Address   | CAMERA_IP     |
| Port         | 8081          |
| Brand        | ONVIF         |
| Model        | All Functions |
| Username     | *(empty)*     |
| Password     | *(empty)*     |

### Steps

* Click **Test Connection**
* If successful → **Next**
* Click **Complete**

---

## 🔧 Changes Compared to Original Repository

From the original project, I made the following modifications:

* ➕ Added new `config.sh` in `factory`
* ➕ Added `anyka_onvif_ptz` to `custom`
* ➕ Added `ss.jpg` (dummy snapshot for Surveillance Station)
* ➕ Created `custom/log` directory for logging
* ❌ Removed `custom/onvif`

---

## 📚 More Information

For full details about:

* the SD hack
* firmware behavior
* flashing / overriding camera software

See the original repository:

👉 [https://github.com/MuhammedKalkan/Anyka-Camera-Firmware](https://github.com/MuhammedKalkan/Anyka-Camera-Firmware)

* Anyka onvif ptz on raspberry-pi proxy
  
👉 [https://github.com/Pr-Barnart/Anyka-onvif-proxy-Raspberry](https://github.com/Pr-Barnart/Anyka-onvif-proxy-Raspberry)

* Onvif Device Manager miration vs-2022
  
👉 [https://github.com/Pr-Barnart/Onvif-Device-Manager-ODM-vs2022-framework-4.8)](https://github.com/Pr-Barnart/Onvif-Device-Manager-ODM-vs2022-framework-4.8)

---

## ✅ Result

With these changes, cheap ANYKA cameras can now:

* integrate with Synology Surveillance Station
* support ONVIF properly (for SS only) 
* provide PTZ control
---
