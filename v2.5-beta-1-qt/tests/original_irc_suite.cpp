// Shared runner for the IRC-domain regression sources.  CMake renames each
// source file's existing main function to the matching entry below; this file
// changes only test-process dispatch and does not alter a test fixture.

#include <cstring>
#include <iostream>

int run_original_ccommon_test();
int run_original_jis_conversion_test();
int run_original_intl_test(int argc, char** argv);
int run_original_protocol_encoding_test();
int run_original_irc_login_test(int argc, char** argv);
int run_original_join_starring_test(int argc, char** argv);
int run_original_comic_send_test(int argc, char** argv);
int run_original_irc_state_test(int argc, char** argv);
int run_original_presence_protocol_test(int argc, char** argv);
int run_original_irc_parser_test(int argc, char** argv);
int run_original_irc_dispatch_test(int argc, char** argv);
int run_original_slash_command_test(int argc, char** argv);
int run_original_multiroom_routing_test(int argc, char** argv);
int run_original_mdi_join_test(int argc, char** argv);
int run_original_member_commands_test(int argc, char** argv);
int run_original_channel_properties_test(int argc, char** argv);
int run_original_room_switch_test(int argc, char** argv);
int run_original_motd_away_test(int argc, char** argv);
int run_original_room_user_list_test(int argc, char** argv);
int run_original_admin_dialog_test(int argc, char** argv);

namespace {
using ArgumentEntry = int (*)(int, char**);
using PlainEntry = int (*)();

struct SuiteCase {
    const char* name;
    ArgumentEntry argumentEntry;
    PlainEntry plainEntry;
};

const SuiteCase cases[] = {
    {"original-ccommon", nullptr, run_original_ccommon_test},
    {"original-jis-conversion", nullptr,
     run_original_jis_conversion_test},
    {"original-intl", run_original_intl_test, nullptr},
    {"original-protocol-encoding", nullptr,
     run_original_protocol_encoding_test},
    {"original-irc-login", run_original_irc_login_test, nullptr},
    {"original-join-starring", run_original_join_starring_test, nullptr},
    {"original-comic-send", run_original_comic_send_test, nullptr},
    {"original-irc-state", run_original_irc_state_test, nullptr},
    {"original-presence-protocol", run_original_presence_protocol_test,
     nullptr},
    {"original-irc-parser", run_original_irc_parser_test, nullptr},
    {"original-irc-dispatch", run_original_irc_dispatch_test, nullptr},
    {"original-slash-command", run_original_slash_command_test, nullptr},
    {"original-multiroom-routing", run_original_multiroom_routing_test,
     nullptr},
    {"original-mdi-join", run_original_mdi_join_test, nullptr},
    {"original-member-commands", run_original_member_commands_test,
     nullptr},
    {"original-channel-properties", run_original_channel_properties_test,
     nullptr},
    {"original-room-switch", run_original_room_switch_test, nullptr},
    {"original-motd-away", run_original_motd_away_test, nullptr},
    {"original-room-user-list", run_original_room_user_list_test, nullptr},
    {"original-admin-dialog", run_original_admin_dialog_test, nullptr},
};
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: original-irc-suite-test <case>\n";
        return 2;
    }

    for (const SuiteCase& suiteCase : cases) {
        if (std::strcmp(argv[1], suiteCase.name) != 0) continue;
        if (suiteCase.plainEntry) return suiteCase.plainEntry();

        int testArgc = 1;
        char* testArgv[] = {argv[0], nullptr};
        return suiteCase.argumentEntry(testArgc, testArgv);
    }

    std::cerr << "unknown IRC suite case: " << argv[1] << '\n';
    return 2;
}
