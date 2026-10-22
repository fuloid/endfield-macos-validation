// Collect Apple's actual per-frame HUD records for one Wine prefix.
// No inferred FPS or synthetic 60 Hz samples. Build with -framework Foundation.
#import <Foundation/Foundation.h>
#include <libproc.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stopping;
static volatile sig_atomic_t log_pid;
static void stop(int sig) {
    (void)sig;
    stopping = 1;
    if (log_pid > 0) kill(log_pid, SIGTERM);
}

static BOOL same_file(const char *path, NSString *other) {
    struct stat a, b;
    return !stat(path, &a) && !stat(other.fileSystemRepresentation, &b) &&
           a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

// Wine's process naming erases argv/environment and may exec a hard link in
// winetemp-*. Require this engine's loader or mapped ntdll file identity, just
// like CPU target selection. Each engine has one prefix. If WINEPREFIX remains
// readable, also require an exact match (never a substring of ps output).
static BOOL belongs(pid_t pid, NSString *prefix, NSString *engine) {
    static NSMutableDictionary *identities;
    if (!identities) identities = [NSMutableDictionary new];
    struct proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != (int)sizeof(info) || info.pbi_uid != getuid()) return NO;
    NSString *identity = [NSString stringWithFormat:@"%d:%llu:%llu", pid,
        (unsigned long long)info.pbi_start_tvsec, (unsigned long long)info.pbi_start_tvusec];
    if ([identities[identity] boolValue]) return YES;
    char path[PROC_PIDPATHINFO_MAXSIZE];
    BOOL wine = NO;
    NSArray *trees = @[@"wine", @"wine-dxmt", @"wine-d3dmetal"];
    if (proc_pidpath(pid, path, sizeof(path)) > 0) {
        for (NSString *tree in trees) {
            NSString *base = [[engine stringByAppendingPathComponent:tree] stringByAppendingPathComponent:@"lib/wine/x86_64-unix"];
            if (same_file(path, [base stringByAppendingPathComponent:@"wine"]) ||
                same_file(path, [base stringByAppendingPathComponent:@"wine-preloader"])) wine = YES;
        }
    }
    if (!wine) {
        uint64_t address = 0;
        struct proc_regionwithpathinfo region;
        while (proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, address, &region, sizeof(region)) == (int)sizeof(region)) {
            for (NSString *tree in trees) {
                NSString *dll = [[engine stringByAppendingPathComponent:tree] stringByAppendingPathComponent:@"lib/wine/x86_64-unix/ntdll.so"];
                if (same_file(region.prp_vip.vip_path, dll)) wine = YES;
            }
            if (wine) break;
            uint64_t next = region.prp_prinfo.pri_address + region.prp_prinfo.pri_size;
            if (next <= address) break;
            address = next;
        }
    }
    if (!wine) return NO;
    int maximum = 0, argmax[] = {CTL_KERN, KERN_ARGMAX};
    size_t maximum_size = sizeof(maximum);
    if (sysctl(argmax, 2, &maximum, &maximum_size, NULL, 0) || maximum < (int)sizeof(int)) return NO;
    int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t size = (size_t)maximum;
    NSMutableData *buffer = [NSMutableData dataWithLength:size];
    if (sysctl(mib, 3, buffer.mutableBytes, &size, NULL, 0)) return NO;
    char *p = buffer.mutableBytes, *end = p + size;
    int argc;
    memcpy(&argc, p, sizeof(argc));
    p += sizeof(argc);
    // Skip executable path, alignment NULs, then argc argument strings.
    while (p < end && *p) p++;
    while (p < end && !*p) p++;
    for (int i = 0; i < argc && p < end; i++) {
        while (p < end && *p) p++;
        if (p < end) p++;
    }
    NSData *wanted = [[@"WINEPREFIX=" stringByAppendingString:prefix] dataUsingEncoding:NSUTF8StringEncoding];
    while (p < end) {
        size_t n = strnlen(p, end - p);
        if (n >= 11 && !memcmp(p, "WINEPREFIX=", 11)) {
            if (n != wanted.length || memcmp(p, wanted.bytes, n)) return NO;
            identities[identity] = @YES;
            return YES;
        }
        p += n;
        if (p < end) p++;
    }
    identities[identity] = @YES;
    return YES;
}

