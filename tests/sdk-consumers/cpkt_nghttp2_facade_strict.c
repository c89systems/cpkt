#include <cpkt/nghttp2.h>

int main(void) {
  const cpkt_nghttp2_info *info;

  info = cpkt_nghttp2_version(0);
  return info == 0 || info->version_num != CPKT_NGHTTP2_VERSION_NUM;
}
