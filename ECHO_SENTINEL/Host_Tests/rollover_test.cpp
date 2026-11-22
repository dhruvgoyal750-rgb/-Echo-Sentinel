
#include <iostream>
#include <stdint.h>
using namespace std;

int main() {
    cout << "====================================\n";
    cout << "       M1.5 TIMER ROLLOVER TEST\n";
    cout << "====================================\n\n";

    // Simulate 32-bit microsecond timer rollover
    uint32_t startTime = 4294967280U;
    uint32_t endTime   = 32U;

    // Unsigned subtraction handles rollover correctly
    uint32_t elapsed = endTime - startTime;

    cout << "Start time: " << startTime << " us\n";
    cout << "End time:   " << endTime << " us\n";
    cout << "Elapsed time: " << elapsed << " us\n\n";

    if (elapsed == 48U) {
        cout << "TIMER ROLLOVER TEST: PASS\n";
    } else {
        cout << "TIMER ROLLOVER TEST: FAIL\n";
    }

    cout << "\n====================================\n";
    cout << "      ECHO WIDTH ROLLOVER TEST\n";
    cout << "====================================\n";

    uint32_t risingEdge  = 4294967280U;
    uint32_t fallingEdge = 32U;

    uint32_t pulseWidth = fallingEdge - risingEdge;

    cout << "Rising edge:  " << risingEdge << " us\n";
    cout << "Falling edge: " << fallingEdge << " us\n";
    cout << "Pulse width:  " << pulseWidth << " us\n";

    if (pulseWidth == 48U) {
        cout << "ECHO WIDTH ROLLOVER TEST: PASS\n";
    } else {
        cout << "ECHO WIDTH ROLLOVER TEST: FAIL\n";
    }

    cout << "====================================\n";

    return 0;
}
