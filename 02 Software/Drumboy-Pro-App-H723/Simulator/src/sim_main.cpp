// Drumboy Pro simulator entry point.
//
// The firmware's main() is compiled as firmware_main() (see
// Simulator/CMakeLists.txt) and runs on its own thread, like the MCU core.
// The host main thread runs the test script, or waits while a client drives
// the simulator over the control socket.

#include <execinfo.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/clonefile.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "Controller.h"
#include "sim.h"

int firmware_main();
extern Controller controller;

static char** savedArgv;
static char cardClonePath[1024];                                // Removed on exit when --card-copy is used

namespace sim {

Options options;

void finish(int code) {
    fflush(stdout);
    if (cardClonePath[0]) unlink(cardClonePath);
    _Exit(code);
}

void restart() {
    printf("sim: system reset\n");
    fflush(stdout);
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) execv(path, savedArgv);
    perror("sim: restart failed");
    _Exit(1);
}

}  // namespace sim

// Firmware crashes print the faulting address and a backtrace, so a failed
// test run shows where (symbolize with: atos -o DrumboySim -l LOAD_ADDRESS PC)
static void crashHandler(int sig, siginfo_t* info, void* context) {
    auto* uc = static_cast<ucontext_t*>(context);
    fprintf(stderr, "sim: crashed with signal %d at pc 0x%llx (address %p), load address %p\n", sig,
            static_cast<unsigned long long>(uc->uc_mcontext->__ss.__rip), info->si_addr, static_cast<const void*>(_dyld_get_image_header(0)));
    void* frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, 2);
    if (cardClonePath[0]) unlink(cardClonePath);
    _Exit(128 + sig);
}

static void usage(const char* argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("  --card IMAGE       raw FAT32 SD card image (Simulator/tools/make_card_image.sh)\n");
    printf("  --card-copy        run on a temporary clone of the image, leaving it unchanged\n");
    printf("  --out DIR          directory for relative screenshot/record paths\n");
    printf("  --script FILE      run test commands from FILE (- = stdin), exit 0 if all pass\n");
    printf("  --control SOCKET   accept commands on a Unix socket (Simulator/tools/simctl)\n");
    printf("  --run-ms N         stop after N ms; exit 0 if the firmware reached its main loop\n");
    printf("  --delay-scale F    multiply HAL_Delay by F (e.g. 0.1 skips most of the boot animation)\n");
}

static bool parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        bool hasValue = i + 1 < argc;
        if (!strcmp(argv[i], "--card") && hasValue) {
            sim::options.cardImage = argv[++i];
        } else if (!strcmp(argv[i], "--card-copy")) {
            sim::options.cardCopy = true;
        } else if (!strcmp(argv[i], "--out") && hasValue) {
            sim::options.outDir = argv[++i];
        } else if (!strcmp(argv[i], "--script") && hasValue) {
            sim::options.script = argv[++i];
        } else if (!strcmp(argv[i], "--control") && hasValue) {
            sim::options.controlSocket = argv[++i];
        } else if (!strcmp(argv[i], "--run-ms") && hasValue) {
            sim::options.runMs = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 10));
        } else if (!strcmp(argv[i], "--delay-scale") && hasValue) {
            sim::options.delayScale = strtof(argv[++i], nullptr);
        } else {
            usage(argv[0]);
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    savedArgv = argv;
    struct sigaction action {};
    action.sa_sigaction = crashHandler;
    action.sa_flags = SA_RESETHAND | SA_SIGINFO;
    for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) sigaction(sig, &action, nullptr);
    if (!parseArgs(argc, argv)) return 2;

    sim::mapMemory();
    // A restart (NVIC_SystemReset) re-execs with the same arguments and keeps using the same clone
    if (!sim::options.cardImage.empty() && sim::options.cardCopy) {
        const char* reuse = getenv("DRUMBOY_SIM_CARD_CLONE");
        if (reuse) {
            snprintf(cardClonePath, sizeof(cardClonePath), "%s", reuse);
        } else {
            const char* tmp = getenv("TMPDIR");
            snprintf(cardClonePath, sizeof(cardClonePath), "%s/drumboy-card-%d.img", tmp ? tmp : "/tmp", getpid());
            unlink(cardClonePath);
            if (clonefile(sim::options.cardImage.c_str(), cardClonePath, 0) != 0) {
                perror("sim: cannot clone the card image");
                return 1;
            }
            setenv("DRUMBOY_SIM_CARD_CLONE", cardClonePath, 1);
        }
        sim::options.cardImage = cardClonePath;
    }
    if (!sim::options.cardImage.empty() && !sim::cardOpen(sim::options.cardImage)) {
        fprintf(stderr, "sim: cannot open card image %s\n", sim::options.cardImage.c_str());
        return 1;
    }
    printf("sim: card %s\n", sim::cardInserted() ? sim::options.cardImage.c_str() : "not inserted");

    sim::startCycleCounter();
    sim::startTimers();
    if (!sim::options.controlSocket.empty()) sim::startControlServer(sim::options.controlSocket);
    std::thread(firmware_main).detach();

    if (!sim::options.script.empty()) {
        sim::finish(sim::runScript(sim::options.script));
    }

    // Without a script: report boot progress, stop after --run-ms
    Menu lastMenu = INIT_MENU;
    uint64_t mainLoopUs = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Menu menu = controller.getMenu();
        if (menu != lastMenu) {
            printf("sim: %6.3f s  menu %d\n", sim::nowUs() / 1e6, static_cast<int>(menu));
            if (!mainLoopUs && menu != INIT_MENU && menu != TEST_MENU) mainLoopUs = sim::nowUs();
            lastMenu = menu;
        }
        if (sim::options.runMs && sim::nowUs() >= sim::options.runMs * 1000ULL) break;
    }

    if (mainLoopUs) {
        printf("sim: firmware reached the main loop after %.3f s\n", mainLoopUs / 1e6);
        sim::finish(0);
    }
    printf("sim: firmware did not reach the main loop\n");
    sim::finish(1);
}
