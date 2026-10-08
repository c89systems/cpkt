cmake_minimum_required(VERSION 3.21)
foreach(name cmocka.h cmocka_types.h)
  file(READ "${CPKT_INCLUDE}/cpkt/${name}" content)
  if(content MATCHES "(^|[^A-Za-z0-9_])(intmax_t|uintmax_t|u?int(8|16|32|64)_t|bool|inline|__func__|__FUNCTION__|__extension__|__attribute__|_Generic)([^A-Za-z0-9_]|$)"
      OR content MATCHES "#[ \t]*include[ \t]*[<\"](stdint|stdbool)\\.h"
      OR content MATCHES "#[ \t]*define[^\n]*\\([^\n)]*\\.\\.\\.")
    message(FATAL_ERROR "Non-C89 token in cmocka public header: ${name}")
  endif()
endforeach()
