#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <thread>

using namespace std;

bool isPrime(int n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0) return false;
    for (int i = 3;i * (long long)i <= n;i += 2) if (n % i == 0) return false;
    return true;
}

void child_process_main() {
    // read "start end\n" from stdin, print primes to stdout
    int start, end;
    if (!(cin >> start >> end)) return;
    for (int x = start; x <= end; ++x) {
        if (isPrime(x)) {
            cout << x << "\n";
        }
    }
    // ensure flushed
    cout.flush();
}

void readerThread(HANDLE hPipeRead, int idx) {
    // Read from pipe in binary and print lines
    const int BUFSZ = 4096;
    char buffer[BUFSZ];
    DWORD bytesRead;
    string partial;
    while (true) {
        BOOL success = ReadFile(hPipeRead, buffer, BUFSZ, &bytesRead, NULL);
        if (!success || bytesRead == 0) break;
        partial.append(buffer, buffer + bytesRead);
        // print complete lines
        size_t pos;
        while ((pos = partial.find('\n')) != string::npos) {
            string line = partial.substr(0, pos + 1);
            cout << "[Child " << idx << "] " << line;
            partial.erase(0, pos + 1);
        }
    }
    if (!partial.empty()) {
        cout << "[Child " << idx << "] " << partial << endl;
    }
    CloseHandle(hPipeRead);
}

int main(int argc, char* argv[]) {
    // If started with "--child" act as child
    if (argc >= 2 && string(argv[1]) == "--child") {
        ios::sync_with_stdio(true);
        cin.tie(nullptr);
        child_process_main();
        return 0;
    }

    const int N = 10000;
    const int P = 10;
    const int chunk = N / P;

    struct ChildInfo {
        HANDLE childStdinWr; // parent writes to this
        HANDLE childStdoutRd; // parent reads from this
        PROCESS_INFORMATION pi;
    };
    vector<ChildInfo> childs(P);

    // get full path to this executable
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    string cmd = string("\"") + exePath + "\" --child";

    for (int i = 0;i < P;i++) {
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        HANDLE childStdinRd = NULL;
        HANDLE childStdinWr = NULL;
        HANDLE childStdoutRd = NULL;
        HANDLE childStdoutWr = NULL;

        // create pipe for child's stdout -> parent
        if (!CreatePipe(&childStdoutRd, &childStdoutWr, &saAttr, 0)) {
            cerr << "CreatePipe stdout failed\n"; return 1;
        }
        // ensure read handle is not inherited
        SetHandleInformation(childStdoutRd, HANDLE_FLAG_INHERIT, 0);

        // create pipe for parent -> child's stdin
        if (!CreatePipe(&childStdinRd, &childStdinWr, &saAttr, 0)) {
            cerr << "CreatePipe stdin failed\n"; return 1;
        }
        // ensure write handle is not inherited
        SetHandleInformation(childStdinWr, HANDLE_FLAG_INHERIT, 0);

        // Set up STARTUPINFO to redirect child's handles
        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdError = childStdoutWr; // combine stderr with stdout
        si.hStdOutput = childStdoutWr;
        si.hStdInput = childStdinRd;
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        BOOL success = CreateProcessA(
            NULL,
            &cmd[0], // command line
            NULL,
            NULL,
            TRUE, // inherit handles
            0,
            NULL,
            NULL,
            &si,
            &pi
        );
        // Close handles that are not needed by parent
        CloseHandle(childStdoutWr);
        CloseHandle(childStdinRd);

        if (!success) {
            cerr << "CreateProcess failed, err=" << GetLastError() << "\n";
            return 1;
        }

        childs[i].childStdinWr = childStdinWr;
        childs[i].childStdoutRd = childStdoutRd;
        childs[i].pi = pi;
    }

    // start reader threads
    vector<thread> readers;
    for (int i = 0;i < P;i++) {
        readers.emplace_back(readerThread, childs[i].childStdoutRd, i);
    }

    // send intervals to children and close write handle
    for (int i = 0;i < P;i++) {
        int s = i * chunk + 1;
        int e = (i + 1) * chunk;
        if (i == P - 1) e = N;
        string msg = to_string(s) + " " + to_string(e) + "\n";
        DWORD written = 0;
        BOOL ok = WriteFile(childs[i].childStdinWr, msg.c_str(), (DWORD)msg.size(), &written, NULL);
        if (!ok) {
            cerr << "WriteFile failed to child " << i << "\n";
        }
        // close the write end so child sees EOF
        CloseHandle(childs[i].childStdinWr);
    }

    // wait for children processes to finish
    for (int i = 0;i < P;i++) {
        WaitForSingleObject(childs[i].pi.hProcess, INFINITE);
        CloseHandle(childs[i].pi.hProcess);
        CloseHandle(childs[i].pi.hThread);
    }

    // join reader threads
    for (auto& t : readers) if (t.joinable()) t.join();

    cout << "Parent: all children finished.\n";
    return 0;
}