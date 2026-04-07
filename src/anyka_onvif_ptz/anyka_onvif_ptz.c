/*
 * v 2.1.0 
 * Standalone ONVIF Server with PTZ for Anyka cameras
 * Replaces libonvif dependency — pure C, no external ONVIF libs needed
 *
 * Compile with Anyka toolchain:
 *   arm-anykav200-linux-uclibcgnueabi-gcc anyka_onvif_ptz.c \
 *     -std=c99 -D_GNU_SOURCE -lpthread -o anyka_onvif_ptz
 *
 * Or for testing on Linux x86:
 *   gcc anyka_onvif_ptz.c -std=c99 -D_GNU_SOURCE -lpthread -o anyka_onvif_ptz
 *
 * Usage on camera (SD card method):
 *   Copy to /mnt/auto/ and add to run.sh:
 *   ./anyka_onvif_ptz &
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>

/* ===================== CONFIG ===================== */
#define ONVIF_PORT          8081          /* ONVIF HTTP port on camera */
#define DISCOVERY_PORT      3702        /* WS-Discovery UDP port */
#define DISCOVERY_ADDR      "239.255.255.250"
#define CAMERA_IP_FALLBACK  "192.168.0.81"
#define RTSP_PATH           "/vs0"
#define RTSP_PORT           554

/* PTZ commands — match your camera's webui */
#define PTZ_CMD_LEFT        "ptzl"
#define PTZ_CMD_RIGHT       "ptzr"
#define PTZ_CMD_UP          "ptzu"
#define PTZ_CMD_DOWN        "ptzd"
#define PTZ_CMD_LEFT_UP     "ptzlu"
#define PTZ_CMD_RIGHT_UP    "ptzru"
#define PTZ_CMD_LEFT_DOWN   "ptzld"
#define PTZ_CMD_RIGHT_DOWN  "ptzrd"

/* Webui base URL for PTZ — adjust port if needed */
#define PTZ_WEBUI_PORT      8080
#define PTZ_WEBUI_CMD       "http://127.0.0.1:%d/cgi-bin/webui?command=%s"

/* Buffer sizes */
#define BUF_SIZE            32768
#define SMALL_BUF           4096
#define UUID_STR            "316d4de6-a7b4-438b-8374-aabbccddeeff"

/* Snapshot path */
#define SNAPSHOT_PATH "/mnt/Factory/custom/snapshot.jpg"

/* ===================== GLOBALS ===================== */
static char camera_ip[64] = CAMERA_IP_FALLBACK;
static int  server_running = 1;
/* ===================== PROFILE STATE ===================== */

static char active_profile_token[64] = "Profile_1";
static char active_profile_name[64]  = "MainStream";
static int  active_profile_fixed     = 1;   // default profile
static int profile_has_video_source = 1;
static int profile_has_video_encoder = 1;
static int profile_has_ptz = 1;
/* ===================== UTILS ===================== */

/* Get local IP of wlan0 or eth0 */
static void get_local_ip(char *ip_out, size_t len)
{
    struct ifaddrs *ifaddr, *ifa;
    strcpy(ip_out, CAMERA_IP_FALLBACK);

    if (getifaddrs(&ifaddr) == -1)
        return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        strncpy(ip_out, inet_ntoa(sa->sin_addr), len - 1);
        /* prefer wlan0 */
        if (strcmp(ifa->ifa_name, "wlan0") == 0)
            break;
    }
    freeifaddrs(ifaddr);
}

static int xml_has_op(const char *xml, const char *op)
{
    char pat1[128], pat2[128], pat3[128], pat4[128];

    snprintf(pat1, sizeof(pat1), "<%s", op);
    snprintf(pat2, sizeof(pat2), ":%s", op);
    snprintf(pat3, sizeof(pat3), "</%s>", op);
    snprintf(pat4, sizeof(pat4), ":%s>", op);

    return strstr(xml, pat1) != NULL ||
           strstr(xml, pat2) != NULL ||
           strstr(xml, pat3) != NULL ||
           strstr(xml, pat4) != NULL;
}

/* Simple string search */
static int str_contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

/* Execute PTZ command via webui */
/*static void ptz_execute(const char* cmd)
{
    char url[256];
    char wget_cmd[512];
    snprintf(url, sizeof(url), PTZ_WEBUI_CMD, PTZ_WEBUI_PORT, cmd);
    snprintf(wget_cmd, sizeof(wget_cmd), "wget -q -O /dev/null '%s' &", url);
    printf("[PTZ] %s\n", url);
    fflush(stdout);
    system(wget_cmd);
}
*/
static void ptz_execute(const char* cmd)
{
    char syscmd[512];

    printf("[PTZ] direct webui command=%s\n", cmd);
    fflush(stdout);

    snprintf(syscmd, sizeof(syscmd),
        "REQUEST_METHOD=GET QUERY_STRING='command=%s' "
        "/mnt/Factory/custom/web_interface/www/cgi-bin/webui "
        ">/dev/null 2>&1 &",
        cmd);

    system(syscmd);
}
/* Parse PanTilt x/y from ContinuousMove XML */
static void parse_and_execute_ptz(const char *xml)
{
    float x = 0.0f, y = 0.0f;
    const char *px = strstr(xml, "PanTilt");
    if (!px) {
        printf("[PTZ] No PanTilt found\n");
        return;
    }

    const char *xattr = strstr(px, "x=\"");
    const char *yattr = strstr(px, "y=\"");
    if (xattr) x = atof(xattr + 3);
    if (yattr) y = atof(yattr + 3);

    printf("[PTZ] x=%.2f y=%.2f\n", x, y);

    if      (x < 0 && y > 0) ptz_execute(PTZ_CMD_LEFT_UP);
    else if (x > 0 && y > 0) ptz_execute(PTZ_CMD_RIGHT_UP);
    else if (x < 0 && y < 0) ptz_execute(PTZ_CMD_LEFT_DOWN);
    else if (x > 0 && y < 0) ptz_execute(PTZ_CMD_RIGHT_DOWN);
    else if (x < 0)           ptz_execute(PTZ_CMD_LEFT);
    else if (x > 0)           ptz_execute(PTZ_CMD_RIGHT);
    else if (y > 0)           ptz_execute(PTZ_CMD_UP);
    else if (y < 0)           ptz_execute(PTZ_CMD_DOWN);
    else printf("[PTZ] x=0 y=0, no movement\n");
}

