#!/system/bin/sh
export LIBC_DEBUG_MALLOC_OPTIONS="backtrace=16 guard fill_on_alloc fill_on_free verify_pointers"
exec "$@"
