#include <ldap.h>
#include <stddef.h>

int main(void) {
  BerElement *element;
  const char *description;

  element = ber_alloc_t(LBER_USE_DER);
  if (element == NULL) {
    return 1;
  }
  ber_free(element, 1);
  description = ldap_err2string(LDAP_SUCCESS);
  return description == NULL || description[0] == '\0';
}