/* ===================== SOAP RESPONSES ===================== */

static const char *SOAP_HEADER =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/soap+xml; charset=utf-8\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n\r\n";

static const char *ENV_OPEN =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\""
    " xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\""
    " xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\""
    " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\""
    " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
    "<s:Body>";

static const char *ENV_CLOSE = "</s:Body></s:Envelope>";

/* Send SOAP response */
static void send_soap(int fd, const char *body)
{
    char header[256];
    int body_len = strlen(ENV_OPEN) + strlen(body) + strlen(ENV_CLOSE);
    char *full = malloc(body_len + 16);
    if (!full) return;

    sprintf(full, "%s%s%s", ENV_OPEN, body, ENV_CLOSE);
    int full_len = strlen(full);
    snprintf(header, sizeof(header), SOAP_HEADER, full_len);

    send(fd, header, strlen(header), 0);
    send(fd, full, full_len, 0);
    free(full);
}
static void send_profile_element_response(int fd,
                                          const char *response_tag,
                                          const char *profile_tag,
                                          const char *token,
                                          const char *name,
                                          const char *fixed)
{
    char body[8192];
    int n = 0;

    n += snprintf(body + n, sizeof(body) - n,
        "<%s>"
        "<%s token=\"%s\" fixed=\"%s\">"
        "<tt:Name>%s</tt:Name>",
        response_tag, profile_tag, token, fixed, name);

    if (profile_has_video_source) {
        n += snprintf(body + n, sizeof(body) - n,
            "<tt:VideoSourceConfiguration token=\"VSC_1\">"
            "<tt:Name>VideoSourceConfig</tt:Name>"
            "<tt:UseCount>1</tt:UseCount>"
            "<tt:SourceToken>VideoSource_1</tt:SourceToken>"
            "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
            "</tt:VideoSourceConfiguration>");
    }

    if (profile_has_video_encoder) {
        n += snprintf(body + n, sizeof(body) - n,
            "<tt:VideoEncoderConfiguration token=\"VEC_1\">"
            "<tt:Name>VideoEncoderConfig</tt:Name>"
            "<tt:UseCount>1</tt:UseCount>"
            "<tt:Encoding>H264</tt:Encoding>"
            "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
            "<tt:Quality>5</tt:Quality>"
            "<tt:RateControl>"
            "<tt:FrameRateLimit>25</tt:FrameRateLimit>"
            "<tt:EncodingInterval>1</tt:EncodingInterval>"
            "<tt:BitrateLimit>4096</tt:BitrateLimit>"
            "</tt:RateControl>"
            "</tt:VideoEncoderConfiguration>");
    }

    if (profile_has_ptz) {
        n += snprintf(body + n, sizeof(body) - n,
            "<tt:PTZConfiguration token=\"PTZConfig_1\">"
            "<tt:Name>PTZConfig</tt:Name>"
            "<tt:UseCount>1</tt:UseCount>"
            "<tt:NodeToken>PTZNode_1</tt:NodeToken>"
            "<tt:DefaultAbsolutePantTiltPositionSpace>"
            "http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace"
            "</tt:DefaultAbsolutePantTiltPositionSpace>"
            "<tt:DefaultContinuousPanTiltVelocitySpace>"
            "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace"
            "</tt:DefaultContinuousPanTiltVelocitySpace>"
            "<tt:DefaultPTZSpeed>"
            "<tt:PanTilt x=\"0.5\" y=\"0.5\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/>"
            "</tt:DefaultPTZSpeed>"
            "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
            "</tt:PTZConfiguration>");
    }

    n += snprintf(body + n, sizeof(body) - n,
        "</%s>"
        "</%s>",
        profile_tag, response_tag);

    send_soap(fd, body);
}



/* ===================== REQUEST HANDLERS ===================== */

static void handle_GetCapabilities(int fd)
{
    char body[2048];
    snprintf(body, sizeof(body),
        "<tds:GetCapabilitiesResponse>"
        "<tds:Capabilities>"
        "<tt:Device>"
        "<tt:XAddr>http://%s:%d/onvif/device_service</tt:XAddr>"
        "<tt:System>"
        "<tt:SupportedVersions><tt:Major>2</tt:Major><tt:Minor>0</tt:Minor></tt:SupportedVersions>"
        "</tt:System>"
        "</tt:Device>"
        "<tt:Media>"
        "<tt:XAddr>http://%s:%d/onvif/media_service</tt:XAddr>"
        "<tt:StreamingCapabilities>"
        "<tt:RTP_TCP>true</tt:RTP_TCP>"
        "<tt:RTP_RTSP_TCP>true</tt:RTP_RTSP_TCP>"
        "</tt:StreamingCapabilities>"
        "</tt:Media>"
        "<tt:PTZ>"
        "<tt:XAddr>http://%s:%d/onvif/ptz_service</tt:XAddr>"
        "</tt:PTZ>"
        "</tds:Capabilities>"
        "</tds:GetCapabilitiesResponse>",
        camera_ip, ONVIF_PORT,
        camera_ip, ONVIF_PORT,
        camera_ip, ONVIF_PORT);
    send_soap(fd, body);
}

