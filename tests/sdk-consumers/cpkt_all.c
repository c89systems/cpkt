#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <curl/curl.h>
#include <lauxlib.h>
#include <libssh2.h>
#include <libxml/parser.h>
#include <lua.h>
#include <lualib.h>
#include <miniaudio.h>
#include <nghttp2/nghttp2.h>
#include <open62541/client.h>
#include <open62541/server.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <zlib.h>

static int cpkt_nonnull(const void *value) { return value != NULL; }

static int cpkt_dependency_symbols_are_linkable(void) {
  const char *curl_version;
  const char *ssh2_version;
  nghttp2_info *http2_info;
  SSL_CTX *ctx;
  xmlDocPtr xml_doc;
  lua_State *lua_state;
  UA_Client *opcua_client;
  UA_Server *opcua_server;
  const char *miniaudio_version;
  unsigned long zlib_flags;

  curl_version = curl_version_info(CURLVERSION_NOW)->version;
  if (!cpkt_nonnull(curl_version)) {
    return 1;
  }

  ssh2_version = libssh2_version(0);
  if (!cpkt_nonnull(ssh2_version)) {
    return 2;
  }

  http2_info = nghttp2_version(NGHTTP2_VERSION_NUM);
  if (!cpkt_nonnull(http2_info) || !cpkt_nonnull(http2_info->version_str)) {
    return 3;
  }
  if (!cpkt_nonnull(OpenSSL_version(OPENSSL_VERSION))) {
    return 4;
  }

  ctx = SSL_CTX_new(TLS_client_method());
  if (!cpkt_nonnull(ctx)) {
    return 5;
  }
  SSL_CTX_free(ctx);

  zlib_flags = zlibCompileFlags();
  (void)zlib_flags;

  xml_doc = xmlReadMemory("<root/>", 7, "memory.xml", NULL, 0);
  if (!cpkt_nonnull(xml_doc)) {
    return 6;
  }
  xmlFreeDoc(xml_doc);
  xmlCleanupParser();

  lua_state = luaL_newstate();
  if (!cpkt_nonnull(lua_state)) {
    return 7;
  }
  luaL_openlibs(lua_state);
  lua_close(lua_state);

  miniaudio_version = ma_version_string();
  if (!cpkt_nonnull(miniaudio_version)) {
    return 8;
  }

  opcua_client = UA_Client_new();
  if (!cpkt_nonnull(opcua_client)) {
    return 9;
  }
  UA_Client_delete(opcua_client);

  opcua_server = UA_Server_new();
  if (!cpkt_nonnull(opcua_server)) {
    return 10;
  }
  UA_Server_delete(opcua_server);

  return 0;
}

int main(void) { return cpkt_dependency_symbols_are_linkable(); }
