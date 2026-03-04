#include "manager/PrinterManager.h"
#include "protocol/TsplHandler.h"
#include "protocol/EscpHandler.h"
#include "protocol/PclHandler.h"
#include "transport/UsbDeviceDescriptor.h"
#include "util/Logger.h"
#include "LinuxUsbTransport.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static const char* TAG = "main";

static void printUsage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS] [<job_file>|-]\n"
        "\n"
        "Send a TSPL print job to a USB label printer.\n"
        "\n"
        "Options:\n"
        "  --vid VID       Printer vendor ID (hex, e.g. 0A5F)\n"
        "  --pid PID       Printer product ID (hex, e.g. 00D3)\n"
        "  --list          List detected USB printers and exit\n"
        "  --verbose       Enable debug logging\n"
        "  --help          Show this help\n"
        "\n"
        "If <job_file> is '-' or omitted, data is read from stdin.\n",
        prog);
}

static std::vector<uint8_t> readJobData(const std::string& path) {
    std::vector<uint8_t> data;

    if (path == "-" || path.empty()) {
        // Read from stdin
        char buf[4096];
        while (std::cin.read(buf, sizeof(buf)) || std::cin.gcount() > 0) {
            auto n = std::cin.gcount();
            data.insert(data.end(), buf, buf + n);
        }
    } else {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            return data; // empty = error
        }
        f.seekg(0, std::ios::end);
        auto sz = f.tellg();
        f.seekg(0, std::ios::beg);
        if (sz > 0) {
            data.resize(static_cast<size_t>(sz));
            f.read(reinterpret_cast<char*>(data.data()), sz);
            if (!f) {
                data.clear(); // read error
            }
        }
    }
    return data;
}

int main(int argc, char* argv[]) {
    uint16_t    vid      = 0;
    uint16_t    pid      = 0;
    bool        listMode = false;
    bool        verbose  = false;
    std::string jobPath;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--list") == 0) {
            listMode = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--vid") == 0 && i + 1 < argc) {
            vid = static_cast<uint16_t>(strtol(argv[++i], nullptr, 16));
        } else if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            pid = static_cast<uint16_t>(strtol(argv[++i], nullptr, 16));
        } else if (argv[i][0] != '-' || strcmp(argv[i], "-") == 0) {
            jobPath = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (verbose) {
        Logger::setLevel(LogLevel::DEBUG);
    }

    // --list mode: enumerate and exit
    if (listMode) {
        UsbDeviceDescriptor devs[16];
        int n = LinuxUsbTransport::enumeratePrinters(devs, 16);
        if (n < 0) {
            fprintf(stderr, "Error: failed to enumerate USB devices\n");
            return 1;
        }
        if (n == 0) {
            printf("No USB printers found.\n");
            return 0;
        }
        printf("Found %d USB printer(s):\n", n);
        for (int i = 0; i < n; ++i) {
            printf("  [%d] %s\n", i, devs[i].description().c_str());
        }
        return 0;
    }

    // Read job data
    if (jobPath.empty()) jobPath = "-";

    auto jobData = readJobData(jobPath);
    if (jobData.empty()) {
        if (jobPath == "-") {
            fprintf(stderr, "Error: no data received from stdin\n");
        } else {
            fprintf(stderr, "Error: cannot read file '%s'\n", jobPath.c_str());
        }
        return 1;
    }

    // Auto-detect or use supplied VID/PID
    UsbDeviceDescriptor target;
    if (vid != 0 && pid != 0) {
        target.vendorId  = vid;
        target.productId = pid;
    } else {
        UsbDeviceDescriptor devs[16];
        int n = LinuxUsbTransport::enumeratePrinters(devs, 16);
        if (n <= 0) {
            fprintf(stderr, "Error: no USB printer found. "
                    "Connect a printer or specify --vid/--pid.\n");
            return 1;
        }
        target = devs[0];
        LOG_INFO(TAG, "Auto-selected printer: %s", target.description().c_str());
    }

    // Build manager and send
    auto transport = std::make_unique<LinuxUsbTransport>();
    PrinterManager manager(std::move(transport));
    manager.registerHandler(std::make_unique<TsplHandler>());
    manager.registerHandler(std::make_unique<EscpHandler>());
    manager.registerHandler(std::make_unique<PclHandler>());

    if (!manager.openDevice(target)) {
        fprintf(stderr, "Error: failed to open printer %s\n",
                target.description().c_str());
        return 1;
    }

    PrintJob job;
    job.rawData   = std::move(jobData);
    job.jobId     = jobPath;
    job.timeoutMs = 5000;

    if (!manager.submitJob(std::move(job))) {
        fprintf(stderr, "Error: print job failed\n");
        return 1;
    }

    LOG_INFO(TAG, "Job completed successfully");
    return 0;
}