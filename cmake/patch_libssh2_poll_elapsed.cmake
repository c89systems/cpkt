if(NOT DEFINED CPKT_LIBSSH2_SOURCE_DIR OR CPKT_LIBSSH2_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_LIBSSH2_SOURCE_DIR is required")
endif()

set(source_path "${CPKT_LIBSSH2_SOURCE_DIR}/src/session.c")
file(READ "${source_path}" source_text)
string(FIND "${source_text}" "cpkt_libssh2_subtract_poll_elapsed(long *remaining" already_patched)
if(NOT already_patched EQUAL -1)
  return()
endif()

set(function_anchor [=[LIBSSH2_API int
libssh2_poll(]=])
set(helper [=[#if defined(HAVE_POLL) || defined(HAVE_SELECT)
/* The public timeout is long, while 32-bit musl uses 64-bit timeval fields.
 * Bound the elapsed interval before narrowing it back to the API's long. */
static void
cpkt_libssh2_subtract_poll_elapsed(long *remaining,
                                  const struct timeval *begin,
                                  const struct timeval *end)
{
    unsigned long long seconds;
    suseconds_t microseconds;
    unsigned long long elapsed_ms;

    if(*remaining <= 0 || end->tv_sec < begin->tv_sec)
        return;
    seconds = (unsigned long long)end->tv_sec -
        (unsigned long long)begin->tv_sec;
    if(seconds > (unsigned long long)(*remaining / 1000) + 1) {
        *remaining = 0;
        return;
    }
    microseconds = end->tv_usec - begin->tv_usec;
    elapsed_ms = seconds * 1000;
    if(microseconds < 0) {
        unsigned long long fraction =
            (unsigned long long)(-(microseconds / 1000));
        if(elapsed_ms <= fraction)
            return;
        elapsed_ms -= fraction;
    }
    else
        elapsed_ms += (unsigned long long)(microseconds / 1000);
    if(elapsed_ms >= (unsigned long long)*remaining) {
        *remaining = 0;
        return;
    }
    *remaining -= (long)elapsed_ms;
}
#endif

LIBSSH2_API int
libssh2_poll(]=])
set(old_elapsed [=[            timeout_remaining -= (tv_end.tv_sec - tv_begin.tv_sec) * 1000;
            timeout_remaining -= (tv_end.tv_usec - tv_begin.tv_usec) / 1000;]=])
set(new_elapsed [=[            cpkt_libssh2_subtract_poll_elapsed(&timeout_remaining,
                                                &tv_begin, &tv_end);]=])

string(FIND "${source_text}" "${function_anchor}" function_position)
if(function_position EQUAL -1)
  message(FATAL_ERROR "libssh2_poll insertion anchor is missing")
endif()
string(REGEX MATCHALL "timeout_remaining -= \\(tv_end[.]tv_sec - tv_begin[.]tv_sec\\) \\* 1000" old_occurrences "${source_text}")
list(LENGTH old_occurrences old_count)
if(NOT old_count EQUAL 2)
  message(FATAL_ERROR "libssh2_poll must have exactly two elapsed-time branches; found ${old_count}")
endif()
string(REPLACE "${function_anchor}" "${helper}" source_text "${source_text}")
string(REPLACE "${old_elapsed}" "${new_elapsed}" source_text "${source_text}")
file(WRITE "${source_path}" "${source_text}")
