#include <libxml/parser.h>

int main(void) {
  xmlDocPtr doc = xmlReadMemory("<root/>", 7, "memory.xml", 0, 0);
  if (doc == 0) {
    return 1;
  }
  xmlFreeDoc(doc);
  xmlCleanupParser();
  return 0;
}
