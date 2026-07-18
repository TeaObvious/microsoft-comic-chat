// Shared runner for non-UI core regression sources. CMake renames each
// source file's existing main function to the matching entry below; every
// CTest invocation still starts a separate process and selects one case.

#include <cstring>
#include <iostream>

int run_original_rules_test(int argc, char** argv);
int run_original_ccomp_test();
int run_original_constants_test();
int run_original_assets_test();
int run_original_avb_test(int argc, char** argv);
int run_original_format_geometry_test();
int run_original_url_test(int argc, char** argv);
int run_original_comic_core_test(int argc, char** argv);
int run_original_rule_daemon_test(int argc, char** argv);
int run_original_notification_test(int argc, char** argv);
int run_original_textpose_test(int argc, char** argv);

namespace {
using ArgumentEntry = int (*)(int, char**);
using PlainEntry = int (*)();

struct SuiteCase {
    const char* name;
    ArgumentEntry argumentEntry;
    PlainEntry plainEntry;
};

const SuiteCase cases[] = {
    {"original-rules", run_original_rules_test, nullptr},
    {"original-ccomp", nullptr, run_original_ccomp_test},
    {"original-constants", nullptr, run_original_constants_test},
    {"original-assets", nullptr, run_original_assets_test},
    {"original-avb", run_original_avb_test, nullptr},
    {"original-format-geometry", nullptr,
     run_original_format_geometry_test},
    {"original-url", run_original_url_test, nullptr},
    {"original-comic-core", run_original_comic_core_test, nullptr},
    {"original-rule-daemon", run_original_rule_daemon_test, nullptr},
    {"original-notification", run_original_notification_test, nullptr},
    {"original-textpose", run_original_textpose_test, nullptr},
};
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: original-core-suite-test <case>\n";
        return 2;
    }

    for (const SuiteCase& suiteCase : cases) {
        if (std::strcmp(argv[1], suiteCase.name) != 0) continue;
        if (suiteCase.plainEntry) return suiteCase.plainEntry();

        int testArgc = 1;
        char* testArgv[] = {argv[0], nullptr};
        return suiteCase.argumentEntry(testArgc, testArgv);
    }

    std::cerr << "unknown core suite case: " << argv[1] << '\n';
    return 2;
}
