#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

static void add_seven(sqlite3_context *context, int argc,
                      sqlite3_value **arguments) {
  if (argc != 1) {
    sqlite3_result_error(context, "argument count", -1);
    return;
  }
  sqlite3_result_int(context, sqlite3_value_int(arguments[0]) + 7);
}

int sqlite3_cpktprobe_init(sqlite3 *database, char **error_out,
                           const sqlite3_api_routines *api) {
  (void)error_out;
  SQLITE_EXTENSION_INIT2(api)
  return sqlite3_create_function(database, "cpkt_add_seven", 1, SQLITE_UTF8, 0,
                                 add_seven, 0, 0);
}
