// Shared runner for UI regression sources. CMake renames each source file's
// existing main function to the matching entry below; every CTest invocation
// still starts a separate process and selects one case.

#include <cstring>
#include <iostream>

int run_original_pageview_interaction_test(int argc, char** argv);
int run_original_page_iteration_test(int argc, char** argv);
int run_original_notipage_test(int argc, char** argv);
int run_original_autopage_test(int argc, char** argv);
int run_original_chatsrv_test(int argc, char** argv);
int run_original_ui_structure_test(int argc, char** argv);
int run_original_coolbar_test(int argc, char** argv);
int run_original_persistence_test(int argc, char** argv);
int run_original_registry_persistence_test(int argc, char** argv);
int run_original_text_view_test(int argc, char** argv);
int run_original_text_font_dialog_test(int argc, char** argv);
int run_original_history_test(int argc, char** argv);
int run_original_whisper_box_test(int argc, char** argv);
int run_original_printing_test(int argc, char** argv);

namespace {
using Entry = int (*)(int, char**);

struct SuiteCase {
    const char* name;
    Entry entry;
};

const SuiteCase cases[] = {
    {"original-pageview-interaction",
     run_original_pageview_interaction_test},
    {"original-page-iteration", run_original_page_iteration_test},
    {"original-notipage", run_original_notipage_test},
    {"original-autopage", run_original_autopage_test},
    {"original-chatsrv", run_original_chatsrv_test},
    {"original-ui-structure", run_original_ui_structure_test},
    {"original-coolbar", run_original_coolbar_test},
    {"original-persistence", run_original_persistence_test},
    {"original-registry-persistence",
     run_original_registry_persistence_test},
    {"original-text-view", run_original_text_view_test},
    {"original-text-font-dialog", run_original_text_font_dialog_test},
    {"original-history", run_original_history_test},
    {"original-whisper-box", run_original_whisper_box_test},
    {"original-printing", run_original_printing_test},
};
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: original-ui-suite-test <case>\n";
        return 2;
    }

    for (const SuiteCase& suiteCase : cases) {
        if (std::strcmp(argv[1], suiteCase.name) != 0) continue;

        int testArgc = 1;
        char* testArgv[] = {argv[0], nullptr};
        return suiteCase.entry(testArgc, testArgv);
    }

    std::cerr << "unknown UI suite case: " << argv[1] << '\n';
    return 2;
}
