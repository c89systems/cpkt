#include <nghttp2/nghttp2.h>

int main(void) {
  nghttp2_info *info = nghttp2_version(NGHTTP2_VERSION_NUM);
  return info == 0 || info->version_str == 0;
}
