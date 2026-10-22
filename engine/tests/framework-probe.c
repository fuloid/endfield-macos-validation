#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    void *handle;
    if (argc != 2) return 2;
    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    puts("D3DMetal framework: loaded with immediate symbol resolution");
    dlclose(handle);
    return 0;
}