static void handle_GetServices(int fd)
{
    char body[2048];
    snprintf(body, sizeof(body),
        "<tds:GetServicesResponse>"
        "<tds:Service>"
        "<tds:Namespace>http://www.onvif.org/ver10/device/wsdl</tds:Namespace>"
        "<tds:XAddr>http://%s:%d/onvif/device_service</tds:XAddr>"
        "<tds:Version><tds:Major>2</tds:Major><tds:Minor>0</tds:Minor></tds:Version>"
        "</tds:Service>"
        "<tds:Service>"
        "<tds:Namespace>http://www.onvif.org/ver10/media/wsdl</tds:Namespace>"
        "<tds:XAddr>http://%s:%d/onvif/media_service</tds:XAddr>"
        "<tds:Version><tds:Major>2</tds:Major><tds:Minor>0</tds:Minor></tds:Version>"
        "</tds:Service>"
        "<tds:Service>"
        "<tds:Namespace>http://www.onvif.org/ver20/ptz/wsdl</tds:Namespace>"
        "<tds:XAddr>http://%s:%d/onvif/ptz_service</tds:XAddr>"
        "<tds:Version><tds:Major>2</tds:Major><tds:Minor>0</tds:Minor></tds:Version>"
        "</tds:Service>"
        "</tds:GetServicesResponse>",
        camera_ip, ONVIF_PORT,
        camera_ip, ONVIF_PORT,
        camera_ip, ONVIF_PORT);
    send_soap(fd, body);
}

static void handle_GetDeviceInformation(int fd)
{
    const char *body =
        "<tds:GetDeviceInformationResponse>"
        "<tds:Manufacturer>Anyka</tds:Manufacturer>"
        "<tds:Model>AK3918</tds:Model>"
        "<tds:FirmwareVersion>2.0</tds:FirmwareVersion>"
        "<tds:SerialNumber>AK3918-001</tds:SerialNumber>"
        "<tds:HardwareId>AK3918v200</tds:HardwareId>"
        "</tds:GetDeviceInformationResponse>";
    send_soap(fd, body);
}

static void handle_GetSystemDateAndTime(int fd)
{
    const char *body =
        "<tds:GetSystemDateAndTimeResponse>"
        "<tds:SystemDateAndTime>"
        "<tt:DateTimeType>NTP</tt:DateTimeType>"
        "<tt:DaylightSavings>false</tt:DaylightSavings>"
        "<tt:TimeZone><tt:TZ>UTC</tt:TZ></tt:TimeZone>"
        "</tds:SystemDateAndTime>"
        "</tds:GetSystemDateAndTimeResponse>";
    send_soap(fd, body);
}

static void handle_GetScopes(int fd)
{
    const char *body =
        "<tds:GetScopesResponse>"
        "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/name/AnykaCamera</tt:ScopeItem></tds:Scopes>"
        "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/type/video_encoder</tt:ScopeItem></tds:Scopes>"
        "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/type/ptz</tt:ScopeItem></tds:Scopes>"
        "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/Profile/Streaming</tt:ScopeItem></tds:Scopes>"
        "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/manufacturer/Anyka</tt:ScopeItem></tds:Scopes>"
        "</tds:GetScopesResponse>";
    send_soap(fd, body);
}

static void handle_GetNetworkInterfaces(int fd)
{
    char body[1024];
    snprintf(body, sizeof(body),
        "<tds:GetNetworkInterfacesResponse>"
        "<tds:NetworkInterfaces token=\"wlan0\">"
        "<tt:Enabled>true</tt:Enabled>"
        "<tt:Info><tt:Name>wlan0</tt:Name><tt:MTU>1500</tt:MTU></tt:Info>"
        "<tt:IPv4><tt:Enabled>true</tt:Enabled>"
        "<tt:Config><tt:Manual>"
        "<tt:Address>%s</tt:Address>"
        "<tt:PrefixLength>24</tt:PrefixLength>"
        "</tt:Manual></tt:Config></tt:IPv4>"
        "</tds:NetworkInterfaces>"
        "</tds:GetNetworkInterfacesResponse>",
        camera_ip);
    send_soap(fd, body);
}
static void handle_GetProfiles(int fd)
{
    printf("[ONVIF] handle_GetProfiles token=%s name=%s fixed=%d\n",
           active_profile_token, active_profile_name, active_profile_fixed);

    send_profile_element_response(fd,
        "trt:GetProfilesResponse",
        "trt:Profiles",
        active_profile_token,
        active_profile_name,
        active_profile_fixed ? "true" : "false");
}

static void handle_GetStreamUri(int fd)
{
    char body[1024];
    printf("[ONVIF] handle_GetStreamUri called\n");
    snprintf(body, sizeof(body),
        "<trt:GetStreamUriResponse>"
        "<trt:MediaUri>"
        "<tt:Uri>rtsp://%s:%d%s</tt:Uri>"
        "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
        "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
        "<tt:Timeout>PT60S</tt:Timeout>"
        "</trt:MediaUri>"
        "</trt:GetStreamUriResponse>",
        camera_ip, RTSP_PORT, RTSP_PATH);
    send_soap(fd, body);
}

static void handle_GetSnapshotUri(int fd)
{
    char body[512];
    snprintf(body, sizeof(body),
        "<trt:GetSnapshotUriResponse>"
        "<trt:MediaUri>"
        "<tt:Uri>http://%s:%d/snapshot.jpg</tt:Uri>"
        "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
        "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
        "<tt:Timeout>PT60S</tt:Timeout>"
        "</trt:MediaUri>"
        "</trt:GetSnapshotUriResponse>",
        camera_ip, ONVIF_PORT);
    send_soap(fd, body);
}

static void handle_GetNodes(int fd)
{
    const char *body =
        "<tptz:GetNodesResponse>"
        "<tptz:PTZNode token=\"PTZNode_1\" FixedHomePosition=\"false\">"
        "<tt:Name>PTZNode</tt:Name>"
        "<tt:SupportedPTZSpaces>"
        "<tt:AbsolutePanTiltPositionSpace>"
        "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI>"
        "<tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
        "<tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange>"
        "</tt:AbsolutePanTiltPositionSpace>"
        "<tt:ContinuousPanTiltVelocitySpace>"
        "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI>"
        "<tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
        "<tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange>"
        "</tt:ContinuousPanTiltVelocitySpace>"
        "<tt:PanTiltSpeedSpace>"
        "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace</tt:URI>"
        "<tt:XRange><tt:Min>0</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
        "</tt:PanTiltSpeedSpace>"
        "</tt:SupportedPTZSpaces>"
        "<tt:MaximumNumberOfPresets>0</tt:MaximumNumberOfPresets>"
        "<tt:HomeSupported>false</tt:HomeSupported>"
        "</tptz:PTZNode>"
        "</tptz:GetNodesResponse>";
    send_soap(fd, body);
}

