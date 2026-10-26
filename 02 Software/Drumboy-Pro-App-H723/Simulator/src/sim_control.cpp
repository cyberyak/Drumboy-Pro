// Command interface for scripted and AI-driven tests.
//
// The same commands come from a script file (--script), stdin (--script -) or
// a Unix socket (--control, used by Simulator/tools/simctl). Each command gets
// a one-line reply starting with "ok" or "error"; `state` replies with JSON.
// `help` lists the commands.

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Read access to the controller's state for `state` and `expect`
#define private public
#include "Controller.h"
#undef private

#include "sim.h"

extern Controller controller;

namespace sim {

static const char* const kMenuNames[] = {
    "INIT",     "GLOBAL",   "FILE",     "DRUMKIT",  "MIXER",        "RHYTHM",       "METRO",        "IO",          "FILTER_0", "FILTER_1",
    "EQ",       "EFFECT_0", "EFFECT_1", "REVERB",   "LFO",          "LAYER_INST_0", "LAYER_INST_1", "LAYER_INST_2", "LAYER_SONG", "TEST"};

static std::string menuName(int menu) {
    int count = sizeof(kMenuNames) / sizeof(kMenuNames[0]);
    return (menu >= 0 && menu < count) ? kMenuNames[menu] : std::to_string(menu);
}

// Relative output paths go under --out
static std::string outputPath(const std::string& path) {
    if (path.empty() || path[0] == '/' || options.outDir.empty()) return path;
    return options.outDir + "/" + path;
}

static void sleepMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static std::string fmt(const char* format, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), format, value);
    return buf;
}

/* Observable values --------------------------------------------- */

// Values usable in `get` and `expect`, read like an ISR would
static std::map<std::string, std::string> snapshot() {
    std::map<std::string, std::string> v;
    lockInterrupts();
    v["menu"] = menuName(controller.menu);
    v["play"] = controller.playActive ? "1" : "0";
    v["record"] = controller.recordActive ? "1" : "0";
    v["alert"] = controller.alertFlag ? "1" : "0";
    v["tempo"] = std::to_string(controller.rhythm.tempo);
    v["layer"] = std::to_string(controller.selectedLayerNum);
    v["bank"] = std::to_string(controller.activeSongBank);
    unlockInterrupts();

    LcdInfo lcd = lcdInfo();
    v["lcd.on"] = (lcd.displayOn && lcd.backlight) ? "1" : "0";
    v["lcd.width"] = std::to_string(lcd.width);
    v["lcd.height"] = std::to_string(lcd.height);
    v["lcd.checksum"] = std::to_string(lcdChecksum());
    v["lcd.pixels"] = std::to_string(lcd.pixels);

    AudioLevel level = audioLevel(200);
    v["audio.blocks"] = std::to_string(audioBlocks());
    v["audio.peak"] = fmt("%.1f", std::max(level.peakDb[0], level.peakDb[1]));
    v["audio.rms"] = fmt("%.1f", std::max(level.rmsDb[0], level.rmsDb[1]));
    v["uptime_ms"] = std::to_string(nowUs() / 1000);
    return v;
}

static std::string stateJson() {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& kv : snapshot()) {
        bool number = !kv.second.empty() && (isdigit(static_cast<unsigned char>(kv.second[0])) || kv.second[0] == '-');
        out << (first ? "" : ", ") << "\"" << kv.first << "\": " << (number ? kv.second : "\"" + kv.second + "\"");
        first = false;
    }
    out << ", \"leds\": " << panelLedsJson() << "}";
    return out.str();
}

static bool compare(const std::string& actual, const std::string& op, const std::string& expected) {
    char* endA;
    char* endE;
    double a = strtod(actual.c_str(), &endA);
    double e = strtod(expected.c_str(), &endE);
    bool numeric = !actual.empty() && !expected.empty() && *endA == 0 && *endE == 0;
    if (op == "==") return numeric ? a == e : actual == expected;
    if (op == "!=") return numeric ? a != e : actual != expected;
    if (!numeric) return false;
    if (op == "<") return a < e;
    if (op == "<=") return a <= e;
    if (op == ">") return a > e;
    if (op == ">=") return a >= e;
    return false;
}

