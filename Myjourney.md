# 🚀 Journey

## A Discovery Journey

It all started with buying two cheap IP cameras—just for fun.

Setting them up with the Yi IoT app on my iPhone was easy enough, but the experience quickly became frustrating. The app was full of ads and constantly pushed cloud services. Over time, it got so bad that it almost blocked normal use of the cameras.

That was the moment I decided:
**there must be a better way.**

I wanted to:

* Use the cameras without the app
* Stop them from communicating with the internet
* Take full control

---

## 🔧 Step 1: Hacking the Camera

With no documentation available, I opened one of the cameras to inspect the hardware. Not easy (I’m more clumsy than handy), but I managed.

I discovered an **Anyka chip**, which led me to a firmware hack:

👉 [https://github.com/MuhammedKalkan/Anyka-Camera-Firmware](https://github.com/MuhammedKalkan/Anyka-Camera-Firmware)

Installing it via an SD card was surprisingly simple.

**Result:**

* RTSP video streams
* Web interface for PTZ control
* Full local access

I assigned fixed IP addresses and blocked internet access.
From that moment on, the cameras were fully under my control.

---

## 💾 Step 2: Connecting to My NAS

Next step: connecting the cameras to my **Synology NAS (DS412+)** using Surveillance Station.

Since the cameras are unsupported, I configured them manually via RTSP.

**What worked:**

* Video recording on NAS
* Remote access via DS Cam
* Motion detection

**What didn’t:**

* ❌ PTZ control

---

## 🔍 Step 3: Trying ONVIF

To enable PTZ, I explored **ONVIF**.

The hacked firmware included ONVIF, but:

* It wasn’t active
* It didn’t support PTZ

I tried:

* Activating it
* Port scanning
* Experimenting with configurations

➡️ No success.

That’s when I discovered the idea of using a **proxy**.

---

## 🔄 Step 4: Building a Proxy

The concept:

* NAS connects to a *fake ONVIF camera* (proxy)
* Proxy translates PTZ commands to the real camera
* Video stream remains direct

I built a basic ONVIF listener on a **Raspberry Pi**.

**Good news:**

* PTZ commands were correctly forwarded

**Bad news:**

* Synology Surveillance Station rejected my ONVIF responses

I could see requests coming in, but my responses weren’t “good enough.”

I also tested **ONVIF Device Manager (ODM)**—same issue.

---

## 🧠 Step 5: Reverse Engineering ONVIF (ODM)

Since ODM is open source, I decided to debug it to understand correct ONVIF responses.

👉 [https://github.com/hongkilldong/odm/tree/master](https://github.com/hongkilldong/odm/tree/master)

This became a project on its own.

I:

* Migrated it to **Visual Studio 2022**
* Updated it to **.NET Framework 4.8**
* Worked through a massive solution (29 projects, multiple languages)

Eventually, I got it running:

👉 [https://github.com/Pr-Barnart/Onvif-Device-Manager-ODM-framework-4.8](https://github.com/Pr-Barnart/Onvif-Device-Manager-ODM-framework-4.8)

---

## 🐞 Step 6: Debugging

Now I could debug ODM while connecting to my proxy.

**Goal:**

* Understand expected ONVIF responses
* Identify why my implementation failed
* Make Synology recognize the proxy

I started by debugging ODM in Visual Studio.
That helped build a foundation.

Later, I switched to debugging:

* Synology Surveillance Station
* My proxy

Because I didn’t need full ONVIF—only what SS requires.

---

## 🍓 Step 7: Raspberry Pi Proxy

Finally, I achieved a working proxy on my Raspberry Pi using **Python 3**.

### Challenge

Synology only accepts one IP for ONVIF + stream.

### Solution

Used **mediamtx**:

* Pull stream from camera
* Serve it through the proxy

✅ Result:

* Camera connected to Surveillance Station
* PTZ working

👉 [https://github.com/Pr-Barnart/Anyka-onvif-proxy-Raspberry](https://github.com/Pr-Barnart/Anyka-onvif-proxy-Raspberry)

---

## 💡 Step 8: ONVIF + PTZ on SD Card Hack

With a working ONVIF implementation, I took it further.

### Problem

* No Python 3 on the camera

### Solution

* Rewrote everything in **C**
* Integrated it into the SD-card firmware startup

A completely new direction—but successful.

👉 [https://github.com/Pr-Barnart/anyka_onvif_ptz-with-synology-serveillance-station](https://github.com/Pr-Barnart/anyka_onvif_ptz-with-synology-serveillance-station)

---

## 🧭 Final Thoughts

What started as:

> “I just want to avoid ads and cloud services”

Turned into a deep dive into:

* Embedded systems
* Reverse engineering
* ONVIF protocol
* Network debugging

And honestly—that’s what makes it fun.

There’s still a long way to go…
but that’s part of the journey.

---

## 👨‍💻 About Me

I’m not a professional developer.
I’ve learned everything through experience over the years—mostly self-taught.

---

## 🛠️ Tools I Used

* FileZilla (FTP)
* PuTTY (Telnet)
* Visual Studio 2022
* Nmap / Zenmap
* ONVIF Device Manager (ODM)
* ILSpy (DLL inspection)
* Wireshark (network analysis)
* WSL

---

