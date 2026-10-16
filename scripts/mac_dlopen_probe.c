/* Resolve the same three absolute dylib names used by the configured Wine.
 * Compile and execute as x86_64 on Apple Silicon; no GPU rendering is attempted. */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *symbols[] = {"FT_Init_FreeType", "gnutls_global_init", "vkGetInstanceProcAddr"};
    if (argc != 4) return 2;
    for (int i=0; i<3; i++)
    {
        void *handle=dlopen(argv[i+1], RTLD_NOW|RTLD_LOCAL);
        if (!handle)
        {
            fprintf(stderr,"FAIL dlopen %s: %s\n",argv[i+1],dlerror());
            return 1;
        }
        if (!dlsym(handle,symbols[i]))
        {
            fprintf(stderr,"FAIL symbol %s in %s: %s\n",symbols[i],argv[i+1],dlerror());
            dlclose(handle);
            return 1;
        }
        printf("PASS x86_64 runtime library: %s (%s)\n",argv[i+1],symbols[i]);
        dlclose(handle);
    }
    return 0;
}
