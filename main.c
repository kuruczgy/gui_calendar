#include "application.h"

int main(int argc, char **argv) {
    struct backend backend = 
#ifdef BACKEND_DUMMY
    backend_init_dummy();
#elif BACKEND_FBDEV
    backend_init_fbdev();
#else
    backend_init_wayland();
#endif
    if (!backend.self) return 1;
    return application_main(argc, argv, &backend);
}
