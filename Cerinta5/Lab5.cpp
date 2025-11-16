#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <thread>

using namespace std;

bool isPrime(int n){
    if(n <= 1) return false;
    if(n <= 3) return true;
    if(n%2==0) return false;
    for(int i=3;i*(long long)i<=n;i+=2) if(n%i==0) return false;
    return true;
}

void readerThread(int fd, int idx){
    // fd is read-end from child -> parent
    FILE* f = fdopen(fd, "r");
    if(!f){
        perror("fdopen");
        return;
    }
    char *line = nullptr;
    size_t len = 0;
    while(true){
        ssize_t r = getline(&line, &len, f);
        if(r == -1) break;
        // print as received
        cout << "[Child " << idx << "] " << string(line, r);
        fflush(stdout);
    }
    free(line);
    fclose(f);
}

int main(){
    const int N = 10000;
    const int P = 10;
    const int chunk = N / P;

    struct ChildInfo {
        int parent_to_child_fd; // write end in parent
        int child_to_parent_fd; // read end in parent
        pid_t pid;
    };
    vector<ChildInfo> childs(P);

    for(int i=0;i<P;i++){
        int ptc[2]; // parent -> child
        int ctp[2]; // child -> parent

        if(pipe(ptc) == -1){ perror("pipe ptc"); exit(1); }
        if(pipe(ctp) == -1){ perror("pipe ctp"); exit(1); }

        pid_t pid = fork();
        if(pid < 0){ perror("fork"); exit(1); }

        if(pid == 0){
            // child
            // close unused ends
            close(ptc[1]); // parent write end closed in child
            close(ctp[0]); // parent read end closed in child

            // read interval from ptc[0]
            FILE* in = fdopen(ptc[0], "r");
            FILE* out = fdopen(ctp[1], "w");
            if(!in || !out){ perror("fdopen child"); exit(1); }

            int start, end;
            if(fscanf(in, "%d %d", &start, &end) == 2){
                // compute primes and send them
                for(int x = start; x <= end; ++x){
                    if(isPrime(x)){
                        fprintf(out, "%d\n", x);
                    }
                }
            }
            // cleanup and exit
            fclose(in);
            fclose(out);
            _exit(0);
        } else {
            // parent
            // close unused ends
            close(ptc[0]); // close child read end in parent
            close(ctp[1]); // close child write end in parent

            childs[i].parent_to_child_fd = ptc[1];
            childs[i].child_to_parent_fd = ctp[0];
            childs[i].pid = pid;
        }
    }

    // Launch reader threads to read from each child concurrently
    vector<thread> readers;
    for(int i=0;i<P;i++){
        readers.emplace_back(readerThread, childs[i].child_to_parent_fd, i);
    }

    // Send intervals to each child
    for(int i=0;i<P;i++){
        int s = i*chunk + 1;
        int e = (i+1)*chunk;
        // last chunk adjust (in case N not divisible)
        if(i == P-1) e = N;
        string msg = to_string(s) + " " + to_string(e) + "\n";
        ssize_t written = write(childs[i].parent_to_child_fd, msg.c_str(), (ssize_t)msg.size());
        if(written < 0){
            perror("write to child");
        }
        // close write end so child receives EOF and can finish
        close(childs[i].parent_to_child_fd);
    }

    // wait for children to finish
    for(int i=0;i<P;i++){
        int status;
        waitpid(childs[i].pid, &status, 0);
    }

    // join reader threads
    for(auto &t : readers) if(t.joinable()) t.join();

    cout << "Parent: all children finished.\n";
    return 0;
}
