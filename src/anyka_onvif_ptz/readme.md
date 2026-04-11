The source uses a dynamic aproach of the ipadress of the camera

You can define other setting:

- ONVIF_PORT          8081          /* ONVIF HTTP port on camera */
- DISCOVERY_PORT      3702        /* WS-Discovery UDP port */ - doesnot work well, but not needed with manual attaching to sss
- DISCOVERY_ADDR      "239.255.255.250"
- CAMERA_IP_FALLBACK  "192.168.0.80"   ( i have a fixed ip address, fallback just inc case - otherwise not used) 
- RTSP_PATH           "/vs0"
- RTSP_PORT           554

For building:

arm-anykav200-linux-uclibcgnueabi-gcc anyka_onvif_ptz.c \
 -std=c99 -D_GNU_SOURCE \
 -DVERSION="\"v2.3.0\"" \
 -lpthread -o anyka_onvif_ptz

v.2.3
- reverse ptz (swap lef/right/uip/down)
- modified broadcast ws discovery
- log file maintenance every 12 hour, if greater then 300 kb, trim forst 300 lines,  mutex_lock
 

  
