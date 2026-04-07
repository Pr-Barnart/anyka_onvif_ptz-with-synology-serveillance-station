This repository is based on the sd- hack repository from https://github.com/MuhammedKalkan/Anyka-Camera-Firmware.

I wanted them to connect to my old synology nas (ds412+) serveillance station (ss), but the orginale repo did nt not have onvif with ptz.

For me as a newbie it was the start of a complete journe y en ding with an working onvif with ptz for my ss synology.

# SD card hack:
if you have an anyka based ip-camerayou:
- just copy the factory directory to the sd card.
- Insert and restart the camera.

The hack provides you
- telnet,
- ssh,
- ptzdeamon,
- rtps-stream on rtps://CAMERA_IP:554/vs0
- and  also a onvif service with ptz (port8081)
Logging from the onvif is send to  factopry/custom/log/onvif.log

# Connecting to SS Synology
- start serveillance station
- select ip-camera from the menu
- select add => add camera
Add camera:
  - quick install -> next
  - name: YOUR  CHOICE
  - IP_address: CAMERA_IP
  - Port: 8081
  - Brand [ONVIF]
  - Model: all functions
  - UserName: blank
  - Password: blank

- click test connection: if OK -> next
- click complete.

From https://github.com/MuhammedKalkan/Anyka-Camera-Firmware the only changes I made:
- new config.sh in factory
- add anyka_onvif_ptz to custom
- add ss.jpg to custom ( snapshot dummy)
- create custom/log for logging
- deleted custom/onvif

# More info about the hack itself and how to overwrite Camera software
see https://github.com/MuhammedKalkan/Anyka-Camera-Firmware
