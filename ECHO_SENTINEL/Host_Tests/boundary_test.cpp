
#include <iostream>
#include <cstdint>

using namespace std;

const uint32_t MIN_DISTANCE_MM = 20;
const uint32_t MAX_DISTANCE_MM = 4000;

uint32_t convertDistance(uint32_t pulseWidthUs) {
    return (pulseWidthUs * 10U + 29U) / 58U;
}

bool isInRange(uint32_t distanceMm) {
    return (
        distanceMm >= MIN_DISTANCE_MM &&
        distanceMm <= MAX_DISTANCE_MM
    );
}

struct TestCase {
    uint32_t pulseUs;
    uint32_t expectedMm;
    bool expectedValid;
};

int main() {

    TestCase tests[] = {
        {0,     0,    false},
        {58,    10,   false},
        {116,   20,   true},
        {290,   50,   true},
        {580,   100,  true},
        {5800,  1000, true},
        {8120,  1400, true},
        {11600, 2000, true},
        {23200, 4000, true},
        {23258, 4010, false},
        {30000, 5172, false},
        {40000, 6897, false}
    };

    const int totalTests =
        sizeof(tests) / sizeof(tests[0]);

    int passed = 0;

    cout << "====================================\n";
    cout << " ECHO SENTINEL - BOUNDARY TEST\n";
    cout << "====================================\n\n";

    cout << "Test | Pulse(us) | Distance(mm) | Expected | Result\n";
    cout << "---------------------------------------------------\n";

    for (int i = 0; i < totalTests; i++) {

        uint32_t actual =
            convertDistance(tests[i].pulseUs);

        bool valid =
            isInRange(actual);

        bool distanceCorrect =
            (actual == tests[i].expectedMm);

        bool rangeCorrect =
            (valid == tests[i].expectedValid);

        bool testPassed =
            distanceCorrect && rangeCorrect;

        cout << i + 1 << "\t"
             << tests[i].pulseUs << "\t\t"
             << actual << "\t\t"
             << (tests[i].expectedValid ? "VALID" : "INVALID")
             << "\t"
             << (testPassed ? "PASS" : "FAIL")
             << "\n";

        if (testPassed) {
            passed++;
        }
    }

    cout << "\nTotal tests: " << totalTests << "\n";
    cout << "Passed: " << passed << "\n";
    cout << "Failed: " << totalTests - passed << "\n";

    if (passed == totalTests) {
        cout << "\nM1.6 BOUNDARY TEST: PASS\n";
    } else {
        cout << "\nM1.6 BOUNDARY TEST: FAIL\n";
    }

    return 0;
}
