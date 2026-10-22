/* Native macOS helper for Discord Rich Presence, started by gryphfield-engine during a launch
 * when config.json has "discord_presence": true. Design after xbridge
 * (github.com/PlusoneChiang/xbridge), written anew.
 *
 * - Relays the game's Discord connection: discord-bridge.exe (inside Wine) owns the game's pipe
 *   \\.\pipe\discord-ipc-0 and connects here over 127.0.0.1; the first line must be the token
 *   from GRYPHFIELD_DISCORD_TOKEN. Bytes then go unchanged to and from Discord's Unix socket
 *   ($TMPDIR/discord-ipc-N), so the game's own Rich Presence (player, level) shows.
 * - Fallback: while the engine reports the game running ("playing <unix time>" on stdin) and the
 *   game has no connection of its own, it sets a plain activity for ARKNIGHTS: ENDFIELD
 *   (Discord application 1461154307171811401, the game's own entry) with the start time. Discord
 *   for macOS cannot see the game process, which runs under Wine.
 * stdin: "playing <unix time>" | "idle"; end of input ends the helper. Prints "port <n>" on stdout
 * once listening. GRYPHFIELD_DISCORD_IPC_DIR overrides the socket directory (for tests).
 *
 * Build: clang -O2 -Wall -Wextra -arch arm64 -arch x86_64 gryphfield-discord.c -o gryphfield-discord */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

#define ENDFIELD_CLIENT_ID "1461154307171811401"
#define RETRY_SECONDS 15

enum { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2 };

static const char *token;
static int listen_fd = -1;
static int game_fd = -1;          /* from discord-bridge.exe */
static int game_authed;
static char game_line[160];
static size_t game_line_len;
static int game_discord_fd = -1;  /* our socket to Discord for the game */
static int fallback_fd = -1;      /* our own socket to Discord for the fallback activity */
static int fallback_ready;
static long long playing_since;   /* 0: game not running */
static time_t fallback_retry;
static int stdin_open = 1;

