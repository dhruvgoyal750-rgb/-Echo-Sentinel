
#include <iostream>
#include <cstdint>

using namespace std;

const uint32_t MIN_DISTANCE_MM = 20;
const uint32_t MAX_DISTANCE_MM = 4000;

enum SensorStatus {
    SENSOR_OK,
    OUT_OF_RANGE
};

SensorStatus validateDistance(uint32_t distance) {

    if (
        distance < MIN_DISTANCE_MM ||
        distance > MAX_DISTANCE_MM
    ) {
        return OUT_OF_RANGE;
    }

    return SENSOR_OK;
}

const char* statusName(SensorStatus status) {
    return status == SENSOR_OK ? "OK" : "OUT_OF_RANGE";
}

struct TestCase {
    uint32_t distance;
    SensorStatus expected;
};

int main() {

    TestCase tests[] = {
        {0, OUT_OF_RANGE},
        {19, OUT_OF_RANGE},
        {20, SENSOR_OK},
        {21, SENSOR_OK},
        {100, SENSOR_OK},
        {1000, SENSOR_OK},
        {3999, SENSOR_OK},
        {4000, SENSOR_OK},
        {4001, OUT_OF_RANGE},
        {5000, OUT_OF_RANGE}
    };

    int total = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;

    cout << "====================================\n";
    cout << " ECHO SENTINEL - RANGE TEST\n";
    cout << "====================================\n\n";

    cout << "Distance | Expected     | Actual       | Result\n";
    cout << "------------------------------------------------\n";

    for (int i = 0; i < total; i++) {

        SensorStatus actual =
            validateDistance(tests[i].distance);

        bool success = actual == tests[i].expected;

        cout << tests[i].distance << " mm\t"
             << statusName(tests[i].expected) << "\t"
             << statusName(actual) << "\t"
             << (success ? "PASS" : "FAIL")
             << "\n";

        if (success) passed++;
    }

    cout << "\nTotal tests: " << total;
    cout << "\nPassed: " << passed;
    cout << "\nFailed: " << total - passed;

    if (passed == total) {
        cout << "\n\nOUT_OF_RANGE HOST TEST: PASS\n";
    } else {
        cout << "\n\nOUT_OF_RANGE HOST TEST: FAIL\n";
    }

    return 0;
}