static void handle_GetConfigurations(int fd)
{
    const char *body =
        "<tptz:GetConfigurationsResponse>"
        "<tptz:PTZConfiguration token=\"PTZConfig_1\">"
        "<tt:Name>PTZConfig</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:NodeToken>PTZNode_1</tt:NodeToken>"
        "<tt:DefaultAbsolutePantTiltPositionSpace>"
        "http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace"
        "</tt:DefaultAbsolutePantTiltPositionSpace>"
        "<tt:DefaultContinuousPanTiltVelocitySpace>"
        "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace"
        "</tt:DefaultContinuousPanTiltVelocitySpace>"
        "<tt:DefaultPTZSpeed>"
        "<tt:PanTilt x=\"0.5\" y=\"0.5\""
        " space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/>"
        "</tt:DefaultPTZSpeed>"
        "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
        "</tptz:PTZConfiguration>"
        "</tptz:GetConfigurationsResponse>";
    send_soap(fd, body);
}

static void handle_GetConfigurationOptions(int fd)
{
    const char *body =
        "<tptz:GetConfigurationOptionsResponse>"
        "<tptz:PTZConfigurationOptions>"
        "<tt:Spaces>"
        "<tt:AbsolutePanTiltPositionSpace>"
        "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI>"
        "<tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
        "<tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange>"
        "</tt:AbsolutePanTiltPositionSpace>"
        "<tt:ContinuousPanTiltVelocitySpace>"
        "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI>"
        "<tt:XRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
        "<tt:YRange><tt:Min>-1</tt:Min><tt:Max>1</tt:Max></tt:YRange>"
        "</tt:ContinuousPanTiltVelocitySpace>"
        "<tt:PanTiltSpeedSpace>"
        "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace</tt:URI>"
        "<tt:XRange><tt:Min>0</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
        "</tt:PanTiltSpeedSpace>"
        "</tt:Spaces>"
        "<tt:PTZTimeout><tt:Min>PT1S</tt:Min><tt:Max>PT60S</tt:Max></tt:PTZTimeout>"
        "</tptz:PTZConfigurationOptions>"
        "</tptz:GetConfigurationOptionsResponse>";
    send_soap(fd, body);
}

static void handle_GetServiceCapabilities(int fd)
{
    const char *body =
        "<trt:GetServiceCapabilitiesResponse>"
        "<trt:Capabilities>"
        "<trt:ProfileCapabilities MaximumNumberOfProfiles=\"4\"/>"
        "<trt:StreamingCapabilities RTPMulticast=\"false\""
        " RTP_TCP=\"true\" RTP_RTSP_TCP=\"true\"/>"
        "</trt:Capabilities>"
        "</trt:GetServiceCapabilitiesResponse>";
    send_soap(fd, body);
}

static void handle_GetVideoSources(int fd)
{
    const char *body =
        "<trt:GetVideoSourcesResponse>"
        "<trt:VideoSources token=\"VideoSource_1\">"
        "<tt:Framerate>25</tt:Framerate>"
        "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
        "</trt:VideoSources>"
        "</trt:GetVideoSourcesResponse>";
    send_soap(fd, body);
}

static void handle_GetVideoSourceConfigurations(int fd)
{
    const char *body =
        "<trt:GetVideoSourceConfigurationsResponse>"
        "<trt:Configurations token=\"VSC_1\">"
        "<tt:Name>VideoSourceConfig</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:SourceToken>VideoSource_1</tt:SourceToken>"
        "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
        "</trt:Configurations>"
        "</trt:GetVideoSourceConfigurationsResponse>";
    send_soap(fd, body);
}

static void handle_GetVideoSourceConfigurationOptions(int fd)
{
    const char *body =
        "<trt:GetVideoSourceConfigurationOptionsResponse>"
        "<trt:Options>"
        "<tt:BoundsRange>"
        "<tt:XRange><tt:Min>0</tt:Min><tt:Max>0</tt:Max></tt:XRange>"
        "<tt:YRange><tt:Min>0</tt:Min><tt:Max>0</tt:Max></tt:YRange>"
        "<tt:WidthRange><tt:Min>1920</tt:Min><tt:Max>1920</tt:Max></tt:WidthRange>"
        "<tt:HeightRange><tt:Min>1080</tt:Min><tt:Max>1080</tt:Max></tt:HeightRange>"
        "</tt:BoundsRange>"
        "<tt:VideoSourceTokensAvailable>VideoSource_1</tt:VideoSourceTokensAvailable>"
        "</trt:Options>"
        "</trt:GetVideoSourceConfigurationOptionsResponse>";
    send_soap(fd, body);
}