static void logf_(const char *fmt, const char *arg)
{
    fprintf(stderr, "gryphfield-discord: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
}

static void close_fd(int *fd)
{
    if (*fd >= 0) close(*fd);
    *fd = -1;
}

static int write_all(int fd, const void *data, size_t size)
{
    const char *p = data;
    ssize_t n;

    while (size)
    {
        n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static int send_frame(int fd, uint32_t op, const char *json)
{
    uint32_t len = (uint32_t)strlen(json);
    unsigned char header[8];

    memcpy(header, &op, 4);       /* little-endian on both Apple architectures */
    memcpy(header + 4, &len, 4);
    return write_all(fd, header, 8) || write_all(fd, json, len) ? -1 : 0;
}

/* Discord's socket: $TMPDIR/discord-ipc-0..9, as the Discord client creates it on macOS */
static int connect_discord(void)
{
    const char *dirs[4];
    char darwin_tmp[512], path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct sockaddr_un addr;
    int i, j, fd, ndirs = 0;

    if (getenv("GRYPHFIELD_DISCORD_IPC_DIR")) dirs[ndirs++] = getenv("GRYPHFIELD_DISCORD_IPC_DIR");
    else
    {
        if (getenv("TMPDIR")) dirs[ndirs++] = getenv("TMPDIR");
#ifdef _CS_DARWIN_USER_TEMP_DIR
        if (confstr(_CS_DARWIN_USER_TEMP_DIR, darwin_tmp, sizeof(darwin_tmp))) dirs[ndirs++] = darwin_tmp;
#else
        (void)darwin_tmp;
#endif
        dirs[ndirs++] = "/tmp";
    }
    for (i = 0; i < ndirs; i++)
    {
        for (j = 0; j < 10; j++)
        {
            size_t len = strlen(dirs[i]);
            snprintf(path, sizeof(path), "%s%sdiscord-ipc-%d", dirs[i],
                     len && dirs[i][len - 1] == '/' ? "" : "/", j);
            if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return -1;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
            if (!connect(fd, (struct sockaddr *)&addr, sizeof(addr)))
            {
                logf_("connected to %s", path);
                return fd;
            }
            close(fd);
        }
    }
    return -1;
}

static void open_fallback(void)
{
    char handshake[96];

    if ((fallback_fd = connect_discord()) < 0)
    {
        logf_("%s", "Discord is not running (no discord-ipc socket); retrying later");
        return;
    }
    snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", ENDFIELD_CLIENT_ID);
    fallback_ready = 0;
    if (send_frame(fallback_fd, OP_HANDSHAKE, handshake)) close_fd(&fallback_fd);
}

static void send_fallback_activity(void)
{
    char json[320];

    snprintf(json, sizeof(json),
             "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":{\"timestamps\":{\"start\":%lld}}},"
             "\"nonce\":\"gryphfield-%lld\"}", (int)getpid(), playing_since, (long long)time(NULL));
    if (send_frame(fallback_fd, OP_FRAME, json)) close_fd(&fallback_fd);
    else logf_("%s", "fallback activity set");
}

/* drain Discord's messages on the fallback socket: the first one is READY (or a CLOSE) */
static void read_fallback(void)
{
    unsigned char header[8];
    char buf[4096];
    uint32_t op, len;
    ssize_t n;

    n = recv(fallback_fd, header, sizeof(header), MSG_WAITALL);
    if (n != (ssize_t)sizeof(header)) { close_fd(&fallback_fd); return; }
    memcpy(&op, header, 4);
    memcpy(&len, header + 4, 4);
    while (len)
    {
        n = recv(fallback_fd, buf, len < sizeof(buf) ? len : sizeof(buf), 0);
        if (n <= 0) { close_fd(&fallback_fd); return; }
        len -= (uint32_t)n;
    }
    if (op == OP_CLOSE) { logf_("%s", "Discord refused the fallback connection"); close_fd(&fallback_fd); return; }
    if (!fallback_ready && op == OP_FRAME)
    {
        fallback_ready = 1;
        send_fallback_activity();
    }
}

static void drop_game(void)
{
    if (game_fd >= 0) logf_("%s", "game connection closed");
    close_fd(&game_fd);
    close_fd(&game_discord_fd);
    game_authed = 0;
    game_line_len = 0;
}

static void accept_game(void)
{
    int fd = accept(listen_fd, NULL, NULL);

    if (fd < 0) return;
    if (game_fd >= 0)   /* one game connection at a time, like Discord's slot 0 */
    {
        close(fd);
        return;
    }
    game_fd = fd;
    game_authed = 0;
    game_line_len = 0;
}

/* the token line, then a plain relay */
static void read_game(void)
{
    char buf[65536];
    ssize_t n = recv(game_fd, buf, sizeof(buf), 0);
    size_t i;

    if (n <= 0) { drop_game(); return; }
    if (!game_authed)
    {
        for (i = 0; i < (size_t)n && buf[i] != '\n'; i++)
        {
            if (game_line_len + 1 >= sizeof(game_line)) { drop_game(); return; }
            game_line[game_line_len++] = buf[i];
        }
        if (i == (size_t)n) return;   /* token line not complete yet */
        game_line[game_line_len] = 0;
        if (strlen(token) != game_line_len || memcmp(game_line, token, game_line_len))
        {
            logf_("%s", "rejected a connection with a wrong token");
            drop_game();
            return;
        }
        /* the game talks to Discord itself now; Discord clears our fallback when it closes */
        close_fd(&fallback_fd);
        if ((game_discord_fd = connect_discord()) < 0)
        {
            logf_("%s", "Discord is not running; closing the game's connection");
            drop_game();
            return;
        }
        game_authed = 1;
        logf_("%s", "relaying the game's Discord connection");
        i++;
        if (i < (size_t)n && write_all(game_discord_fd, buf + i, (size_t)n - i)) drop_game();
        return;
    }
    if (write_all(game_discord_fd, buf, (size_t)n)) drop_game();
}

static void read_game_discord(void)
{
    char buf[65536];
    ssize_t n = recv(game_discord_fd, buf, sizeof(buf), 0);

    if (n <= 0 || write_all(game_fd, buf, (size_t)n)) drop_game();
}

static void handle_command(const char *line)
{
    if (!strncmp(line, "playing", 7))
    {
        long long since = atoll(line + 7);
        playing_since = since > 0 ? since : (long long)time(NULL);
        fallback_retry = 0;
        logf_("%s", "the game is running");
    }
    else if (!strncmp(line, "idle", 4))
    {
        playing_since = 0;
        close_fd(&fallback_fd);
        logf_("%s", "the game is not running");
    }
}

/* unbuffered, so poll() sees every line */
static void read_stdin(void)
{
    static char line[128];
    static size_t len;
    char buf[256];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    ssize_t i;

    if (n <= 0) { stdin_open = 0; return; }
    for (i = 0; i < n; i++)
    {
        if (buf[i] == '\n')
        {
            line[len] = 0;
            handle_command(line);
            len = 0;
        }
        else if (len + 1 < sizeof(line)) line[len++] = buf[i];
    }
}

int main(void)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    struct pollfd fds[5];
    int nfds, i, one = 1;

    signal(SIGPIPE, SIG_IGN);
    token = getenv("GRYPHFIELD_DISCORD_TOKEN");
    if (!token || strlen(token) < 16)
    {
        fprintf(stderr, "gryphfield-discord: GRYPHFIELD_DISCORD_TOKEN is missing\n");
        return 2;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
        bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(listen_fd, 4) ||
        getsockname(listen_fd, (struct sockaddr *)&addr, &addr_len))
    {
        perror("gryphfield-discord: listen");
        return 1;
    }
    printf("port %u\n", ntohs(addr.sin_port));
    fflush(stdout);

    while (stdin_open)
    {
        if (playing_since && game_fd < 0 && fallback_fd < 0 && time(NULL) >= fallback_retry)
        {
            fallback_retry = time(NULL) + RETRY_SECONDS;
            open_fallback();
        }
        nfds = 0;
        fds[nfds++] = (struct pollfd){ .fd = STDIN_FILENO, .events = POLLIN };
        fds[nfds++] = (struct pollfd){ .fd = listen_fd, .events = POLLIN };
        if (game_fd >= 0) fds[nfds++] = (struct pollfd){ .fd = game_fd, .events = POLLIN };
        if (game_discord_fd >= 0) fds[nfds++] = (struct pollfd){ .fd = game_discord_fd, .events = POLLIN };
        if (fallback_fd >= 0) fds[nfds++] = (struct pollfd){ .fd = fallback_fd, .events = POLLIN };
        if (poll(fds, (nfds_t)nfds, 1000) < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        for (i = 0; i < nfds; i++)
        {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            if (fds[i].fd == STDIN_FILENO) read_stdin();
            else if (fds[i].fd == listen_fd) accept_game();
            else if (fds[i].fd == game_fd) read_game();
            else if (fds[i].fd == game_discord_fd) read_game_discord();
            else if (fds[i].fd == fallback_fd) read_fallback();
        }
    }
    drop_game();
    close_fd(&fallback_fd);
    close_fd(&listen_fd);
    return 0;
}
