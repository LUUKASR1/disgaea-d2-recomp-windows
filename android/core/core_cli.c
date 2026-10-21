#include "runtime_probe.h"
#include "elf_probe.h"
#include "dispatch_probe.h"
#include <stdio.h>
int main(void) {
    char report[8192];
    int result = d2_runtime_probe_run(report, sizeof report);
    puts(report);
    if (result == 0) {
        result = d2_elf_probe_run(".", report, sizeof report);
        puts(report);
    }
    if (result == 0) {
        result = d2_dispatch_probe_run(".", report, sizeof report);
        puts(report);
    }
    return result;
}