static void handle_GetVideoEncoderConfigurations(int fd)
{
	if (!profile_has_video_encoder) {
        const char *body =
            "<trt:GetVideoEncoderConfigurationsResponse/>";  /* leeg */
        send_soap(fd, body);
        return;
    }

    const char *body =
        "<trt:GetVideoEncoderConfigurationsResponse>"
        "<trt:Configurations token=\"VEC_1\">"
        "<tt:Name>VideoEncoderConfig</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:Encoding>H264</tt:Encoding>"
        "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
        "<tt:Quality>5</tt:Quality>"
        "<tt:RateControl>"
        "<tt:FrameRateLimit>25</tt:FrameRateLimit>"
        "<tt:EncodingInterval>1</tt:EncodingInterval>"
        "<tt:BitrateLimit>4096</tt:BitrateLimit>"
        "</tt:RateControl>"
        "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>"
        "</trt:Configurations>"
        "</trt:GetVideoEncoderConfigurationsResponse>";
    send_soap(fd, body);
}
static void handle_GetVideoEncoderConfiguration(int fd)
{
    if (!profile_has_video_encoder) {
        send_soap(fd,
            "<s:Fault>"
            "<s:Code><s:Value>s:Sender</s:Value></s:Code>"
            "<s:Reason><s:Text>VideoEncoderConfiguration not present</s:Text></s:Reason>"
            "</s:Fault>");
        return;
    }

    const char *body =
        "<trt:GetVideoEncoderConfigurationResponse>"
        "<trt:Configuration token=\"VEC_1\">"
        "<tt:Name>VideoEncoderConfig</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:Encoding>H264</tt:Encoding>"
        "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
        "<tt:Quality>5</tt:Quality>"
        "<tt:RateControl>"
        "<tt:FrameRateLimit>25</tt:FrameRateLimit>"
        "<tt:EncodingInterval>1</tt:EncodingInterval>"
        "<tt:BitrateLimit>4096</tt:BitrateLimit>"
        "</tt:RateControl>"
        "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>"
        "</trt:Configuration>"
        "</trt:GetVideoEncoderConfigurationResponse>";
    send_soap(fd, body);
}
static void handle_GetVideoEncoderConfigurationOptions(int fd)
{
    const char *body =
        "<trt:GetVideoEncoderConfigurationOptionsResponse>"
        "<trt:Options>"
        "<tt:QualityRange><tt:Min>1</tt:Min><tt:Max>10</tt:Max></tt:QualityRange>"
        "<tt:H264>"
        "<tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable>"
        "<tt:ResolutionsAvailable><tt:Width>1280</tt:Width><tt:Height>720</tt:Height></tt:ResolutionsAvailable>"
        "<tt:GovLengthRange><tt:Min>1</tt:Min><tt:Max>60</tt:Max></tt:GovLengthRange>"
        "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>25</tt:Max></tt:FrameRateRange>"
        "<tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:EncodingIntervalRange>"
        "<tt:H264ProfilesSupported>Main</tt:H264ProfilesSupported>"
        "</tt:H264>"
        "</trt:Options>"
        "</trt:GetVideoEncoderConfigurationOptionsResponse>";
    send_soap(fd, body);
}
static void handle_GetAudioEncoderConfigurationOptions(int fd)
{
    const char *body =
        "<trt:GetAudioEncoderConfigurationOptionsResponse>"
        "<trt:Options/>"
        "</trt:GetAudioEncoderConfigurationOptionsResponse>";
    send_soap(fd, body);
}

static void handle_GetGuaranteedNumberOfVideoEncoderInstances(int fd)
{
    const char *body =
        "<trt:GetGuaranteedNumberOfVideoEncoderInstancesResponse>"
        "<trt:TotalNumber>4</trt:TotalNumber>"
        "<trt:H264>4</trt:H264>"
        "</trt:GetGuaranteedNumberOfVideoEncoderInstancesResponse>";
    send_soap(fd, body);
}

/* Generic profile XML reused by CreateProfile/GetProfile */
static void send_profile_response(int fd, const char *tag,
                                   const char *token, const char *name,
                                   const char *fixed)
{
    char body[4096];
    snprintf(body, sizeof(body),
        "<%s>"
        "<trt:Profiles token=\"%s\" fixed=\"%s\">"
        "<tt:Name>%s</tt:Name>"
        "<tt:VideoSourceConfiguration token=\"VSC_1\">"
        "<tt:Name>VideoSourceConfig</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:SourceToken>VideoSource_1</tt:SourceToken>"
        "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
        "</tt:VideoSourceConfiguration>"
        "<tt:VideoEncoderConfiguration token=\"VEC_1\">"
        "<tt:Name>VideoEncoderConfig</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:Encoding>H264</tt:Encoding>"
        "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
        "<tt:Quality>5</tt:Quality>"
        "<tt:RateControl>"
        "<tt:FrameRateLimit>25</tt:FrameRateLimit>"
        "<tt:EncodingInterval>1</tt:EncodingInterval>"
        "<tt:BitrateLimit>4096</tt:BitrateLimit>"
        "</tt:RateControl>"
        "</tt:VideoEncoderConfiguration>"
        "<tt:PTZConfiguration token=\"PTZConfig_1\">"
        "<tt:Name>PTZConfig</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:NodeToken>PTZNode_1</tt:NodeToken>"
        "<tt:DefaultAbsolutePantTiltPositionSpace>"
        "http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace"
        "</tt:DefaultAbsolutePantTiltPositionSpace>"
        "<tt:DefaultContinuousPanTiltVelocitySpace>"
        "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace"
        "</tt:DefaultContinuousPanTiltVelocitySpace>"
        "<tt:DefaultPTZSpeed>"
        "<tt:PanTilt x=\"0.5\" y=\"0.5\""
        " space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/>"
        "</tt:DefaultPTZSpeed>"
        "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
        "</tt:PTZConfiguration>"
        "</trt:Profiles>"
        "</%s>",
        tag, token, fixed, name, tag);
    send_soap(fd, body);
}


static void handle_CreateProfile(int fd, const char *xml)
{
    char name[64] = "SynoProfile";
    char token[64];
    static int counter = 0;

    const char *np = strstr(xml, "<Name");
    if (np) {
        np = strchr(np, '>');
        if (np) {
            np++;
            const char *ne = strstr(np, "</Name>");
            if (ne && (ne - np) > 0 && (ne - np) < (int)sizeof(name)) {
                memcpy(name, np, ne - np);
                name[ne - np] = '\0';
            }
        }
    }

    snprintf(token, sizeof(token), "Syno_%04x", counter++ & 0xFFFF);

    strncpy(active_profile_token, token, sizeof(active_profile_token) - 1);
    active_profile_token[sizeof(active_profile_token) - 1] = '\0';

    strncpy(active_profile_name, name, sizeof(active_profile_name) - 1);
    active_profile_name[sizeof(active_profile_name) - 1] = '\0';

    active_profile_fixed = 0;
	
    profile_has_video_source = 0;
    profile_has_video_encoder = 0;
    profile_has_ptz = 0;
    printf("[ONVIF] CreateProfile -> token=%s name=%s\n",
           active_profile_token, active_profile_name);

    send_profile_element_response(fd,
        "trt:CreateProfileResponse",
        "trt:Profile",
        active_profile_token,
        active_profile_name,
        "false");
}

