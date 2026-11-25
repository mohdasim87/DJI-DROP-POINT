#include "test_framework.h"

#include "util/crt_silence.h"

int main() {
    // Never a modal dialog from a CRT assertion — stderr + non-zero exit.
    droppoint::util::SilenceCrtDialogs();
    return droppoint::test::RunAll();
}
