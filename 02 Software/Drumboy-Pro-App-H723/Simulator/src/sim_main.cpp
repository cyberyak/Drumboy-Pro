// Drumboy Pro simulator entry point.
//
// The firmware's main() is compiled as firmware_main() (see
// Simulator/CMakeLists.txt) and runs on its own thread, like the MCU core; the
// host main thread is left for the window and the simulator itself.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <mach-o/dyld.h>
#include <unistd.h>

#include "Controller.h"
#include "sim.h"

int firmware_main();
extern Controller controller;

static char** savedArgv;

namespace sim {

Options options;

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

static void usage(const char* argv0) {
    printf("Usage: %s [--card IMAGE] [--run-ms N]\n", argv0);
    printf("  --card IMAGE  raw FAT32 SD card image (Simulator/tools/make_card_image.sh)\n");
    printf("  --run-ms N    stop after N ms and report whether the firmware reached its main loop\n");
}

static bool parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--card") && i + 1 < argc) {
            sim::options.cardImage = argv[++i];
        } else if (!strcmp(argv[i], "--run-ms") && i + 1 < argc) {
            sim::options.runMs = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 10));
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
    if (!parseArgs(argc, argv)) return 2;

    sim::mapMemory();
    if (!sim::options.cardImage.empty() && !sim::cardOpen(sim::options.cardImage)) {
        fprintf(stderr, "sim: cannot open card image %s\n", sim::options.cardImage.c_str());
        return 1;
    }
    printf("sim: card %s\n", sim::cardInserted() ? sim::options.cardImage.c_str() : "not inserted");

    sim::startCycleCounter();
    std::thread(firmware_main).detach();

    // Headless runner: report boot progress and stop after --run-ms
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
        _Exit(0);
    }
    printf("sim: firmware did not reach the main loop\n");
    _Exit(1);
}