static void handle_ok(int fd, const char *tag)
{
    char body[128];
    snprintf(body, sizeof(body), "<%s/>", tag);
    send_soap(fd, body);
}

static void send_jpeg_file(int fd, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(fd, resp, strlen(resp), 0);
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[256];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n", len);
    send(fd, header, strlen(header), 0);

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        send(fd, buf, n, 0);
    }
    fclose(f);
}

/* ===================== MAIN REQUEST ROUTER ===================== */
static void log_request_name(const char *xml)
{
    const char *name = NULL;

    if      (xml_has_op(xml, "ContinuousMove")) name = "ContinuousMove";
    else if (xml_has_op(xml, "Stop")) name = "Stop";
    else if (xml_has_op(xml, "GetCapabilities")) name = "GetCapabilities";
    else if (xml_has_op(xml, "GetServices")) name = "GetServices";
    else if (xml_has_op(xml, "GetDeviceInformation")) name = "GetDeviceInformation";
    else if (xml_has_op(xml, "GetSystemDateAndTime")) name = "GetSystemDateAndTime";
    else if (xml_has_op(xml, "GetScopes")) name = "GetScopes";
    else if (xml_has_op(xml, "GetNetworkInterfaces")) name = "GetNetworkInterfaces";
    else if (xml_has_op(xml, "GetNetworkDefaultGateway")) name = "GetNetworkDefaultGateway";
    else if (xml_has_op(xml, "GetNetworkProtocols")) name = "GetNetworkProtocols";
    else if (xml_has_op(xml, "GetDiscoveryMode")) name = "GetDiscoveryMode";
    else if (xml_has_op(xml, "GetNTP")) name = "GetNTP";
    else if (xml_has_op(xml, "GetDNS")) name = "GetDNS";
    else if (xml_has_op(xml, "GetHostname")) name = "GetHostname";
    else if (xml_has_op(xml, "GetRelayOutputs")) name = "GetRelayOutputs";
    else if (xml_has_op(xml, "GetServiceCapabilities")) name = "GetServiceCapabilities";
    else if (xml_has_op(xml, "GetVideoSources")) name = "GetVideoSources";
    else if (xml_has_op(xml, "GetVideoSourceConfigurationOptions")) name = "GetVideoSourceConfigurationOptions";
    else if (xml_has_op(xml, "GetVideoSourceConfigurations")) name = "GetVideoSourceConfigurations";
    else if (xml_has_op(xml, "GetGuaranteedNumberOfVideoEncoderInstances")) name = "GetGuaranteedNumberOfVideoEncoderInstances";
    else if (xml_has_op(xml, "GetVideoEncoderConfigurationOptions")) name = "GetVideoEncoderConfigurationOptions";
    else if (xml_has_op(xml, "SetVideoEncoderConfiguration")) name = "SetVideoEncoderConfiguration";
    else if (xml_has_op(xml, "GetVideoEncoderConfigurations")) name = "GetVideoEncoderConfigurations";
    else if (xml_has_op(xml, "GetVideoEncoderConfiguration")) name = "GetVideoEncoderConfiguration";
    else if (xml_has_op(xml, "GetAudioEncoderConfigurationOptions")) name = "GetAudioEncoderConfigurationOptions";
    else if (xml_has_op(xml, "GetCompatibleVideoEncoderConfigurations")) name = "GetCompatibleVideoEncoderConfigurations";
    else if (xml_has_op(xml, "CreateProfile")) name = "CreateProfile";
    else if (xml_has_op(xml, "DeleteProfile")) name = "DeleteProfile";
    else if (xml_has_op(xml, "GetProfile") && !xml_has_op(xml, "GetProfiles")) name = "GetProfile";
    else if (xml_has_op(xml, "GetProfiles")) name = "GetProfiles";
    else if (xml_has_op(xml, "GetStreamUri")) name = "GetStreamUri";
    else if (xml_has_op(xml, "GetSnapshotUri")) name = "GetSnapshotUri";
    else if (xml_has_op(xml, "AddVideoSourceConfiguration")) name = "AddVideoSourceConfiguration";
    else if (xml_has_op(xml, "AddVideoEncoderConfiguration")) name = "AddVideoEncoderConfiguration";
    else if (xml_has_op(xml, "AddPTZConfiguration")) name = "AddPTZConfiguration";
    else if (xml_has_op(xml, "GetNodes")) name = "GetNodes";
    else if (xml_has_op(xml, "GetConfigurationOptions")) name = "GetConfigurationOptions";
    else if (xml_has_op(xml, "GetConfigurations")) name = "GetConfigurations";

    if (name) {
        printf("[ONVIF] %s\n", name);
    } else {
        printf("[ONVIF] UNHANDLED FULL START:\n%.500s\n", xml);
    }
    fflush(stdout);
}

