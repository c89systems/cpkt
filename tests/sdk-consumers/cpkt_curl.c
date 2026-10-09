#include <curl/curl.h>

int main(void) { return curl_version_info(CURLVERSION_NOW)->version == 0; }
