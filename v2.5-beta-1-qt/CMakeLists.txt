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
    add_executable(original-rules-test
        tests/original_rules_test.cpp
    )
    target_link_libraries(original-rules-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-rules COMMAND original-rules-test)

    add_executable(original-ccomp-test
        tests/original_ccomp_test.cpp
    )
    target_link_libraries(original-ccomp-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-ccomp COMMAND original-ccomp-test)

    add_executable(original-ccommon-test
        tests/original_ccommon_test.cpp
    )
    target_link_libraries(original-ccommon-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-ccommon COMMAND original-ccommon-test)

    add_executable(original-jis-conversion-test
        tests/original_jis_conversion_test.cpp
    )
    target_link_libraries(original-jis-conversion-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-jis-conversion COMMAND original-jis-conversion-test)

    add_executable(original-intl-test
        tests/original_intl_test.cpp
    )
    target_link_libraries(original-intl-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-intl COMMAND original-intl-test)
    set_tests_properties(original-intl PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-constants-test
        tests/original_constants_test.cpp
    )
    target_include_directories(original-constants-test PRIVATE
        src/original
    )
    add_test(NAME original-constants COMMAND original-constants-test)

    add_executable(original-assets-test
        tests/original_assets_test.cpp
        src/qt/originalassets.cpp
    )
    target_include_directories(original-assets-test PRIVATE
        src/qt
    )
    target_compile_definitions(original-assets-test PRIVATE
        COMIC_CHAT_ORIGINAL_ROOT="${COMIC_CHAT_ORIGINAL_ROOT}"
        COMIC_CHAT_ARTIFACTS_ROOT="${COMIC_CHAT_ARTIFACTS_ROOT}"
    )
    target_link_libraries(original-assets-test PRIVATE
        Qt6::Core
    )
    add_test(NAME original-assets COMMAND original-assets-test)

    add_executable(original-protocol-encoding-test
        tests/original_protocol_encoding_test.cpp
    )
    target_link_libraries(original-protocol-encoding-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-protocol-encoding COMMAND original-protocol-encoding-test)

    add_executable(original-avb-test
        tests/original_avb_test.cpp
    )
    target_link_libraries(original-avb-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-avb COMMAND original-avb-test)

    add_executable(original-format-geometry-test
        tests/original_format_geometry_test.cpp
    )
    target_link_libraries(original-format-geometry-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-format-geometry COMMAND original-format-geometry-test)

    add_executable(original-url-test
        tests/original_url_test.cpp
    )
    target_link_libraries(original-url-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-url COMMAND original-url-test)

    add_executable(original-comic-core-test
        tests/original_comic_core_test.cpp
    )
    target_link_libraries(original-comic-core-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-comic-core COMMAND original-comic-core-test)

    add_executable(original-pageview-interaction-test
        tests/original_pageview_interaction_test.cpp
    )
    target_link_libraries(original-pageview-interaction-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-pageview-interaction
        COMMAND original-pageview-interaction-test)

    add_executable(original-page-iteration-test
        tests/original_page_iteration_test.cpp
    )
    target_link_libraries(original-page-iteration-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-page-iteration
        COMMAND original-page-iteration-test)
    set_tests_properties(original-page-iteration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-irc-login-test
        tests/original_irc_login_test.cpp
    )
    target_link_libraries(original-irc-login-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-irc-login COMMAND original-irc-login-test)

    add_executable(original-rule-daemon-test
        tests/original_rule_daemon_test.cpp
    )
    target_link_libraries(original-rule-daemon-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-rule-daemon COMMAND original-rule-daemon-test)

    add_executable(original-notification-test
        tests/original_notification_test.cpp
    )
    target_link_libraries(original-notification-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-notification COMMAND original-notification-test)

    add_executable(original-notipage-test
        tests/original_notipage_test.cpp
    )
    target_link_libraries(original-notipage-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-notipage COMMAND original-notipage-test)
    set_tests_properties(original-notipage PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-autopage-test
        tests/original_autopage_test.cpp
    )
    target_link_libraries(original-autopage-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-autopage COMMAND original-autopage-test)
    set_tests_properties(original-autopage PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-chatsrv-test
        tests/original_chatsrv_test.cpp
    )
    target_link_libraries(original-chatsrv-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-chatsrv COMMAND original-chatsrv-test)
    set_tests_properties(original-chatsrv PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-join-starring-test
        tests/original_join_starring_test.cpp
    )
    target_link_libraries(original-join-starring-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-join-starring COMMAND original-join-starring-test)

    add_executable(original-comic-send-test
        tests/original_comic_send_test.cpp
    )
    target_link_libraries(original-comic-send-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-comic-send COMMAND original-comic-send-test)

    add_executable(original-irc-state-test
        tests/original_irc_state_test.cpp
    )
    target_link_libraries(original-irc-state-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-irc-state COMMAND original-irc-state-test)

    add_executable(original-ui-structure-test
        tests/original_ui_structure_test.cpp
    )
    target_link_libraries(original-ui-structure-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-ui-structure COMMAND original-ui-structure-test)
    set_tests_properties(original-ui-structure PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-coolbar-test
        tests/original_coolbar_test.cpp
    )
    target_link_libraries(original-coolbar-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-coolbar COMMAND original-coolbar-test)
    set_tests_properties(original-coolbar PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-persistence-test
        tests/original_persistence_test.cpp
    )
    target_link_libraries(original-persistence-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-persistence COMMAND original-persistence-test)
    set_tests_properties(original-persistence PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-registry-persistence-test
        tests/original_registry_persistence_test.cpp
    )
    target_link_libraries(original-registry-persistence-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-registry-persistence
        COMMAND original-registry-persistence-test)

    add_executable(original-text-view-test
        tests/original_text_view_test.cpp
    )
    target_link_libraries(original-text-view-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-text-view COMMAND original-text-view-test)
    set_tests_properties(original-text-view PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-text-font-dialog-test
        tests/original_text_font_dialog_test.cpp
    )
    target_link_libraries(original-text-font-dialog-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-text-font-dialog
        COMMAND original-text-font-dialog-test)
    set_tests_properties(original-text-font-dialog PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-textpose-test
        tests/original_textpose_test.cpp
    )
    target_link_libraries(original-textpose-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-textpose COMMAND original-textpose-test)
    set_tests_properties(original-textpose PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-history-test
        tests/original_history_test.cpp
    )
    target_link_libraries(original-history-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-history COMMAND original-history-test)
    set_tests_properties(original-history PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-presence-protocol-test
        tests/original_presence_protocol_test.cpp
    )
    target_link_libraries(original-presence-protocol-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-presence-protocol COMMAND original-presence-protocol-test)
    set_tests_properties(original-presence-protocol PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-irc-parser-test
        tests/original_irc_parser_test.cpp
    )
    target_link_libraries(original-irc-parser-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-irc-parser COMMAND original-irc-parser-test)

    add_executable(original-slash-command-test
        tests/original_slash_command_test.cpp
    )
    target_link_libraries(original-slash-command-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-slash-command COMMAND original-slash-command-test)

    add_executable(original-multiroom-routing-test
        tests/original_multiroom_routing_test.cpp
    )
    target_link_libraries(original-multiroom-routing-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-multiroom-routing COMMAND original-multiroom-routing-test)
    set_tests_properties(original-multiroom-routing PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-mdi-join-test
        tests/original_mdi_join_test.cpp
    )
    target_link_libraries(original-mdi-join-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-mdi-join COMMAND original-mdi-join-test)
    set_tests_properties(original-mdi-join PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-member-commands-test
        tests/original_member_commands_test.cpp
    )
    target_link_libraries(original-member-commands-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-member-commands COMMAND original-member-commands-test)
    set_tests_properties(original-member-commands PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-channel-properties-test
        tests/original_channel_properties_test.cpp
    )
    target_link_libraries(original-channel-properties-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-channel-properties
        COMMAND original-channel-properties-test)
    set_tests_properties(original-channel-properties PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-room-switch-test
        tests/original_room_switch_test.cpp
    )
    target_link_libraries(original-room-switch-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-room-switch COMMAND original-room-switch-test)
    set_tests_properties(original-room-switch PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-motd-away-test
        tests/original_motd_away_test.cpp
    )
    target_link_libraries(original-motd-away-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-motd-away COMMAND original-motd-away-test)
    set_tests_properties(original-motd-away PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-room-user-list-test
        tests/original_room_user_list_test.cpp
    )
    target_link_libraries(original-room-user-list-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-room-user-list COMMAND original-room-user-list-test)
    set_tests_properties(original-room-user-list PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-admin-dialog-test
        tests/original_admin_dialog_test.cpp
    )
    target_link_libraries(original-admin-dialog-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-admin-dialog COMMAND original-admin-dialog-test)
    set_tests_properties(original-admin-dialog PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-whisper-box-test
        tests/original_whisper_box_test.cpp
    )
    target_link_libraries(original-whisper-box-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-whisper-box COMMAND original-whisper-box-test)
    set_tests_properties(original-whisper-box PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(original-printing-test
        tests/original_printing_test.cpp
    )
    target_link_libraries(original-printing-test PRIVATE
        comic-chat-core
    )
    add_test(NAME original-printing COMMAND original-printing-test)
    set_tests_properties(original-printing PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