static void handle_request(int fd, const char *xml)
{
    log_request_name(xml);

    /* PTZ first */
    if (xml_has_op(xml, "ContinuousMove")) {
        parse_and_execute_ptz(xml);
        handle_ok(fd, "tptz:ContinuousMoveResponse");

    } else if (xml_has_op(xml, "Stop")) {
        handle_ok(fd, "tptz:StopResponse");

    /* Device service */
    } else if (xml_has_op(xml, "GetCapabilities")) {
        handle_GetCapabilities(fd);

    } else if (xml_has_op(xml, "GetServices")) {
        handle_GetServices(fd);

    } else if (xml_has_op(xml, "GetDeviceInformation")) {
        handle_GetDeviceInformation(fd);

    } else if (xml_has_op(xml, "GetSystemDateAndTime")) {
        handle_GetSystemDateAndTime(fd);

    } else if (xml_has_op(xml, "GetScopes")) {
        handle_GetScopes(fd);

    } else if (xml_has_op(xml, "GetNetworkInterfaces")) {
        handle_GetNetworkInterfaces(fd);

    } else if (xml_has_op(xml, "GetNetworkDefaultGateway")) {
        handle_ok(fd, "tds:GetNetworkDefaultGatewayResponse");

    } else if (xml_has_op(xml, "GetNetworkProtocols")) {
        handle_ok(fd, "tds:GetNetworkProtocolsResponse");

    } else if (xml_has_op(xml, "GetDiscoveryMode")) {
        send_soap(fd, "<tds:GetDiscoveryModeResponse>"
                      "<tds:DiscoveryMode>Discoverable</tds:DiscoveryMode>"
                      "</tds:GetDiscoveryModeResponse>");

    } else if (xml_has_op(xml, "GetNTP")) {
        handle_ok(fd, "tds:GetNTPResponse");

    } else if (xml_has_op(xml, "GetDNS")) {
        handle_ok(fd, "tds:GetDNSResponse");

    } else if (xml_has_op(xml, "GetHostname")) {
        send_soap(fd, "<tds:GetHostnameResponse>"
                      "<tds:HostnameInformation FromDHCP=\"false\">"
                      "<tt:Name>AnykaCamera</tt:Name>"
                      "</tds:HostnameInformation>"
                      "</tds:GetHostnameResponse>");

    } else if (xml_has_op(xml, "GetRelayOutputs")) {
        handle_ok(fd, "tds:GetRelayOutputsResponse");

    /* Media service */
    } else if (xml_has_op(xml, "GetServiceCapabilities")) {
        handle_GetServiceCapabilities(fd);

    } else if (xml_has_op(xml, "GetVideoSources")) {
        handle_GetVideoSources(fd);
} else if (xml_has_op(xml, "GetVideoSourceConfigurationOptions")) {
    handle_GetVideoSourceConfigurationOptions(fd);

    } else if (xml_has_op(xml, "GetVideoSourceConfigurations")) {
        handle_GetVideoSourceConfigurations(fd);

    } else if (xml_has_op(xml, "GetGuaranteedNumberOfVideoEncoderInstances")) {
        handle_GetGuaranteedNumberOfVideoEncoderInstances(fd);

    } else if (xml_has_op(xml, "GetVideoEncoderConfigurationOptions")) {
        handle_GetVideoEncoderConfigurationOptions(fd);

    } else if (xml_has_op(xml, "SetVideoEncoderConfiguration")) {
        handle_ok(fd, "trt:SetVideoEncoderConfigurationResponse");

  } else if (xml_has_op(xml, "GetVideoEncoderConfigurations")) {
    handle_GetVideoEncoderConfigurations(fd);

} else if (xml_has_op(xml, "GetVideoEncoderConfiguration")) {
    handle_GetVideoEncoderConfiguration(fd);
} else if (xml_has_op(xml, "GetAudioEncoderConfigurationOptions")) {
    handle_GetAudioEncoderConfigurationOptions(fd); 

    } else if (xml_has_op(xml, "GetCompatibleVideoEncoderConfigurations")) {
        handle_GetVideoEncoderConfigurations(fd);

    } else if (xml_has_op(xml, "CreateProfile")) {
        printf("[ONVIF] CreateProfile XML: %.200s\n", xml);
        fflush(stdout);
        handle_CreateProfile(fd, xml);

    } else if (xml_has_op(xml, "DeleteProfile")) {
        strcpy(active_profile_token, "Profile_1");
        strcpy(active_profile_name, "MainStream");
        active_profile_fixed = 1;

        profile_has_video_source = 1;
        profile_has_video_encoder = 1;
        profile_has_ptz = 1;

        printf("[ONVIF] DeleteProfile -> reset default\n");
        fflush(stdout);
        handle_ok(fd, "trt:DeleteProfileResponse");

    } else if (xml_has_op(xml, "GetProfile") && !xml_has_op(xml, "GetProfiles")) {
        printf("[ONVIF] GetProfile token=%s name=%s fixed=%d\n",
               active_profile_token, active_profile_name, active_profile_fixed);
        fflush(stdout);

        send_profile_element_response(fd,
            "trt:GetProfileResponse",
            "trt:Profile",
            active_profile_token,
            active_profile_name,
            active_profile_fixed ? "true" : "false");

    } else if (xml_has_op(xml, "GetProfiles")) {
        handle_GetProfiles(fd);

    } else if (xml_has_op(xml, "GetStreamUri")) {
        handle_GetStreamUri(fd);

    } else if (xml_has_op(xml, "GetSnapshotUri")) {
        handle_GetSnapshotUri(fd);

    } else if (xml_has_op(xml, "AddVideoSourceConfiguration")) {
        profile_has_video_source = 1;
        printf("[ONVIF] AddVideoSourceConfiguration\n");
        fflush(stdout);
        handle_ok(fd, "trt:AddVideoSourceConfigurationResponse");

    } else if (xml_has_op(xml, "AddVideoEncoderConfiguration")) {
        profile_has_video_encoder = 1;
        printf("[ONVIF] AddVideoEncoderConfiguration\n");
        fflush(stdout);
        handle_ok(fd, "trt:AddVideoEncoderConfigurationResponse");

    } else if (xml_has_op(xml, "AddPTZConfiguration")) {
        profile_has_ptz = 1;
        printf("[ONVIF] AddPTZConfiguration\n");
        fflush(stdout);
        handle_ok(fd, "trt:AddPTZConfigurationResponse");

    /* PTZ service */
    } else if (xml_has_op(xml, "GetNodes")) {
        handle_GetNodes(fd);

    } else if (xml_has_op(xml, "GetConfigurationOptions")) {
        handle_GetConfigurationOptions(fd);

    } else if (xml_has_op(xml, "GetConfigurations")) {
        handle_GetConfigurations(fd);

    /* Fallback */
    } else {
        printf("[ONVIF] UNHANDLED FULL START:\n%.500s\n", xml);
        fflush(stdout);
        send_soap(fd,
            "<s:Fault>"
            "<s:Code><s:Value>s:Receiver</s:Value></s:Code>"
            "<s:Reason><s:Text>Not implemented</s:Text></s:Reason>"
            "</s:Fault>");
    }
}






/* ===================== HTTP SERVER ===================== */

struct client_args {
    int fd;
};

