#include <check.h>
#include <stdio.h>
#include "malloc_suite.h"

int main(void) {
    Suite *s = malloc_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_VERBOSE);
    srunner_free(sr);
    return 0;
}