/* Commands ------------------------------------------------------ */

static const char* kHelp =
    "ok commands: "
    "press KEY[+KEY...] [hold_ms] | down KEY | up KEY | turn ENC(0-7) DETENTS | button | "
    "wait MS | wait-ready [timeout_ms] | wait-menu NAME [timeout_ms] | wait-stable [quiet_ms] [timeout_ms] | "
    "screenshot PATH | record PATH MS | level [ms] | state | get FIELD | expect FIELD OP VALUE | keys | warnings | quit [code]";

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(s);
    while (std::getline(in, part, sep)) {
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

static std::string waitFor(const std::function<bool()>& done, uint32_t timeoutMs, const std::string& what) {
    uint64_t end = nowUs() + timeoutMs * 1000ULL;
    while (!done()) {
        if (nowUs() > end) return "error timeout waiting for " + what;
        sleepMs(10);
    }
    return "ok";
}

std::string runCommand(const std::string& line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    std::vector<std::string> args;
    for (std::string a; in >> a;) args.push_back(a);
    auto arg = [&](size_t i, const std::string& fallback) { return i < args.size() ? args[i] : fallback; };
    auto num = [&](size_t i, uint32_t fallback) { return i < args.size() ? static_cast<uint32_t>(strtoul(args[i].c_str(), nullptr, 10)) : fallback; };

    if (cmd.empty() || cmd[0] == '#') return "";
    if (cmd == "help") return kHelp;

    if (cmd == "press") {
        if (args.empty()) return "error usage: press KEY[+KEY...] [hold_ms]";
        std::vector<std::string> keys = split(args[0], '+');
        for (const std::string& k : keys) {
            if (!panelKey(k, true)) return "error unknown key " + k + " (see `keys`)";
            sleepMs(30);                                        // One scan per change: the firmware reads each keypad separately
        }
        sleepMs(num(1, 80));
        for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
            panelKey(*it, false);
            sleepMs(30);
        }
        sleepMs(50);
        return "ok";
    }
    if (cmd == "down" || cmd == "up") {
        if (args.empty() || !panelKey(args[0], cmd == "down")) return "error unknown key " + arg(0, "") + " (see `keys`)";
        sleepMs(30);
        return "ok";
    }
    if (cmd == "keys") {
        std::string out = "ok";
        for (const std::string& k : panelKeyNames()) out += " " + k;
        return out;
    }
    if (cmd == "turn") {
        if (args.size() < 2) return "error usage: turn ENC(0-7) DETENTS";
        int enc = atoi(args[0].c_str());
        if (enc < 0 || enc > 7) return "error encoder must be 0-7";
        panelTurn(enc, atoi(args[1].c_str()));
        sleepMs(30);
        return "ok";
    }
    if (cmd == "button") {
        panelFuncButton(true);
        sleepMs(80);
        panelFuncButton(false);
        return "ok";
    }
    if (cmd == "wait") {
        sleepMs(num(0, 100));
        return "ok";
    }
    if (cmd == "wait-ready") {
        return waitFor([] { return controller.getMenu() != INIT_MENU; }, num(0, 30000), "the main loop");
    }
    if (cmd == "wait-menu") {
        if (args.empty()) return "error usage: wait-menu NAME [timeout_ms]";
        std::string want = args[0];
        return waitFor([want] { return menuName(controller.getMenu()) == want; }, num(1, 5000), "menu " + want);
    }
    if (cmd == "wait-stable") {
        uint32_t quietMs = num(0, 300);
        uint32_t last = lcdChecksum();
        uint64_t since = nowUs();
        return waitFor(
            [&] {
                uint32_t now = lcdChecksum();
                if (now != last) {
                    last = now;
                    since = nowUs();
                }
                return nowUs() - since >= quietMs * 1000ULL;
            },
            num(1, 10000), "a stable screen");
    }
    if (cmd == "screenshot") {
        if (args.empty()) return "error usage: screenshot PATH";
        std::string path = outputPath(args[0]);
        return lcdScreenshot(path) ? "ok " + path : "error cannot write " + path;
    }
    if (cmd == "record") {
        if (args.size() < 2) return "error usage: record PATH MS";
        std::string path = outputPath(args[0]);
        return audioRecord(path, num(1, 1000)) ? "ok " + path : "error cannot write " + path;
    }
    if (cmd == "level") {
        AudioLevel l = audioLevel(num(0, 200));
        char buf[160];
        snprintf(buf, sizeof(buf), "ok peak %.1f %.1f rms %.1f %.1f dBFS", l.peakDb[0], l.peakDb[1], l.rmsDb[0], l.rmsDb[1]);
        return buf;
    }
    if (cmd == "state") return "ok " + stateJson();
    if (cmd == "warnings") {
        std::vector<std::string> list = warnings();
        std::string out = "ok " + std::to_string(list.size());
        for (const std::string& w : list) out += " | " + w;
        return out;
    }
    if (cmd == "get") {
        auto values = snapshot();
        auto it = values.find(arg(0, ""));
        return it == values.end() ? "error unknown field (see `state`)" : "ok " + it->second;
    }
    if (cmd == "expect") {
        if (args.size() < 3) return "error usage: expect FIELD OP VALUE";
        auto values = snapshot();
        auto it = values.find(args[0]);
        if (it == values.end()) return "error unknown field " + args[0] + " (see `state`)";
        if (compare(it->second, args[1], args[2])) return "ok " + args[0] + " = " + it->second;
        return "error expected " + args[0] + " " + args[1] + " " + args[2] + ", got " + it->second;
    }
    if (cmd == "quit") finish(static_cast<int>(num(0, 0)));
    return "error unknown command " + cmd + " (try `help`)";
}