static void *client_thread(void *arg)
{
    struct client_args *ca = (struct client_args *)arg;
    int fd = ca->fd;
    free(ca);

    char *buf = malloc(BUF_SIZE);
    if (!buf) { close(fd); return NULL; }

    int total = 0;
    int n;

    /* Read HTTP request */
    while (total < BUF_SIZE - 1) {
        n = recv(fd, buf + total, BUF_SIZE - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = 0;

        /* Check if we have full HTTP headers + body */
        char *body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            /* Check Content-Length */
            char *cl = strstr(buf, "Content-Length:");
            if (cl) {
                int content_len = atoi(cl + 15);
                int header_len = body_start - buf;
                int body_received = total - header_len;
                if (body_received >= content_len)
                    break;
            } else {
                break;
            }
        }
    }

    if (total > 0) {
        /* Check for snapshot request FIRST */
if (strncmp(buf, "GET /snapshot.jpg", 17) == 0) {
    send_jpeg_file(fd, SNAPSHOT_PATH);
    free(buf);
    close(fd);
    return NULL;
}
        /* Find XML body */
        char *xml = strstr(buf, "<?xml");
        if (!xml) xml = strstr(buf, "<s:Envelope");
        if (!xml) xml = strstr(buf, "<env:Envelope");
        if (xml) {
            handle_request(fd, xml);
        } else {
            /* HTTP GET — return simple status */
            const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
            send(fd, resp, strlen(resp), 0);
        }
    }

    free(buf);
    close(fd);
    return NULL;
}

static void *http_server_thread(void *arg)
{
    int port = (int)(intptr_t)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return NULL;
    }

    printf("[ONVIF] HTTP server listening on port %d\n", port);
    fflush(stdout);
    while (server_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_fd, &fds);
        struct timeval tv = {1, 0};
        int sel = select(server_fd + 1, &fds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        struct client_args *ca = malloc(sizeof(struct client_args));
        if (!ca) { close(client_fd); continue; }
        ca->fd = client_fd;

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, ca);
        pthread_detach(tid);
    }

    close(server_fd);
    return NULL;
}

/* ===================== WS-DISCOVERY ===================== */

static void *discovery_thread(void *arg)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) { perror("discovery socket"); return NULL; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(DISCOVERY_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("discovery bind");
        close(fd);
        return NULL;
    }

    struct ip_mreq mreq = {0};
    inet_aton(DISCOVERY_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    printf("[DISCOVERY] WS-Discovery listening on UDP %d\n", DISCOVERY_PORT);
    fflush(stdout);
    char buf[BUF_SIZE];
    char reply[BUF_SIZE];

    while (server_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = {1, 0};
        int sel = select(fd + 1, &fds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(fd, buf, BUF_SIZE - 1, 0,
                         (struct sockaddr *)&from, &from_len);
        if (n <= 0) continue;
        buf[n] = 0;

        if (!str_contains(buf, "Probe")) continue;

        /* Extract MessageID */
        char msg_id[128] = "unknown";
        const char *mid = strstr(buf, "MessageID>");
        if (mid) {
            mid += 10;
            const char *end = strstr(mid, "<");
            if (end && (end - mid) < 120) {
                strncpy(msg_id, mid, end - mid);
                msg_id[end - mid] = 0;
                /* Strip urn:uuid: prefix */
                char *uuid_part = strstr(msg_id, "uuid:");
                if (uuid_part) memmove(msg_id, uuid_part + 5, strlen(uuid_part + 5) + 1);
            }
        }

        snprintf(reply, sizeof(reply),
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope"
            " xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\""
            " xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\""
            " xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\""
            " xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
            "<s:Header>"
            "<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</a:Action>"
            "<a:MessageID>urn:uuid:%s-reply</a:MessageID>"
            "<a:RelatesTo>%s</a:RelatesTo>"
            "<a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
            "</s:Header>"
            "<s:Body>"
            "<d:ProbeMatches>"
            "<d:ProbeMatch>"
            "<a:EndpointReference>"
            "<a:Address>urn:uuid:%s</a:Address>"
            "</a:EndpointReference>"
            "<d:Types>dn:NetworkVideoTransmitter</d:Types>"
            "<d:Scopes>"
            "onvif://www.onvif.org/name/AnykaCamera "
            "onvif://www.onvif.org/type/video_encoder "
            "onvif://www.onvif.org/type/ptz "
            "onvif://www.onvif.org/Profile/Streaming "
            "onvif://www.onvif.org/manufacturer/Anyka"
            "</d:Scopes>"
            "<d:XAddrs>http://%s:%d/onvif/device_service</d:XAddrs>"
            "<d:MetadataVersion>1</d:MetadataVersion>"
            "</d:ProbeMatch>"
            "</d:ProbeMatches>"
            "</s:Body>"
            "</s:Envelope>",
            UUID_STR, msg_id, UUID_STR,
            camera_ip, ONVIF_PORT);

        sendto(fd, reply, strlen(reply), 0,
               (struct sockaddr *)&from, from_len);
        printf("[DISCOVERY] Sent ProbeMatch to %s\n", inet_ntoa(from.sin_addr));
        fflush(stdout);
    }

    close(fd);
    return NULL;
}

/* ===================== MAIN ===================== */

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("[ONVIF] main started\n");
    fflush(stdout);

    signal(SIGPIPE, SIG_IGN);

    /* Get local IP */
    get_local_ip(camera_ip, sizeof(camera_ip));
    printf("[ONVIF] Version: %s (%s %s)\n", VERSION, __DATE__, __TIME__);
    printf("[ONVIF] Camera IP: %s\n", camera_ip);
    fflush(stdout);
    printf("[ONVIF] RTSP: rtsp://%s:%d%s\n", camera_ip, RTSP_PORT, RTSP_PATH);
    fflush(stdout);
    printf("[ONVIF] PTZ webui: http://127.0.0.1:%d/cgi-bin/webui?command=ptzX\n",
           PTZ_WEBUI_PORT);
    fflush(stdout);

    /* Start WS-Discovery */
    pthread_t disc_tid;
    pthread_create(&disc_tid, NULL, discovery_thread, NULL);
    pthread_detach(disc_tid);

    /* Start HTTP server — blocks here */
    http_server_thread((void *)(intptr_t)ONVIF_PORT);

    return 0;
}
