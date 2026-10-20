#include "runtime_probe.h"
#include <stdio.h>
int main(void) {
    char report[8192];
    int result = d2_runtime_probe_run(report, sizeof report);
    puts(report);
    return result;
}
