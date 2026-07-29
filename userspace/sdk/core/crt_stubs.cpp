/*
 * Freestanding C++ may still emit atexit / __cxa_atexit for static dtors
 * even with -fno-use-cxa-atexit. We never tear down .exec processes cleanly
 * via static destructors — ignore registration.
 */
extern "C" {

int atexit(void (*fn)(void))
{
    (void)fn;
    return 0;
}

int __cxa_atexit(void (*fn)(void), void *arg, void *dso)
{
    (void)fn;
    (void)arg;
    (void)dso;
    return 0;
}

void __cxa_finalize(void *dso)
{
    (void)dso;
}

void *__dso_handle;

}
