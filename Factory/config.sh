#!/bin/sh

export LD_LIBRARY_PATH=/mnt/Factory/custom/lib

/usr/sbin/net_manage.sh &
telnetd &
ak_adec_demo 16000 2 aac /usr/share/audio_file/common/didi.aac
/mnt/Factory/custom/ptz_daemon & 
/mnt/Factory/custom/busybox httpd -p 8080 -h /mnt/Factory/custom/web_interface/www &

sleep 30

/mnt/Factory/custom/rtsp &
/mnt/Factory/custom/vpn/vpn.sh &

# ================================
# ONVIF SERVICE
# ================================

ONVIF_BIN="/mnt/Factory/custom/anyka_onvif_ptz"

if [ -f "$ONVIF_BIN" ]; then
    chmod +x "$ONVIF_BIN"

    # kill oude instantie (veilig bij reboot/restart)
    killall anyka_onvif_ptz 2>/dev/null

    echo "[ONVIF] starting..." > /tmp/onvif.log

    "$ONVIF_BIN" >> /tmp/onvif.log 2>&1 &

    sleep 1

    if ps | grep anyka_onvif_ptz | grep -v grep >/dev/null; then
        echo "[ONVIF] started OK" >> /tmp/onvif.log
    else
        echo "[ONVIF] FAILED" >> /tmp/onvif.log
    fi
else
    echo "[ONVIF] binary not found" > /tmp/onvif.log
fi


#!/bin/sh

export LD_LIBRARY_PATH=/mnt/Factory/custom/lib

/usr/sbin/net_manage.sh &
telnetd &
ak_adec_demo 16000 2 aac /usr/share/audio_file/common/didi.aac
/mnt/Factory/custom/ptz_daemon &
/mnt/Factory/custom/busybox httpd -p 8080 -h /mnt/Factory/custom/web_interface/www &

sleep 30

/mnt/Factory/custom/rtsp &
/mnt/Factory/custom/vpn/vpn.sh &

# ================================
# ONVIF SERVICE
# ================================

ONVIF_BIN="/mnt/Factory/custom/anyka_onvif_ptz"
ONVIF_LOG_DIR="/mnt/Factory/custom/log"
ONVIF_LOG="$ONVIF_LOG_DIR/onvif.log"

mkdir -p "$ONVIF_LOG_DIR"

if [ -f "$ONVIF_BIN" ]; then
    chmod +x "$ONVIF_BIN"

    # kill oude instantie (veilig bij reboot/restart)
    killall anyka_onvif_ptz 2>/dev/null

    echo "============================" >> "$ONVIF_LOG"
    echo "[ONVIF] starting..." >> "$ONVIF_LOG"

    "$ONVIF_BIN" >> "$ONVIF_LOG" 2>&1 &

    sleep 1

    if ps | grep anyka_onvif_ptz | grep -v grep >/dev/null; then
        echo "[ONVIF] started OK" >> "$ONVIF_LOG"
    else
        echo "[ONVIF] FAILED" >> "$ONVIF_LOG"
    fi
else
    echo "[ONVIF] binary not found" >> "$ONVIF_LOG"
fi