static void write_record(FILE *out, NSDictionary *record) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:NULL];
    if (data) { fwrite(data.bytes, 1, data.length, out); fputc('\n', out); fflush(out); }
}

static void collect_record(FILE *out, NSDictionary *r, BOOL grouped, NSMutableDictionary *pending) {
    if (!grouped) { write_record(out, r); return; }
    NSNumber *pid = r[@"processID"];
    NSString *second = [r[@"timestamp"] substringToIndex:19];
    NSDictionary *previous = pending[pid];
    if (previous && ![[previous[@"timestamp"] substringToIndex:19] isEqualToString:second])
        write_record(out, previous);
    pending[pid] = r;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc != 5) { fprintf(stderr, "usage: gryphfield-hud <prefix> <engine> <1s|frame> <log>\n"); return 2; }
        NSString *prefix = [NSString stringWithUTF8String:argv[1]];
        NSString *engine = [NSString stringWithUTF8String:argv[2]];
        BOOL grouped = !strcmp(argv[3], "1s");
        FILE *out = fopen(argv[4], "a");
        if (!out) return 1;
        struct sigaction action = {0};
        action.sa_handler = stop;
        sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, NULL);
        sigaction(SIGINT, &action, NULL);
        int fds[2];
        if (pipe(fds)) { fclose(out); return 1; }
        pid_t child = fork();
        if (child == 0) {
            dup2(fds[1], STDOUT_FILENO);
            close(fds[0]); close(fds[1]); fclose(out);
            execl("/usr/bin/log", "log", "stream", "--style", "ndjson", "--level", "debug",
                  "--predicate", "subsystem == 'com.apple.metal.hud' OR eventMessage BEGINSWITH 'metal-HUD:'", (char *)NULL);
            _exit(127);
        }
        close(fds[1]);
        if (child < 0) { close(fds[0]); fclose(out); return 1; }
        log_pid = child;
        if (stopping) kill(child, SIGTERM);
        FILE *input = fdopen(fds[0], "r");
        if (!input) { close(fds[0]); kill(child, SIGTERM); waitpid(child, NULL, 0); fclose(out); return 1; }
        char *line = NULL;
        size_t capacity = 0;
        unsigned long records = 0;
        NSMutableDictionary *pending = [NSMutableDictionary dictionary];
        while (!stopping && getline(&line, &capacity, input) >= 0) {
            @autoreleasepool {
                NSData *data = [NSData dataWithBytes:line length:strlen(line)];
                NSDictionary *r = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
                if (![r isKindOfClass:NSDictionary.class]) continue;
                NSNumber *pid = r[@"processID"];
                NSString *stamp = r[@"timestamp"], *message = r[@"eventMessage"];
                if (![pid isKindOfClass:NSNumber.class] || ![stamp isKindOfClass:NSString.class] ||
                    ![message isKindOfClass:NSString.class] || stamp.length < 19) continue;
                // Inspect live process identity for each record; never collect another
                // app's HUD just because its process name or PID once matched.
                if (!belongs(pid.intValue, prefix, engine)) continue;
                records++;
                collect_record(out, r, grouped, pending);
            }
        }
        for (NSDictionary *r in pending.allValues) write_record(out, r);
        free(line);
        fclose(input);
        kill(child, SIGTERM);
        // Bound cleanup even if the logging service stops responding.
        int status = 0, waited = 0;
        while (waitpid(child, &status, WNOHANG) == 0 && waited++ < 20) usleep(100000);
        if (waited >= 20) { kill(child, SIGKILL); waitpid(child, &status, 0); }
        log_pid = 0;
        fprintf(out, "# Collected %lu attributed HUD records; zero means unavailable or no presented frames.\n", records);
        fclose(out);
        return stopping || (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
    }
}
