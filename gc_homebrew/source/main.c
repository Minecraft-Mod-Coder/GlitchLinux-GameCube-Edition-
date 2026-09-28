#include <gccore.h>
#include <network.h>
#include <ogc/console.h>
#include <ogc/pad.h>
#include <stdio.h>
#include <string.h>

#define HTTP_RESPONSE_SIZE 8192
#define PAGE_TEXT_SIZE 4096
#define REPOSITORY_PATH "/repos/Minecraft-Mod-Coder/GlitchLinux-GameCube-Edition-"

typedef enum {
    SCREEN_HOME,
    SCREEN_TERMINAL,
    SCREEN_BROWSER,
    SCREEN_SETTINGS,
    SCREEN_GITHUB,
    SCREEN_ABOUT
} Screen;

static GXRModeObj *video_mode;
static Screen screen = SCREEN_HOME;
static int selection;
static int terminal_selection;
static int rumble_frames;
static const char *terminal_result = "Select a command and press A.";
static int network_started;
static int network_ready;
static int browser_selection;
static int browser_scroll;
static int github_selection;
static int github_scroll;
static char browser_page[PAGE_TEXT_SIZE];
static char browser_status[96] = "Choose an HTTP bookmark and press A.";
static char github_page[PAGE_TEXT_SIZE];
static char github_status[96] = "Choose a GitHub view and press A.";
static char network_gateway[16];
static unsigned short dns_query_id = 0x474c;

static const char *browser_bookmarks[] = {
    "neverssl.com", "info.cern.ch", "example.com"
};

static const char *github_items[] = {
    "Repository details", "Recent releases", "Open issues", "Project page"
};

static int ensure_network(void)
{
    char local_ip[16] = {0};
    char netmask[16] = {0};

    if (network_ready) {
        return 1;
    }
    if (!network_started) {
        if (net_init() < 0) {
            snprintf(browser_status, sizeof(browser_status),
                     "Network init failed; check the Broadband Adapter.");
            snprintf(github_status, sizeof(github_status), "%s", browser_status);
            return 0;
        }
        network_started = 1;
    }
    if (if_config(local_ip, netmask, network_gateway, TRUE, 10) < 0) {
        snprintf(browser_status, sizeof(browser_status),
                 "DHCP failed; check the Broadband Adapter network.");
        snprintf(github_status, sizeof(github_status), "%s", browser_status);
        return 0;
    }
    network_ready = 1;
    snprintf(browser_status, sizeof(browser_status), "Network ready: %.15s",
             local_ip);
    snprintf(github_status, sizeof(github_status), "Network ready.");
    return 1;
}

static unsigned short read_u16(const unsigned char *data)
{
    return (unsigned short)(((unsigned short)data[0] << 8) | data[1]);
}

static int skip_dns_name(const unsigned char *packet, int packet_size,
                         int *offset)
{
    int position = *offset;

    while (position < packet_size) {
        unsigned int label_size = packet[position++];

        if ((label_size & 0xc0U) == 0xc0U) {
            if (position >= packet_size) {
                return 0;
            }
            *offset = position + 1;
            return 1;
        }
        if (label_size == 0) {
            *offset = position;
            return 1;
        }
        if ((label_size & 0xc0U) != 0 ||
            position + (int)label_size > packet_size) {
            return 0;
        }
        position += (int)label_size;
    }
    return 0;
}

static int dns_query(const char *host_name, struct in_addr *result,
                     struct in_addr dns_server)
{
    unsigned char query[512] = {0};
    unsigned char answer[512];
    struct sockaddr_in address;
    struct timeval timeout = {5, 0};
    const char *label = host_name;
    int query_size = 12;
    int socket_fd;
    int answer_size;
    int question_count;
    int answer_count;
    int position;

    ++dns_query_id;
    query[0] = (unsigned char)(dns_query_id >> 8);
    query[1] = (unsigned char)dns_query_id;
    query[2] = 0x01;
    query[5] = 0x01;

    while (*label != '\0') {
        const char *dot = strchr(label, '.');
        size_t label_size = dot == NULL ? strlen(label) : (size_t)(dot - label);

        if (label_size == 0 || label_size > 63 ||
            query_size + (int)label_size + 5 > (int)sizeof(query)) {
            return 0;
        }
        query[query_size++] = (unsigned char)label_size;
        memcpy(query + query_size, label, label_size);
        query_size += (int)label_size;
        if (dot == NULL) {
            break;
        }
        label = dot + 1;
    }
    query[query_size++] = 0;
    query[query_size++] = 0;
    query[query_size++] = 1;
    query[query_size++] = 0;
    query[query_size++] = 1;