/* Script and socket --------------------------------------------- */

int runScript(const std::string& path) {
    std::ifstream file;
    std::istream* in = &std::cin;
    if (path != "-") {
        file.open(path);
        if (!file) {
            printf("sim: cannot open script %s\n", path.c_str());
            return 2;
        }
        in = &file;
    }
    int lineNumber = 0;
    for (std::string line; std::getline(*in, line);) {
        lineNumber++;
        std::string reply = runCommand(line);
        if (reply.empty()) continue;
        printf("> %s\n%s\n", line.c_str(), reply.c_str());
        if (reply.compare(0, 5, "error") == 0) {
            printf("sim: script failed at line %d\n", lineNumber);
            return 1;
        }
    }
    return 0;
}

static void serveClient(int fd) {
    std::string buffer;
    char chunk[512];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
        buffer.append(chunk, static_cast<size_t>(n));
        size_t eol;
        while ((eol = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, eol);
            buffer.erase(0, eol + 1);
            std::string reply = runCommand(line);
            if (reply.empty()) reply = "ok";
            reply += "\n";
            if (write(fd, reply.data(), reply.size()) < 0) break;
        }
    }
    close(fd);
}

void startControlServer(const std::string& path) {
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        fprintf(stderr, "sim: control socket path longer than %zu bytes, use a shorter (or relative) path\n", sizeof(addr.sun_path) - 1);
        finish(2);
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    unlink(path.c_str());
    if (server < 0 || bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(server, 4) != 0) {
        perror("sim: control socket");
        finish(2);
    }
    printf("sim: control socket %s\n", path.c_str());
    std::thread([server] {
        while (true) {
            int client = accept(server, nullptr, nullptr);
            if (client >= 0) std::thread(serveClient, client).detach();
        }
    }).detach();
}

}  // namespace sim

