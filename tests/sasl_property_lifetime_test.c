#include <cpkt/sasl.h>

#include <stdio.h>
#include <string.h>

int main(void) {
  cpkt_sasl_property_context *context;
  const cpkt_sasl_property_value *first, *again;
  cpkt_sasl_property_value selected[2];
  const char *names[] = {"present", "absent", 0};
  const char *empty[] = {0};
  size_t count = 0;
  int found;
  context = cpkt_sasl_property_new(4);
  if (context == 0 ||
      cpkt_sasl_property_request(context, names) != CPKT_SASL_OK ||
      cpkt_sasl_property_set(context, "present", "one", 3) != CPKT_SASL_OK)
    return 1;
  first = cpkt_sasl_property_get(context, &count);
  if (first == 0 || count != 2 || first[0].value_count != 1 ||
      strcmp(first[0].values[0], "one") != 0)
    return 2;
  again = cpkt_sasl_property_get(context, &count);
  if (again != first || count != 2)
    return 3;
  memset(selected, 0, sizeof(selected));
  found = cpkt_sasl_property_getnames(context, names, selected, 2, &count);
  if (found != 2 || count != 2 || selected[0].value_count != 1 ||
      selected[1].value_count != 0 ||
      strcmp(selected[0].values[0], "one") != 0 ||
      strcmp(first[0].values[0], "one") != 0) {
    fprintf(stderr, "found=%d count=%lu a=%lu b=%lu\n", found,
            (unsigned long)count, selected[0].value_count,
            selected[1].value_count);
    return 4;
  }
  if (cpkt_sasl_property_getnames(context, empty, 0, 0, &count) != 0 ||
      cpkt_sasl_property_getnames(context, names, selected, 1, &count) !=
          CPKT_SASL_BUFOVER ||
      strcmp(first[0].values[0], "one") != 0)
    return 5;
  if (cpkt_sasl_property_set(context, "present", "two", 3) != CPKT_SASL_OK)
    return 6;
  again = cpkt_sasl_property_get(context, &count);
  if (again == 0 || count != 2 || again[0].value_count != 2 ||
      strcmp(again[0].values[1], "two") != 0)
    return 7;
  cpkt_sasl_property_dispose(&context);
  return context == 0 ? 0 : 8;
}