    socket_fd = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        return 0;
    }
    net_setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(53);
    address.sin_addr = dns_server;
    if (net_sendto(socket_fd, query, query_size, 0,
                   (struct sockaddr *)&address, sizeof(address)) < 0) {
        net_close(socket_fd);
        return 0;
    }
    answer_size = net_recvfrom(socket_fd, answer, sizeof(answer), 0, NULL, NULL);
    net_close(socket_fd);
    if (answer_size < 12 || read_u16(answer) != dns_query_id ||
        (answer[2] & 0x80U) == 0 || (answer[3] & 0x0fU) != 0) {
        return 0;
    }

    question_count = read_u16(answer + 4);
    answer_count = read_u16(answer + 6);
    position = 12;
    for (int i = 0; i < question_count; ++i) {
        if (!skip_dns_name(answer, answer_size, &position) ||
            position + 4 > answer_size) {
            return 0;
        }
        position += 4;
    }
    for (int i = 0; i < answer_count; ++i) {
        unsigned short record_type;
        unsigned short record_class;
        unsigned short data_size;

        if (!skip_dns_name(answer, answer_size, &position) ||
            position + 10 > answer_size) {
            return 0;
        }
        record_type = read_u16(answer + position);
        record_class = read_u16(answer + position + 2);
        data_size = read_u16(answer + position + 8);
        position += 10;
        if (position + data_size > answer_size) {
            return 0;
        }
        if (record_type == 1 && record_class == 1 && data_size == 4) {
            memcpy(&result->s_addr, answer + position, 4);
            return 1;
        }
        position += data_size;
    }
    return 0;
}

static int resolve_host(const char *host_name, struct in_addr *result)
{
    struct in_addr dns_server;

    if (network_gateway[0] != '\0') {
        dns_server.s_addr = inet_addr(network_gateway);
        if (dns_query(host_name, result, dns_server)) {
            return 1;
        }
    }
    dns_server.s_addr = inet_addr("1.1.1.1");
    return dns_query(host_name, result, dns_server);
}

static void render_http_body(const char *body, char *destination,
                             size_t destination_size)
{
    int in_tag = 0;
    size_t output = 0;

    for (size_t i = 0; body[i] != '\0' && output + 2 < destination_size; ++i) {
        char current = body[i];

        if (current == '<') {
            in_tag = 1;
            continue;
        }
        if (current == '>') {
            in_tag = 0;
            if (output > 0 && destination[output - 1] != '\n') {
                destination[output++] = '\n';
            }
            continue;
        }
        if (!in_tag && current != '\r') {
            destination[output++] = current;
        }
    }
    destination[output] = '\0';
}

static int http_get(const char *host_name, const char *path,
                    char *response, size_t response_size)
{
    struct sockaddr_in address;
    int socket_fd;
    int received = 0;
    char request[512];
    int request_size;

    if (!ensure_network()) {
        return -1;
    }
    if (!resolve_host(host_name, &address.sin_addr)) {
        snprintf(browser_status, sizeof(browser_status), "DNS lookup failed.");
        snprintf(github_status, sizeof(github_status), "%s", browser_status);
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(80);

    socket_fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd < 0 || net_connect(socket_fd, (struct sockaddr *)&address,
                                     sizeof(address)) < 0) {
        if (socket_fd >= 0) {
            net_close(socket_fd);
        }
        snprintf(browser_status, sizeof(browser_status), "Could not connect to host.");
        snprintf(github_status, sizeof(github_status), "%s", browser_status);
        return -1;
    }

    request_size = snprintf(request, sizeof(request),
                            "GET %s HTTP/1.0\r\nHost: %s\r\n"
                            "User-Agent: GlitchLinux-GameCube/0.1\r\n"
                            "Accept: text/html, application/json\r\n"
                            "Connection: close\r\n\r\n", path, host_name);
    if (request_size < 0 || (size_t)request_size >= sizeof(request) ||
        net_send(socket_fd, request, request_size, 0) < 0) {
        net_close(socket_fd);
        snprintf(browser_status, sizeof(browser_status), "HTTP request failed.");
        snprintf(github_status, sizeof(github_status), "%s", browser_status);
        return -1;
    }

    while ((size_t)received + 1 < response_size) {
        int count = net_recv(socket_fd, response + received,
                             (int)(response_size - (size_t)received - 1), 0);
        if (count <= 0) {
            break;
        }
        received += count;
    }
    net_close(socket_fd);
    response[received] = '\0';
    return received;
}

