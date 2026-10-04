#include <objbase.h>
#include "../../epicenter-core/tests/support/TestHarness.hpp"

int main(int argc, char** argv) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int result = epicenter_test::runAll(argc, argv);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return result;
}
