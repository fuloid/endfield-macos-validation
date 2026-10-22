// Test the collector's real macOS process identity checks and sampling behavior.
// Compile as a separate test executable; no test bypasses in the shipped helper.
#define main collector_main
#include "../tools/gryphfield-hud.m"
#undef main
#include <assert.h>

static NSArray *read_records(FILE *file) {
    rewind(file);
    NSMutableArray *records = [NSMutableArray array];
    char *line = NULL;
    size_t size = 0;
    while (getline(&line, &size, file) >= 0) {
        NSData *data = [NSData dataWithBytes:line length:strlen(line)];
        id record = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        assert(record);
        [records addObject:record];
    }
    free(line);
    return records;
}

int main(void) {
    @autoreleasepool {
        NSFileManager *fm = NSFileManager.defaultManager;
        NSString *root = [NSTemporaryDirectory() stringByAppendingPathComponent:
            [@"Gryphfield HUD tests " stringByAppendingString:NSUUID.UUID.UUIDString]];
        NSString *dir = [root stringByAppendingPathComponent:@"wine/lib/wine/x86_64-unix"];
        assert([fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL]);
        NSString *loader = [dir stringByAppendingPathComponent:@"wine"];
        assert(!symlink("/bin/sleep", loader.fileSystemRepresentation));
        NSString *prefix = [root stringByAppendingPathComponent:@"prefix with spaces"];
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            setenv("WINEPREFIX", prefix.fileSystemRepresentation, 1);
            execl(loader.fileSystemRepresentation, "sleep", "15", (char *)NULL);
            _exit(127);
        }
        usleep(200000);
        assert(!belongs(getpid(), prefix, root)); // this process is unrelated
        assert(!belongs(child, [prefix stringByAppendingString:@" other"], root));
        assert(belongs(child, prefix, root)); // exact prefix and executable identity
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
        assert(!belongs(child, prefix, root)); // an exited PID is never accepted
        assert([fm removeItemAtPath:root error:NULL]);

        NSArray *input = @[
            @{@"processID": @11, @"timestamp": @"2026-10-22 10:00:00.001", @"frame": @1},
            @{@"processID": @22, @"timestamp": @"2026-10-22 10:00:00.002", @"frame": @10},
            @{@"processID": @11, @"timestamp": @"2026-10-22 10:00:00.999", @"frame": @2},
            @{@"processID": @11, @"timestamp": @"2026-10-22 10:00:01.010", @"frame": @3}
        ];
        for (NSNumber *group in @[@NO, @YES]) {
            FILE *out = tmpfile();
            assert(out);
            NSMutableDictionary *pending = [NSMutableDictionary dictionary];
            for (NSDictionary *record in input) collect_record(out, record, group.boolValue, pending);
            for (NSDictionary *record in pending.allValues) write_record(out, record);
            NSArray *records = read_records(out);
            assert(records.count == (group.boolValue ? 3 : 4));
            if (group.boolValue) {
                assert([records[0][@"frame"] intValue] == 2); // latest, not average or fake 60 Hz
                assert([records containsObject:input[1]]); // other process is not lost
                assert([records containsObject:input[3]]); // final partial second is flushed
            } else assert([records isEqualToArray:input]);
            fclose(out);
        }
        puts("PASS HUD: exact process/prefix attribution, unrelated/exited rejection, raw frames, per-process seconds, final flush");
    }
    return 0;
}