static void fetch_browser_bookmark(void)
{
    char response[HTTP_RESPONSE_SIZE];
    char *body;

    browser_page[0] = '\0';
    browser_scroll = 0;
    snprintf(browser_status, sizeof(browser_status), "Loading %s...",
             browser_bookmarks[browser_selection]);
    if (http_get(browser_bookmarks[browser_selection], "/", response,
                 sizeof(response)) < 0) {
        snprintf(browser_page, sizeof(browser_page), "%s", browser_status);
        return;
    }

    body = strstr(response, "\r\n\r\n");
    if (body == NULL) {
        snprintf(browser_status, sizeof(browser_status), "Invalid HTTP response.");
        snprintf(browser_page, sizeof(browser_page), "%s", browser_status);
        return;
    }
    body += 4;
    if (strncmp(response, "HTTP/1.0 200", 12) != 0 &&
        strncmp(response, "HTTP/1.1 200", 12) != 0) {
        snprintf(browser_status, sizeof(browser_status),
                 "Server returned an error or HTTPS redirect.");
        snprintf(browser_page, sizeof(browser_page), "%.4000s", body);
        return;
    }

    render_http_body(body, browser_page, sizeof(browser_page));
    snprintf(browser_status, sizeof(browser_status), "Loaded %s (HTTP only).",
             browser_bookmarks[browser_selection]);
}

static void fetch_github_view(void)
{
    char response[HTTP_RESPONSE_SIZE];
    const char *host = "api.github.com";
    const char *path;
    char *body;

    if (github_selection == 0) {
        path = REPOSITORY_PATH;
    } else if (github_selection == 1) {
        path = REPOSITORY_PATH "/releases?per_page=5";
    } else if (github_selection == 2) {
        path = REPOSITORY_PATH "/issues?per_page=5&state=open";
    } else {
        host = "github.com";
        path = "/Minecraft-Mod-Coder/GlitchLinux-GameCube-Edition-";
    }

    github_page[0] = '\0';
    github_scroll = 0;
    snprintf(github_status, sizeof(github_status), "Requesting GitHub...");
    if (http_get(host, path, response, sizeof(response)) < 0) {
        snprintf(github_page, sizeof(github_page), "%s", github_status);
        return;
    }

    body = strstr(response, "\r\n\r\n");
    if (body == NULL) {
        snprintf(github_status, sizeof(github_status), "Invalid GitHub response.");
        snprintf(github_page, sizeof(github_page), "%s", github_status);
        return;
    }
    body += 4;
    if (strncmp(response, "HTTP/1.0 200", 12) != 0 &&
        strncmp(response, "HTTP/1.1 200", 12) != 0) {
        snprintf(github_status, sizeof(github_status),
                 "%s requires HTTPS; this build has no TLS support.", host);
        snprintf(github_page, sizeof(github_page),
                 "GitHub returned a redirect or error.\n"
                 "A TLS-enabled HTTPS transport is required to load live data.");
        return;
    }

    render_http_body(body, github_page, sizeof(github_page));
    snprintf(github_status, sizeof(github_status), "GitHub response loaded.");
}

