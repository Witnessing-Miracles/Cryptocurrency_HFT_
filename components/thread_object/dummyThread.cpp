#include "thread_utils.h"
using namespace std;

auto dummyFunction(int a, int b, bool sleep) {
    cout << "dummyFunction(" << a << "," << b << ")" << endl;
    cout << "dummyFunction output=" << a + b << endl;

    if (sleep) {
        cout << "dummyFunction sleeping..." << endl;
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(5s);

    }
    cout << "dummyFunction done." << endl;
}

int main(int, char **) {
    using namespace Common;
    auto t1 = createAndStartThread(-1, "dummyFunction1", dummyFunction, 12, 21, false);
    auto t2 = createAndStartThread(1, "dummyFunction2", dummyFunction, 15, 51, true);

    cout << "main waiting for threads to be done." << endl;

    t1->join();
    t2->join();

    cout << "main exiting." << endl;
    return 0;
}