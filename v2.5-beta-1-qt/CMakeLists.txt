cmake_minimum_required(VERSION 3.21)

project(comic-chat-qt VERSION 2.5 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network PrintSupport)
find_package(ZLIB REQUIRED)

set(COMIC_CHAT_ORIGINAL_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../v2.5-beta-1-modern"
    CACHE PATH
    "Path to the authoritative v2.5-beta-1-modern source and asset tree"
)
get_filename_component(COMIC_CHAT_ORIGINAL_ROOT
    "${COMIC_CHAT_ORIGINAL_ROOT}"
    REALPATH
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(NOT EXISTS "${COMIC_CHAT_ORIGINAL_ROOT}/chat.mak"
   OR NOT EXISTS "${COMIC_CHAT_ORIGINAL_ROOT}/chat.rc"
   OR NOT EXISTS "${COMIC_CHAT_ORIGINAL_ROOT}/comicart")
    message(FATAL_ERROR
        "COMIC_CHAT_ORIGINAL_ROOT must point to v2.5-beta-1-modern: "
        "${COMIC_CHAT_ORIGINAL_ROOT}"
    )
endif()

# chat.mak builds textcore.cpp by including the shared artifacts tree. Keep
# that exact build-selected source/resource dependency explicit.
set(COMIC_CHAT_ARTIFACTS_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../artifacts"
    CACHE PATH
    "Path to the shared artifacts tree selected by v2.5-beta-1-modern/chat.mak"
)
get_filename_component(COMIC_CHAT_ARTIFACTS_ROOT
    "${COMIC_CHAT_ARTIFACTS_ROOT}"
    REALPATH
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(NOT EXISTS "${COMIC_CHAT_ARTIFACTS_ROOT}/inc/textview.h"
   OR NOT EXISTS "${COMIC_CHAT_ARTIFACTS_ROOT}/inc/textview.rc"
   OR NOT EXISTS "${COMIC_CHAT_ARTIFACTS_ROOT}/inc/msgtype.h")
    message(FATAL_ERROR
        "COMIC_CHAT_ARTIFACTS_ROOT does not contain the TextView dependency: "
        "${COMIC_CHAT_ARTIFACTS_ROOT}"
    )
endif()

set(COMIC_CHAT_V1_SHARED_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../v1.0/shared"
    CACHE PATH
    "Path to the source-backed v1.0 shared files"
)
get_filename_component(COMIC_CHAT_V1_SHARED_ROOT
    "${COMIC_CHAT_V1_SHARED_ROOT}"
    REALPATH
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(NOT EXISTS "${COMIC_CHAT_V1_SHARED_ROOT}/comic.ttf")
    message(FATAL_ERROR
        "COMIC_CHAT_V1_SHARED_ROOT must contain the tracked comic.ttf: "
        "${COMIC_CHAT_V1_SHARED_ROOT}"
    )
endif()

set(COMIC_CHAT_PORT_SOURCES
    src/original/chat.cpp
    src/original/mainfrm.cpp
    src/original/childfrm.cpp
    src/original/chatdoc.cpp
    src/original/chatview.cpp
    src/original/spltchat.cpp
    src/original/tabbar.cpp
    src/original/chatbars.cpp
    src/original/coolbar.cpp
    src/original/pageview.cpp
    src/original/panel.cpp
    src/original/fonts.cpp
    src/original/balloon.cpp
    src/original/histent.cpp
    src/original/saywnd.cpp
    src/original/rtfctrl.cpp
    src/original/doskey.cpp
    src/original/colordlg.cpp
    src/original/format.cpp
    src/original/txtfntdg.cpp
    src/original/urlutil.cpp
    src/original/textcore.cpp
    src/original/textview.cpp
    src/original/status.cpp
    src/original/memblst.cpp
    src/original/userinfo.cpp
    src/original/bodycam.cpp
    src/original/avatar.cpp
    src/original/textpose.cpp
    src/original/avatario.cpp
    src/original/avbfile.cpp
    src/original/dib.cpp
    src/original/vector2d.cpp
    src/original/bbox.cpp
    src/original/traj.cpp
    src/original/spline.cpp
    src/original/splinutl.cpp
    src/original/arc.cpp
    src/original/backdrop.cpp
    src/original/chatsrv.cpp
    src/original/ircsock.cpp
    src/original/ircproto.cpp
    src/original/filesend.cpp
    src/original/ccommon.cpp
    src/original/intl.c
    src/original/jis2sjis.cpp
    src/original/sjis2jis.cpp
    src/original/ccomp.cpp
    src/original/rules.cpp
    src/original/actions.cpp
    src/original/notif.cpp
    src/original/notipage.cpp
    src/original/autopage.cpp
    src/original/query.cpp
    src/original/utils.cpp
    src/original/protsupp.cpp
    src/original/setupdlg.cpp
    src/original/proppage.cpp
    src/original/admindlg.cpp
    src/original/chanprop.cpp
    src/original/motd.cpp
    src/original/roomlist.cpp
    src/original/userlist.cpp
    src/original/whisprbx.cpp
    src/qt/win98palette.cpp
    src/qt/wincompat.cpp
    src/qt/originalassets.cpp
    src/qt/originalsettings.cpp
)

# intl.c is the original filename.  Its active Qt adapter boundary uses C++
# types, just as the original Chat target compiled the source in the C++ build.
set_source_files_properties(src/original/intl.c PROPERTIES LANGUAGE CXX)

add_library(comic-chat-core STATIC
    ${COMIC_CHAT_PORT_SOURCES}
)

target_include_directories(comic-chat-core PUBLIC
    src/original
    src/qt
)

target_include_directories(comic-chat-core PRIVATE
    src/qt/legacy
)

target_compile_definitions(comic-chat-core PRIVATE
    COMIC_CHAT_NO_MAIN
    COMIC_CHAT_ORIGINAL_ROOT="${COMIC_CHAT_ORIGINAL_ROOT}"
    COMIC_CHAT_ARTIFACTS_ROOT="${COMIC_CHAT_ARTIFACTS_ROOT}"
    COMIC_CHAT_V1_SHARED_ROOT="${COMIC_CHAT_V1_SHARED_ROOT}"
)

target_link_libraries(comic-chat-core PUBLIC
    Qt6::Widgets
    Qt6::Network
    Qt6::PrintSupport
    ZLIB::ZLIB
)

add_executable(comic-chat-qt
    src/original/chat.cpp
)

target_compile_definitions(comic-chat-qt PRIVATE
    COMIC_CHAT_ENTRY_ONLY
)

target_link_libraries(comic-chat-qt PRIVATE
    comic-chat-core
)

set_target_properties(comic-chat-qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(BUILD_TESTING)
    set(original_irc_suite_sources
        tests/original_ccommon_test.cpp
        tests/original_jis_conversion_test.cpp
        tests/original_intl_test.cpp
        tests/original_protocol_encoding_test.cpp
        tests/original_irc_login_test.cpp
        tests/original_join_starring_test.cpp
        tests/original_comic_send_test.cpp
        tests/original_irc_state_test.cpp
        tests/original_presence_protocol_test.cpp
        tests/original_irc_parser_test.cpp
        tests/original_irc_dispatch_test.cpp
        tests/original_slash_command_test.cpp
        tests/original_multiroom_routing_test.cpp
        tests/original_mdi_join_test.cpp
        tests/original_member_commands_test.cpp
        tests/original_channel_properties_test.cpp
        tests/original_room_switch_test.cpp
        tests/original_motd_away_test.cpp
        tests/original_room_user_list_test.cpp
        tests/original_admin_dialog_test.cpp
    )
    set_source_files_properties(tests/original_ccommon_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_ccommon_test")
    set_source_files_properties(tests/original_jis_conversion_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_jis_conversion_test")
    set_source_files_properties(tests/original_intl_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_intl_test")
    set_source_files_properties(tests/original_protocol_encoding_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_protocol_encoding_test")
    set_source_files_properties(tests/original_irc_login_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_irc_login_test")
    set_source_files_properties(tests/original_join_starring_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_join_starring_test")
    set_source_files_properties(tests/original_comic_send_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_comic_send_test")
    set_source_files_properties(tests/original_irc_state_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_irc_state_test")
    set_source_files_properties(tests/original_presence_protocol_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_presence_protocol_test")
    set_source_files_properties(tests/original_irc_parser_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_irc_parser_test")
    set_source_files_properties(tests/original_irc_dispatch_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_irc_dispatch_test")
    set_source_files_properties(tests/original_slash_command_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_slash_command_test")
    set_source_files_properties(tests/original_multiroom_routing_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_multiroom_routing_test")
    set_source_files_properties(tests/original_mdi_join_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_mdi_join_test")
    set_source_files_properties(tests/original_member_commands_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_member_commands_test")
    set_source_files_properties(tests/original_channel_properties_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_channel_properties_test")
    set_source_files_properties(tests/original_room_switch_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_room_switch_test")
    set_source_files_properties(tests/original_motd_away_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_motd_away_test")
    set_source_files_properties(tests/original_room_user_list_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_room_user_list_test")
    set_source_files_properties(tests/original_admin_dialog_test.cpp PROPERTIES
        COMPILE_DEFINITIONS "main=run_original_admin_dialog_test")

    add_executable(original-irc-suite-test
        tests/original_irc_suite.cpp
        ${original_irc_suite_sources}
    )
    target_link_libraries(original-irc-suite-test PRIVATE
        comic-chat-core
    )

    foreach(original_irc_case IN ITEMS
        original-ccommon
        original-jis-conversion
        original-intl
        original-protocol-encoding
        original-irc-login
        original-join-starring
        original-comic-send
        original-irc-state
        original-presence-protocol
        original-irc-parser
        original-irc-dispatch
        original-slash-command
        original-multiroom-routing
        original-mdi-join
        original-member-commands
        original-channel-properties
        original-room-switch
        original-motd-away
        original-room-user-list
        original-admin-dialog
    )
        add_test(NAME ${original_irc_case}
            COMMAND original-irc-suite-test ${original_irc_case})
    endforeach()
    set_tests_properties(
        original-intl
        original-presence-protocol
        original-irc-dispatch
        original-multiroom-routing
        original-mdi-join
        original-member-commands
        original-channel-properties
        original-room-switch
        original-motd-away
        original-room-user-list
        original-admin-dialog
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    set(original_core_suite_sources
        tests/original_rules_test.cpp
        tests/original_ccomp_test.cpp
        tests/original_constants_test.cpp
        tests/original_assets_test.cpp
        tests/original_avb_test.cpp
        tests/original_format_geometry_test.cpp
        tests/original_url_test.cpp
        tests/original_comic_core_test.cpp
        tests/original_rule_daemon_test.cpp
        tests/original_notification_test.cpp
        tests/original_textpose_test.cpp
    )
    foreach(original_core_source IN LISTS original_core_suite_sources)
        get_filename_component(original_core_stem
            "${original_core_source}" NAME_WE)
        set_source_files_properties("${original_core_source}" PROPERTIES
            COMPILE_DEFINITIONS "main=run_${original_core_stem}")
    endforeach()

    add_executable(original-core-suite-test
        tests/original_core_suite.cpp
        ${original_core_suite_sources}
    )
    target_link_libraries(original-core-suite-test PRIVATE
        comic-chat-core
    )

    foreach(original_core_case IN ITEMS
        original-rules
        original-ccomp
        original-constants
        original-assets
        original-avb
        original-format-geometry
        original-url
        original-comic-core
        original-rule-daemon
        original-notification
        original-textpose
    )
        add_test(NAME ${original_core_case}
            COMMAND original-core-suite-test ${original_core_case})
    endforeach()
    set_tests_properties(original-textpose PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    set(original_ui_suite_sources
        tests/original_pageview_interaction_test.cpp
        tests/original_page_iteration_test.cpp
        tests/original_notipage_test.cpp
        tests/original_autopage_test.cpp
        tests/original_chatsrv_test.cpp
        tests/original_ui_structure_test.cpp
        tests/original_coolbar_test.cpp
        tests/original_persistence_test.cpp
        tests/original_registry_persistence_test.cpp
        tests/original_text_view_test.cpp
        tests/original_text_font_dialog_test.cpp
        tests/original_history_test.cpp
        tests/original_whisper_box_test.cpp
        tests/original_printing_test.cpp
    )
    foreach(original_ui_source IN LISTS original_ui_suite_sources)
        get_filename_component(original_ui_stem
            "${original_ui_source}" NAME_WE)
        set_source_files_properties("${original_ui_source}" PROPERTIES
            COMPILE_DEFINITIONS "main=run_${original_ui_stem}")
    endforeach()

    add_executable(original-ui-suite-test
        tests/original_ui_suite.cpp
        ${original_ui_suite_sources}
    )
    target_link_libraries(original-ui-suite-test PRIVATE
        comic-chat-core
    )

    foreach(original_ui_case IN ITEMS
        original-pageview-interaction
        original-page-iteration
        original-notipage
        original-autopage
        original-chatsrv
        original-ui-structure
        original-coolbar
        original-persistence
        original-registry-persistence
        original-text-view
        original-text-font-dialog
        original-history
        original-whisper-box
        original-printing
    )
        add_test(NAME ${original_ui_case}
            COMMAND original-ui-suite-test ${original_ui_case})
    endforeach()
    set_tests_properties(
        original-page-iteration
        original-notipage
        original-autopage
        original-chatsrv
        original-ui-structure
        original-coolbar
        original-persistence
        original-text-view
        original-text-font-dialog
        original-history
        original-whisper-box
        original-printing
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