static void print_scrolled_text(const char *text, int scroll, int visible_lines)
{
    const char *line = text;
    int line_number = 0;
    int shown = 0;

    while (*line != '\0' && line_number < scroll) {
        const char *next = strchr(line, '\n');
        if (next == NULL) {
            return;
        }
        line = next + 1;
        ++line_number;
    }
    while (*line != '\0' && shown < visible_lines) {
        const char *next = strchr(line, '\n');
        size_t length = next == NULL ? strlen(line) : (size_t)(next - line);
        if (length > 74) {
            length = 74;
        }
        printf("%.*s\n", (int)length, line);
        ++shown;
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
}

static void draw_header(const char *title)
{
    consoleClear();
    printf("GLITCHLINUX GAMECUBE  |  %s\n", title);
    printf("========================================\n\n");
}

static void draw_home(void)
{
    static const char *items[] = {
        "Terminal", "Browser", "Settings", "GitHub", "About", "Restart"
    };
    int i;

    draw_header("Home");
    printf("GameCube homebrew environment\n\n");
    for (i = 0; i < 6; ++i) {
        printf("%s %s\n", selection == i ? ">" : " ", items[i]);
    }
    printf("\nD-pad: move   A: open   START: restart\n");
}

static void draw_terminal(void)
{
    static const char *commands[] = {"help", "system info", "clear", "home"};
    int i;

    draw_header("Terminal");
    printf("Controller command palette (not a POSIX shell)\n\n");
    for (i = 0; i < 4; ++i) {
        printf("%s %s\n", terminal_selection == i ? ">" : " ", commands[i]);
    }
    printf("\n%s\n\nD-pad: move   A: run   B: desktop\n", terminal_result);
}

static void draw_browser(void)
{
    draw_header("Browser");
    printf("HTTP text browser  |  D-pad: choose/scroll  A: open\n");
    printf("B: desktop  |  X: reload  |  HTTP only (no TLS)\n\n");
    for (int i = 0; i < 3; ++i) {
        printf("%s %s\n", browser_selection == i ? ">" : " ",
               browser_bookmarks[i]);
    }
    printf("\n%s\n", browser_status);
    print_scrolled_text(browser_page, browser_scroll, 13);
}

static void draw_settings(void)
{
    draw_header("Settings");
    printf("Video framebuffer: %u x %u\n", video_mode->fbWidth,
           video_mode->xfbHeight);
    printf("Controller: port 1\n\n");
    printf("A: test controller rumble\n");
    printf("B: desktop\n");
}

static void draw_github(void)
{
    draw_header("GitHub");
    printf("D-pad: choose/scroll  A: fetch  B: desktop\n");
    printf("Live GitHub data requires HTTPS/TLS support.\n\n");
    for (int i = 0; i < 4; ++i) {
        printf("%s %s\n", github_selection == i ? ">" : " ", github_items[i]);
    }
    printf("\n%s\n", github_status);
    print_scrolled_text(github_page, github_scroll, 10);
    if (github_selection == 3) {
        printf("\nhttps://github.com/Minecraft-Mod-Coder/\n");
        printf("GlitchLinux-GameCube-Edition-\n");
    }
}

static void draw_screen(void)
{
    switch (screen) {
    case SCREEN_HOME:
        draw_home();
        break;
    case SCREEN_TERMINAL:
        draw_terminal();
        break;
    case SCREEN_BROWSER:
        draw_browser();
        break;
    case SCREEN_SETTINGS:
        draw_settings();
        break;
    case SCREEN_GITHUB:
        draw_github();
        break;
    case SCREEN_ABOUT:
        draw_header("About");
        printf("GlitchLinux GameCube homebrew prototype\n\n");
        printf("This is a native GameCube application, not a\n");
        printf("Linux kernel or standalone operating system.\n\n");
        printf("B: desktop\n");
        break;
    }
}

static void run_terminal_command(void)
{
    switch (terminal_selection) {
    case 0:
        terminal_result = "Commands: help, system info, clear, home.";
        break;
    case 1:
        terminal_result = "Platform: Nintendo GameCube homebrew (PowerPC).";
        break;
    case 2:
        terminal_result = "Screen cleared.";
        break;
    case 3:
        screen = SCREEN_HOME;
        selection = 0;
        break;
    }
}

static void handle_home_input(u32 pressed)
{
    if ((pressed & PAD_BUTTON_DOWN) && selection < 5) {
        ++selection;
    }
    if ((pressed & PAD_BUTTON_UP) && selection > 0) {
        --selection;
    }
    if ((pressed & PAD_BUTTON_START) ||
        ((pressed & PAD_BUTTON_A) && selection == 5)) {
        SYS_ResetSystem(SYS_HOTRESET, 0, 0);
    }
    if (pressed & PAD_BUTTON_A) {
        switch (selection) {
        case 0:
            screen = SCREEN_TERMINAL;
            break;
        case 1:
            screen = SCREEN_BROWSER;
            break;
        case 2:
            screen = SCREEN_SETTINGS;
            break;
        case 3:
            screen = SCREEN_GITHUB;
            break;
        case 4:
            screen = SCREEN_ABOUT;
            break;
        }
    }
}

static void handle_browser_input(u32 pressed)
{
    if ((pressed & PAD_BUTTON_LEFT) && browser_selection > 0) {
        --browser_selection;
        browser_page[0] = '\0';
        browser_scroll = 0;
    }
    if ((pressed & PAD_BUTTON_RIGHT) && browser_selection < 2) {
        ++browser_selection;
        browser_page[0] = '\0';
        browser_scroll = 0;
    }
    if (pressed & PAD_BUTTON_UP) {
        if (browser_page[0] != '\0' && browser_scroll > 0) {
            --browser_scroll;
        } else if (browser_selection > 0) {
            --browser_selection;
        }
    }
    if (pressed & PAD_BUTTON_DOWN) {
        if (browser_page[0] != '\0') {
            ++browser_scroll;
        } else if (browser_selection < 2) {
            ++browser_selection;
        }
    }
    if ((pressed & PAD_BUTTON_A) || (pressed & PAD_BUTTON_X)) {
        fetch_browser_bookmark();
    }
    if (pressed & PAD_BUTTON_B) {
        screen = SCREEN_HOME;
        selection = 0;
    }
}

static void handle_github_input(u32 pressed)
{
    if ((pressed & PAD_BUTTON_LEFT) && github_selection > 0) {
        --github_selection;
        github_page[0] = '\0';
        github_scroll = 0;
    }
    if ((pressed & PAD_BUTTON_RIGHT) && github_selection < 3) {
        ++github_selection;
        github_page[0] = '\0';
        github_scroll = 0;
    }
    if ((pressed & PAD_BUTTON_UP) && github_page[0] != '\0' && github_scroll > 0) {
        --github_scroll;
    } else if ((pressed & PAD_BUTTON_UP) && github_selection > 0) {
        --github_selection;
    }
    if ((pressed & PAD_BUTTON_DOWN) && github_page[0] != '\0') {
        ++github_scroll;
    } else if ((pressed & PAD_BUTTON_DOWN) && github_selection < 3) {
        ++github_selection;
    }
    if (pressed & PAD_BUTTON_A) {
        fetch_github_view();
    }
    if (pressed & PAD_BUTTON_B) {
        screen = SCREEN_HOME;
        selection = 0;
    }
}

int main(void)
{
    void *framebuffer;
    int running = 1;

    VIDEO_Init();
    PAD_Init();
    video_mode = VIDEO_GetPreferredMode(NULL);
    framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(video_mode));
    if (framebuffer == NULL) {
        return 1;
    }

    console_init(framebuffer, 20, 20, video_mode->fbWidth,
                 video_mode->xfbHeight,
                 video_mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(video_mode);
    VIDEO_SetNextFramebuffer(framebuffer);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (video_mode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }

    while (running) {
        u32 pressed;

        PAD_ScanPads();
        pressed = PAD_ButtonsDown(0);

        if (screen == SCREEN_HOME) {
            handle_home_input(pressed);
        } else if (screen == SCREEN_TERMINAL) {
            if ((pressed & PAD_BUTTON_DOWN) && terminal_selection < 3) {
                ++terminal_selection;
            }
            if ((pressed & PAD_BUTTON_UP) && terminal_selection > 0) {
                --terminal_selection;
            }
            if (pressed & PAD_BUTTON_A) {
                run_terminal_command();
            }
            if (pressed & PAD_BUTTON_B) {
                screen = SCREEN_HOME;
                selection = 0;
            }
        } else if (screen == SCREEN_SETTINGS) {
            if (pressed & PAD_BUTTON_A) {
                PAD_ControlMotor(0, PAD_MOTOR_RUMBLE);
                rumble_frames = 12;
            }
            if (pressed & PAD_BUTTON_B) {
                PAD_ControlMotor(0, PAD_MOTOR_STOP);
                rumble_frames = 0;
                screen = SCREEN_HOME;
                selection = 0;
            }
        } else if (screen == SCREEN_BROWSER) {
            handle_browser_input(pressed);
        } else if (screen == SCREEN_GITHUB) {
            handle_github_input(pressed);
        } else if (pressed & PAD_BUTTON_B) {
            screen = SCREEN_HOME;
            selection = 0;
        }

        if (rumble_frames > 0 && --rumble_frames == 0) {
            PAD_ControlMotor(0, PAD_MOTOR_STOP);
        }

        draw_screen();
        VIDEO_WaitVSync();
    }

    PAD_ControlMotor(0, PAD_MOTOR_STOP);
    return 0;
}