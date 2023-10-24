#include <gst/gst.h>
#include <iostream>

#include "SourceSwitcher.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cout << "Args: switch_interval filepath" << std::endl;
        return -1;
    }

    try {
        SourceSwitcherConfig testConfig;
        testConfig.switchInterval = std::chrono::seconds(std::stoi(argv[1]));
        testConfig.filepath = argv[2];

        gst_init(NULL, NULL);
        SourceSwitcher switcher(testConfig);
        switcher.LoopRun();
        switcher.Stop();
    } catch (const std::exception &e) {
        std::cout << e.what() << std::endl;
        return -1;
    }

    return 0;
}