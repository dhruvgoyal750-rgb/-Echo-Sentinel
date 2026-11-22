
#include <iostream>
#include <windows.h>
#include <cstdint>

using namespace std;

struct Snapshot {
    uint32_t seq;
    uint32_t distance_mm;
    uint32_t status;
    uint32_t timestamp;
};

Snapshot sharedSnapshot = {0, 0, 0, 0};

CRITICAL_SECTION snapshotLock;

volatile LONG writerFinished = 0;
volatile LONG totalReads = 0;
volatile LONG totalWrites = 0;
volatile LONG mismatches = 0;

const uint32_t ITERATIONS = 1000000UL;

// Publish snapshot safely
void publishSnapshot(uint32_t n) {

    Snapshot s;

    s.seq = n;
    s.distance_mm = n ^ 0xA5A55A5AUL;
    s.status = (n & 1U) ? 1U : 2U;
    s.timestamp = n ^ 0x5AA5A55AUL;

    EnterCriticalSection(&snapshotLock);

    sharedSnapshot = s;

    LeaveCriticalSection(&snapshotLock);

    InterlockedIncrement(&totalWrites);
}

// Read snapshot safely
Snapshot readSnapshot() {

    Snapshot copy;

    EnterCriticalSection(&snapshotLock);

    copy = sharedSnapshot;

    LeaveCriticalSection(&snapshotLock);

    return copy;
}

// Validate snapshot consistency
bool validateSnapshot(const Snapshot &s) {

    if (s.seq == 0) {
        return true;
    }

    uint32_t expectedDistance =
        s.seq ^ 0xA5A55A5AUL;

    uint32_t expectedStatus =
        (s.seq & 1U) ? 1U : 2U;

    uint32_t expectedTimestamp =
        s.seq ^ 0x5AA5A55AUL;

    return (
        s.distance_mm == expectedDistance &&
        s.status == expectedStatus &&
        s.timestamp == expectedTimestamp
    );
}

// Writer thread
DWORD WINAPI writerTask(LPVOID) {

    for (uint32_t i = 1; i <= ITERATIONS; i++) {
        publishSnapshot(i);
    }

    InterlockedExchange(&writerFinished, 1);

    return 0;
}

// Reader thread
DWORD WINAPI readerTask(LPVOID) {

    while (
        InterlockedCompareExchange(
            &writerFinished, 0, 0
        ) == 0 ||
        InterlockedCompareExchange(
            &totalReads, 0, 0
        ) < (LONG)ITERATIONS
    ) {

        Snapshot s = readSnapshot();

        if (!validateSnapshot(s)) {
            InterlockedIncrement(&mismatches);
        }

        InterlockedIncrement(&totalReads);
    }

    return 0;
}

// Main
int main() {

    cout << "====================================\n";
    cout << " ECHO SENTINEL - SNAPSHOT STRESS TEST\n";
    cout << "====================================\n";

    InitializeCriticalSection(&snapshotLock);

    HANDLE writer = CreateThread(
        NULL, 0, writerTask, NULL, 0, NULL
    );

    HANDLE reader = CreateThread(
        NULL, 0, readerTask, NULL, 0, NULL
    );

    if (writer == NULL || reader == NULL) {
        cout << "ERROR: Thread creation failed.\n";

        if (writer != NULL) CloseHandle(writer);
        if (reader != NULL) CloseHandle(reader);

        DeleteCriticalSection(&snapshotLock);
        return 1;
    }

    WaitForSingleObject(writer, INFINITE);
    WaitForSingleObject(reader, INFINITE);

    CloseHandle(writer);
    CloseHandle(reader);

    cout << "\nTEST RESULTS\n";

    cout << "Writes: "
         << totalWrites << "\n";

    cout << "Reads: "
         << totalReads << "\n";

    cout << "Mismatches: "
         << mismatches << "\n";

    if (
        totalWrites == (LONG)ITERATIONS &&
        totalReads >= (LONG)ITERATIONS &&
        mismatches == 0
    ) {
        cout << "\nM1.4 HOST STRESS TEST: PASS\n";
    } else {
        cout << "\nM1.4 HOST STRESS TEST: FAIL\n";
    }

    DeleteCriticalSection(&snapshotLock);

    return 0;
}
