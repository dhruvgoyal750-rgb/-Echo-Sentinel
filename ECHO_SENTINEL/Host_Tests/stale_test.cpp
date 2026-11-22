
#include <iostream>
#include <cstdint>

using namespace std;

const uint32_t STALE_LIMIT_US = 150000;

enum SensorStatus {
    SENSOR_OK,
    STALE
};

SensorStatus getStatus(
    SensorStatus originalStatus,
    uint32_t currentTime,
    uint32_t captureTime
) {
    uint32_t age =
        (uint32_t)(currentTime - captureTime);

    if (
        originalStatus == SENSOR_OK &&
        age > STALE_LIMIT_US
    ) {
        return STALE;
    }

    return originalStatus;
}

const char* statusName(SensorStatus status) {
    return status == SENSOR_OK ? "OK" : "STALE";
}

struct TestCase {
    uint32_t captureTime;
    uint32_t currentTime;
    SensorStatus expected;
};

int main() {

    TestCase tests[] = {
        {100000, 100000, SENSOR_OK},
        {100000, 149999, SENSOR_OK},
        {100000, 250000, SENSOR_OK},
        {100000, 250001, STALE},
        {100000, 600000, STALE},

        // 32-bit timer rollover
        {4294967280UL, 32UL, SENSOR_OK}
    };

    int total =
        sizeof(tests) / sizeof(tests[0]);

    int passed = 0;

    cout << "====================================\n";
    cout << " ECHO SENTINEL - STALE TEST\n";
    cout << "====================================\n\n";

    cout << "Test | Age(us) | Expected | Actual | Result\n";
    cout << "-------------------------------------------\n";

    for (int i = 0; i < total; i++) {

        uint32_t age =
            (uint32_t)(
                tests[i].currentTime -
                tests[i].captureTime
            );

        SensorStatus actual = getStatus(
            SENSOR_OK,
            tests[i].currentTime,
            tests[i].captureTime
        );

        bool success =
            actual == tests[i].expected;

        cout << i + 1 << "\t"
             << age << "\t"
             << statusName(tests[i].expected) << "\t"
             << statusName(actual) << "\t"
             << (success ? "PASS" : "FAIL")
             << "\n";

        if (success) passed++;
    }

    cout << "\nTotal tests: " << total << "\n";
    cout << "Passed: " << passed << "\n";
    cout << "Failed: " << total - passed << "\n";

    if (passed == total) {
        cout << "\nM1.7 STALE TEST: PASS\n";
    } else {
        cout << "\nM1.7 STALE TEST: FAIL\n";
    }

    return 0;
}
