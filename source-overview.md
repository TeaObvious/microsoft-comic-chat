# Source Overview: `v2.5-beta-1-modern`

## TODO

- [x] Restore the source-defined character icons in the member pane. This is
  implemented only when comic icon mode obtains each real member's image from
  the original avatar/member-list path, preserves the original role/status
  image rules, and never substitutes generated or converted icons.
- [x] Restore every source-defined icon beside the Say input. This is
  implemented only when the controls, bitmap strips, command IDs, order,
  enabled/check state, and transparency come directly from `chat.rc`,
  `saywnd.*`, `spltchat.*`, and the original BMP resources.
- [x] Remove the delayed initial comic/starring paint after NAMES enumeration.
  This is implemented only when `366` -> `ProcessEndEnumeration` ->
  `CPage::AddTitle`/`UpdateTitle` -> `AddStars`/`AddStarsAux` makes the complete
  source-built title and any already-created panels visible in the next Qt
  event cycle without a click, later message, timer accident, or fabricated
  participant; an offscreen regression must reproduce the former stale-frame
  boundary.
- [x] Restore the source-defined one-shot character forwarding in
  `spltchat.cpp` and `tabbar.cpp`. This is implemented only when a character
  delivered outside the Say control reaches `CSayCtrl` exactly once, focus
  moves to the Say control, and an offscreen regression proves that no parent
  event recursion occurs.
- [x] Restore the unformatted branches of `CLabel::Draw` and
  `CBalloon::DrawText` from `balloon.*`. This is implemented only when a null
  or empty formatting array draws every wrapped line with the original left
  offsets and line-height decrement, while the formatted branches still use
  `DrawFormattedText`; text-only render regressions must distinguish glyph ink
  from contours and avatars.
- [x] Remove the non-source blue selected-tab styling and validate the
  `IDB_TABS` transparency key directly against the original BMP. This is
  implemented only when the fixed Windows 98 palette leaves the tab control to
  its source-equivalent Windows style and every source-green mask pixel is
  transparent without copying or converting the asset.
- [x] Preserve the original send gate around self `JOIN`: `CX_NOCHANNEL` must
  reject a room send, and only the server-confirmed self `JOIN` transition to
  `CX_INCHANNEL` may permit it. Do not add an optimistic local join state to
  suppress the source-defined error.
- [x] Trace and correct delayed inbound self `JOIN` delivery across
  `CIrcProto::ChatJoinAux`, `QTcpSocket::readyRead`,
  `CIrcSocket::handleReadyRead`, and `ProcessMessageBytes`. This is
  implemented only when the outbound `JOIN` is proven to be sent, the first
  complete server response is consumed without unrelated later traffic, the
  source-defined self-`JOIN` handler changes the room title and status, and a
  regression covers the identified boundary without an optimistic local join.
- [ ] Complete the source-defined `TextFonts` and `HighlightedTextFonts`
  role application, persistence validation, and the full
  `CTextFontPage`/`CMyFontDialog` resource UI.
- [ ] Complete every command, numeric, result, error, query-lifetime, and
  non-Far-East ACP branch in `ircsock.*`, `ircproto.*`, `query.*`, and the
  associated glue; keep unsupported SSPI authentication modes disabled with
  an explicit platform blocker.
- [ ] Complete member-list icon/list modes, original status images,
  context-command enablement, selection behavior, and all source-backed
  member actions.
- [ ] Complete all menu, submenu, accelerator, toolbar, status-bar,
  child-frame, focus, and command-UI behavior defined by `chat.rc` and the
  original message maps.
- [ ] Complete the canonical dialog modules, including Settings, Profile,
  Comic Fonts, Text Fonts, file/sound selection, room and user lists, and
  their exact validation and apply/cancel effects.
- [ ] Complete conversation-file integration, child-frame persistence,
  file transfer, sound/MCI behavior, URL edge cases, printer-specific paths,
  art-pack switching, and source-enabled download/notification paths.
- [ ] Document every non-portable OLE, DocObject, Automation, SSPI, Help, and
  Windows integration point by original file and symbol; do not simulate
  behavior without a source-backed equivalent.
- [ ] Require Debug and Release builds, the full CTest suite, an isolated
  offscreen startup check, `git diff --check`, and a scan for dummy content
  before declaring parity.

This index covers the source and build inputs in `v2.5-beta-1-modern`:
`.cpp`, `.c`, `.h`, `.idl`, `.rc`, `.rcv`, and `.mak`. Assets such as `.avb`,
`.bgb`, `.bmp`, `.ico`, `.txt`, `.hlp`, `.gif`, and `.htm` are intentionally
not described file by file.

## Architecture in one sentence

Microsoft Comic Chat is an MFC/Win32 MDI application: `CChatApp` and
`CChatDoc` manage sessions, documents, and menus; `CIrcSocket`/`CIrcProto`
process IRC/IRCX; `protsupp.cpp` connects protocol events to user/room state
and Comic Chat control data; and `CPageView`, `CPanel`, `CBalloon`,
`CAvatarX`, `CBodyCam`, and `CChatBackdrop` build the comic pages.

## Qt port: binding rules

The Qt port remains an MFC/Win32 replacement layer for the same application.
Qt does not justify new filenames, new module boundaries, or new product
logic.

| Rule | Binding effect for `v2.5-beta-1-qt` |
| --- | --- |
| Prefer original filenames | A Qt replacement for `ircsock.cpp` is named `src/original/ircsock.cpp`, not `irc_transport.cpp`. A Qt replacement for `pageview.cpp` is named `src/original/pageview.cpp`, not `comic_view.cpp`. |
| Mirror original module boundaries | Socket/parser code stays in `ircsock.*`; IRC commands and sending in `ircproto.*`; user/room/annotation glue in `protsupp.*`; the comic view in `pageview.*`; panels and starring in `panel.*`; balloons in `balloon.*`/`wmini.cpp`; BodyCam in `bodycam.*`; avatar protocol and emotion handling in `avatario.*`; and AVB/BGB handling in `avbfile.*`. |
| No dummy data in normal code paths | The port must not display sample names, sample messages, invented starring data, or local substitute participants merely to populate the GUI. |
| No invented IRC, join, or starring flow | Join, `353`, `366`, the member list, and starring are derived only from the original code. If an original path is incomplete in the port, the affected UI remains empty or carries a clearly marked developer TODO, never normal chat content. |
| Replace MFC/Win32 with Qt while retaining names | If code cannot be reused directly because of `CWnd`, `CDC`, `CAsyncSocket`, resource templates, or the Win32 palette, the Qt replacement retains the original class and function names wherever C++/Qt permits. |
| Add files only when no meaningful counterpart exists | Qt-only infrastructure such as asset caches, path resolution, Qt adapters, or the fixed Windows 98 palette may live under `src/qt/`. New architecture files are prohibited when an original module exists. |
| Use explicit Windows 98 colors | The Qt GUI uses fixed Windows 98 standard colors rather than host system colors. |
| Mark deviations | Every ported original file briefly identifies its original file and the MFC/Win32 boundaries replaced by Qt. |

## Qt port: binding work plan and parity matrix

This section is the persistent implementation record for the port. It keeps
source evidence, decisions, unresolved items, and verification results in the
repository rather than only in conversational context.

### Permitted evidence

1. Behavior, flow, layout, and data formats are defined exclusively by the
   files under `v2.5-beta-1-modern`: C/C++ sources, headers, `chat.rc`,
   `resource.h`, IDL, and build inputs.
2. README files, Help files, protocol captures, and other documentation must
   not define behavior absent from the source code. A missing source basis is
   recorded as `unresolved`.
3. Original sample text from resources may appear only in the exact original
   dialog or preview for which `chat.rc` or the original code defines it. It
   must never be used as chat, member, join, or starring data.
4. Every image and art file is read byte-for-byte in its existing format. No
   PNG, JPEG, SVG, or other converted copy is committed or persisted at
   runtime. Decoding BMP/DIB, AVB, or BGB data into a transient drawing buffer
   is required by the original behavior and is not asset conversion.

### Audit ledger and reuse classes

The original tree contains 313 files. `chat.mak` names 84 object modules,
including the PCH and generated MIDL object; the Qt CMake build lists 63
`src/original/*.cpp`/`.c` modules and three Qt helpers. A file is considered
audited only when the ledger identifies its path, build status, purpose,
relevant classes/functions/IDs, dependencies, Qt destination, reuse class,
and unimplemented scope.

The mechanical completeness check uses three disjoint sets: 187 source/build
files, 109 files under `res/`, `comicart/`, and `artpack1/` including
`archive/`, and 17 other files, totaling exactly 313 files. Each of the 187
source/build paths has been read completely and has a category or build-status
entry below. The second set contains 108 unchanged binary assets plus
`res/chat.rc2`; the `original-assets` test enumerates all 109 paths and opens
them only at their canonical original locations. The 17 other files are:
`README.md`, `cchat.cnt`, `cchat.hlp`, `chat.reg`, `dirs`, `irc.txt`,
`ircnew.txt`, `ircorig.txt`, `license.txt`, `oldchat.reg`, `profile.txt`,
`readme.gif`, `readme.htm`, `readme.txt`, `rtwsupport.txt`, `strings.txt` and
`support.txt`. They have been read completely as text, or checked by format,
metadata, and hash for the two binary files. Under “Permitted evidence,” they
do not define UI, IRC, or product behavior.

The 17 auxiliary paths are classified as follows. `README.md` describes only
the Modern build and its compiler/manifest adjustments; the source code wins
where they differ. `cchat.cnt` is the topic index for the binary Windows 95
Help system `cchat.hlp`; the Help command and `helpids.h` matter to the port,
but HLP contents are not a substitute specification. `chat.reg` and
`oldchat.reg` register historical CCR/OLE shell classes only; `dirs` is the
BUILD.EXE subdirectory list. `irc.txt`, `ircnew.txt`, and `ircorig.txt` are
real debug captures; only `irc.txt` is referenced by an `#ifdef IRCLOG`
output path. Their real names, messages, server values, and flows are not used
as test fixtures, startup content, or protocol specifications. `strings.txt`
is an octal-encoded dump produced by a debug/Easter-egg output path and is
never read. `profile.txt` is not read either; the only permitted default
profile text remains resource `ID_DEFAULT_PROFILE` in its source-backed
PropertyPage/GetInfo path. `readme.txt`, `readme.htm`, `readme.gif`,
`support.txt`, `rtwsupport.txt`, and `license.txt` are distribution
documentation. The GIF remains unchanged and is not a UI reference.
`cchat.hlp` is identified as Windows 95 Help titled `Microsoft Chat Help`;
neither the HLP nor the GIF was rendered or treated as a visual product
specification.

Exact direct-asset ledger; every file remains in this format and location:

| Directory | Fully checked paths |
| --- | --- |
| `artpack1/archive/` | `bolo.avb`, `cro.avb`, `den.bmp`, `denise.avb`, `kevin.avb`, `kwensa.avb`, `lynnea.avb`, `maynard.avb`, `rebecca.avb`, `sage.avb`, `scotty.avb`, `volcano.bmp` |
| `artpack1/` | `bolo.avb`, `cro.avb`, `den.bgb`, `denise.avb`, `kevin.avb`, `kwensa.avb`, `lynnea.avb`, `maynard.avb`, `rebecca.avb`, `sage.avb`, `scotty.avb`, `volcano.bgb` |
| `comicart/` | `anna.avb`, `armando.avb`, `bolo.avb`, `buck.avb`, `buckroom.bgb`, `clouds.bgb`, `connor.avb`, `cro.avb`, `dan.avb`, `denise.avb`, `field.bgb`, `glenda.avb`, `hugh.avb`, `jordan.avb`, `kirby.avb`, `lance.avb`, `lynnea.avb`, `margaret.avb`, `mike.avb`, `pastoral.bgb`, `pedagog.avb`, `rainbow.avb`, `room.bgb`, `space.bgb`, `susan.avb`, `tiki.avb`, `tongtyed.avb`, `tux.avb`, `veronica.avb`, `waf.avb`, `xeno.avb`, `yellow.bgb` |
| `res/` | `activ.bmp`, `active.bmp`, `avatar.ico`, `backgd.ico`, `balloons.bmp`, `bitmap1.bmp`, `bmp00001.bmp`, `chat.ico`, `chat.rc2`, `chatdoc.ico`, `connect.bmp`, `fc_ang_l.bmp`, `fc_ang_s.bmp`, `fc_bor_l.bmp`, `fc_bor_s.bmp`, `fc_coy_l.bmp`, `fc_coy_s.bmp`, `fc_hap_l.bmp`, `fc_hap_s.bmp`, `fc_hp2_s.bmp`, `fc_laf_l.bmp`, `fc_laf_s.bmp`, `fc_neu_l.bmp`, `fc_neu_s.bmp`, `fc_sad_l.bmp`, `fc_sad_s.bmp`, `fc_sca_l.bmp`, `fc_sca_s.bmp`, `fc_sho_l.bmp`, `fc_sho_s.bmp`, `inactive.bmp`, `itoolbar.bmp`, `member.bmp`, `nm_main.bmp`, `nmutool.bmp`, `notif.ico`, `oldnew.bmp`, `palette.dib`, `ratings.ico`, `room.ico`, `ruleset.ico`, `stopped.bmp`, `tabbar.bmp`, `talkers.bmp`, `texttool.bmp`, `tiki2.bmp`, `tiki2.rle`, `tonet.ico`, `toolbar.bmp`, `tosrv.ico`, `usertool.bmp`, `whisper.ico`, `whsprbar.bmp` |

The original parser indexes all 45 AVBs, lazy-loads their neutral pose and
icon, and renders a real pose; all nine BGBs load with metadata and backdrop
pose. The 41 BMPs, `palette.dib`, and `tiki2.rle` pass through the original DIB
path. The eleven ICOs are handed to Qt directly from their original paths.
Neither audit nor build creates PNGs, converted intermediate assets, or copied
resource files.

| Reuse class | Meaning | Binding implementation rule |
| --- | --- | --- |
| `R0` | Platform-neutral original code | Function bodies and control flow are retained unchanged; only includes, type aliases, and compiler corrections are allowed. |
| `R1` | Original algorithm using MFC/Win32 data types | Algorithms, constants, ordering, and function names remain intact; only containers, strings, file/thread/socket primitives, or drawing primitives are replaced mechanically. |
| `R2` | MFC/Win32 UI boundary | Original class, methods, resource IDs, message-map behavior, and module boundary remain intact; QWidget/QDialog/QAction/QPainter replace only the platform surface. |
| `R3` | Canonically built Windows-only integration without a Qt counterpart | Every visible function that maps to Qt is extracted. The non-mappable portion is documented with the exact original symbol and platform reason as `unresolved`/`non-portable`, never silently simulated. |
| `X` | Not built by `chat.mak` | The file is read and indexed but excluded from the Qt build unless a canonically built original module demonstrably requires it. |
| `A` | Asset, resource, or document | Byte/format/reference audit only; no invented product logic and no format conversion. |

### Known hard parity gaps

A successful build does not establish original parity for these items.

| Area | Original evidence | Qt-port status |
| --- | --- | --- |
| Connection-state values | `defines.h`: `CX_DISCONNECTED=0`, `CX_INCHANNEL=1`, `CX_CONNECTING=2`, `CX_NOCHANNEL=3`, `CX_CONNECTED=4` | Fixed: Qt uses the byte-identical `src/original/defines.h`; `ConnectionStatus` no longer defines a divergent enumeration. |
| Toolbar flags | `defines.h`: Main `4`, Member `8`, Text `16`, OldRead `32` | Fixed: duplicate Qt constants were removed; `CChatApp::m_iShowBars` uses the original macros. |
| Startup/login | `chat.cpp`, `setupdlg.cpp`, `chatsrv.cpp`, `ircsock.cpp`, and `../artifacts/inc/ccommon.h` selected by the original build | The invented fallback nick `ComicChat` and invented separate port control are removed. `NoMachine` is source-backed: `ircsock.cpp` uses `g_szNoMachine`, whose build-selected definition is exactly `"NoMachine"`. Defaults, DDV nick validation, on-connect action, resource status, service/group model, five-socket connector, password dialog, Auth 0/1, retry/reconnect, and IRC/IRCX detection are ported. Windows SSPI execution for Auth 2/3 and additional parser/error branches are unimplemented. |
| IRC parser/dispatch | `ircsock.cpp`, `ircsock.h` | The shared line parser, original command table, and numerics required by login, join/NAMES, messages, room/user lists, administration, MOTD/away, rules, and notifications are ported. Result/error groups, SSPI, legacy encoding, and query paths for unimplemented modules are incomplete. |
| Comic core | `pageview.cpp`, `panel.cpp`, `balloon.cpp`, geometry modules | The invented Qt path with fixed rectangles, ellipses, a stick figure, and parallel `nick/text` lists is removed. Messages pass through `CChatDoc::AddLine` and the original `CUnitPanelPage`, `CUnitPanel`, `CBody*`, and `CBWoodring*` classes. Resize history replay, URL handoff/hits, avatar/label hit tests, comic/member context menus, on-screen iteration of every `CPage`, and the view-printing chain have source-bound regressions. The original defines no page stacking: `AddNewPage()` places every page at `(0,0)` and leaves position calculation as a TODO. The Qt port preserves that overlap and records visual stacking as `unresolved`. |
| Avatar/backdrop I/O | `avbfile.cpp`, `avatar.cpp`, `backdrop.cpp`, `dib.cpp` | The format core and production drawing are ported: AVB/BGB, DIB/zlib, palettes, masks, lazy pose loading, real icons, avatar index, separate screen/printer backdrop caches, source rectangles, and BMP/BGB backdrops are read directly from original files. Web download/notification and art-pack switching are unimplemented. |
| Assets | Original accesses in `chat.cpp`, `avatar.cpp`, `backdrop.cpp`, `chat.rc` | Fixed for bundled local data: `originalassets.*` accepts only existing canonical original paths; `dib.*`, `avbfile.*`, and `backdrop.*` read existing formats directly. No asset copy or conversion is produced. |
| UI/commands | `chat.rc`, message maps in `chat.cpp`, `chatdoc.cpp`, `mainfrm.cpp` | Main, text, formatting, status, and member menus are read directly from `chat.rc`; the invented normal-path `ShowOriginalTodo` is removed. The three-band coolbar, button styles, context menu, and persistence are ported. Many domain commands and parts of enable/check/accelerator semantics remain disabled pending their original modules. |
| Text/formatting | `saywnd.cpp`, `rtfctrl.cpp`, `format.cpp`, `textcore.cpp`, `textview.cpp`, `status.cpp`, `whisprbx.cpp` | Generic output placeholders and the missing non-comic echo/receive path are fixed: `CTextView`, `CTextEdit`, `CStatusView`, `CWhisperBox`, TextCore message types, resource headers, 256000/65536 buffers, formatting ranges, separate Doskey history, Say/Whisper formatting handoff, and the original `CIrcPrint`/`AddToStatus` core are active. URL detection/launch, formatted view printing, pre/post rules for `ProcessSay`/`bAddToWhisperBox`, Alt+0..9, and the active Far-East DBCS/JIS path are connected. Non-Far-East ACP edge cases, more result/error print descriptors, the complete custom text-font UI, and sound from `sounddlg.*`/`mcithrd.*` are unimplemented. |

### Work order and definition of done

The order is binding. A phase is marked `implemented` only when its completion
criteria and tests are satisfied and recorded here.

| Phase | Scope status | Considered implemented when |
| --- | --- | --- |
| 0. Complete source index | **Complete.** All 313 paths are partitioned into 187 source/build files, 108 binary assets plus `res/chat.rc2`, and 17 auxiliary paths. Every source/build file was read completely; assets were enumerated and checked directly; auxiliary paths were read or classified as binary and marked non-authoritative. | The 84 build objects remain mapped to an original path, Qt destination, or specifically named platform blocker; X-class files remain excluded. |
| 1. Exact base types and constants | `defines.h`, `resource.h`, `chatprot.h`, query/user/room flags, and limits are partially or incorrectly copied. | Automated compile/unit checks compare every port-relevant numeric value and structure default with the original definitions; no duplicate divergent constant remains. |
| 2. Direct original assets and image I/O | **Implemented for all bundled local assets.** Web download, cache synchronization, and art-pack switching belong to separate integration scope. | `originalassets.*` opens `res`, `comicart`, and `artpack1` directly under the configured original root; hashes remain unchanged; every bundled AVB/BGB/BMP/DIB/RLE/ICO/GIF file is recognized without a converted copy; metadata, a Simple and Complex avatar pose, and a BMP and BGB backdrop pass through the original parser paths. |
| 3. Avatar/emotion/BodyCam | Pose/body/emotion/index core, AVB drawing, and BodyCam with original emotion bitmaps, mouse, keyboard, freeze, and send-expression behavior are ported. Character double-click, focus tab cycle, several error/PropertyPage couplings, download/RealInfo, and ArtDir switching are unimplemented. | `avatar.*`, `avatario.*`, `bodycam.*`, `textpose.cpp`, and required geometry produce the same selection/index/emotion results as the original functions; BodyCam draws the selected real avatar using only original emotion bitmaps; mouse, keyboard, freeze, and `<Chr>` paths match the original branches. |
| 4. Comic layout and rendering | Panel/page lifecycle, real backdrops/avatars, speaker/Talk-To order, zoom, collision layout, Woodring balloons, think/whisper/action, scrolling, resize autofit/history reflow, body/label hit testing, selection, tooltip, URL hotlinks, comic/member context menus, starring, on-screen iteration of all pages, and printing are ported. Stacked page positioning is absent from the original and is recorded as `unresolved`, not a port defect. | `CPageView`, `CPage`, `CUnitPanelPage`, `CPanel`, `CUnitPanel`, `CBalloon`, and original geometry modules preserve call order; panels use real backdrops/avatars; wrapping, SplitHeight, tail routing, panel changes, autofit, URL formatting segments, resource context menus, starring, and printer-specific grids derive from original state without substitute graphics or invented dimensions. Multiple pages are drawn, measured, and hit-tested in original list order; the port invents no page spacing. |
| 5. IRC/IRCX transport and parser | The line parser, command table, required numerics/queries, Auth 0/1, service reconnect, raw-byte receive, selective `CSInString` conversion, and active Far-East DBCS/JIS core are ported. The full original switch, result/error groups, SSPI/Auth 2/3, and non-Far-East ACP edge cases are incomplete. | `ircsock.*`, `ircproto.*`, `query.*`, `intl.*`, JIS/SJIS, and glue handle every command/numeric in the original switch with identical arguments, status/query effects, and send strings; socket I/O is the only Qt replacement. Tests feed only source-derived lines and compare exact outgoing bytes and state changes. |
| 6. Join, user/room, and starring | Self JOIN, `353`/`366`, enumeration, member list, title/starring, nick/part/quit/kick, avatar assignment, Talk-To, and ignore/flood behavior are ported for tested original branches. Additional mode/WHO/topic/error branches and multi-room edge cases are incomplete. | Self JOIN, `353`, `366`, query lifecycle, `CIUserJoin`, `AddToMembersList`, `ProcessEndEnumeration`, `UpdateTitle`, `AddStars`, and `AddStarsAux` follow the documented original path; no invented star appears before real NAMES data ends; unresolved original couplings remain empty rather than being replaced by assumptions. |
| 7. Sending/receiving Comic Chat data | `bInsertAnnotations`, gesture/expression, modes, Talk-To, IRCX DATA, plain-IRC prefix, formatting chunks, local echo, and the associated `ProcessSay`/panel path are ported and tested. Legacy ACP/DBCS, sound, and disconnected CTCP/URL/DCC edge paths are incomplete. | Annotations and IRC lines produced by `bChatSendText` match `IndexToByte`, `EmotionToBytes`, `BM2SM`, `bInsertAnnotations`, and `bChatSendToTarget` byte-for-byte; receive code sets the same `CUserDisplayInfo` fields and invokes the same `ProcessSay`/history/panel path. |
| 8. Document/history/text mode/input | HistoryEntry/replay core, text/status view, RichEdit, Doskey, Whisper Box, its rule/macro cross-paths, and the MDI base model with hidden status document and room child frames are ported. Full conversation-file/dialog integration, focus/enable paths, child-frame persistence, URL, and sound integration are incomplete. | Original classes and methods exist under their original filenames; comic/text/status/whisper views show the same history events, formatting, and enable states; Enter/Shift/Tab/accelerators and Say/Think/Whisper/Action/Sound follow `saywnd.*`/`rtfctrl.*`. |
| 9. Main UI and all resources | `QMdiArea` replaces the MFC MDI layer; room/status child frames, tab activation, basic window commands, and the three-band coolbar with resource buttons, styles, context menu, and persistence are ported. Exact menus/submenus, complete command UI, additional context menus, accelerators, status bar, and Win98 metrics are partial. | Every visible action/subaction in `chat.rc` is wired with the same ID, order, caption, shortcut, check, and enable logic; layout/splitter/tab bar follow original methods and resource dimensions; colors are fixed Win98 values rather than host palette values. |
| 10. Dialogs, lists, and administration | Setup/Personal/Character/Background, direct `CServersPage`, Channel Properties/Create, MOTD/Away, room/user lists, Kick/Ban/Invite/Invitation, and Host/Speaker/Spectator commands are partially ported or implemented for their evidenced core. The Servers page is the final Options tab and applies atomically through `CChatServiceUI`. Settings, Comic Font and Text Font options pages, profile-format roundtrip, and additional member actions are incomplete; the original `eOnInvitation` hook is connected and tested. | Every canonically built dialog module mirrors controls, IDs, validation, defaults, and protocol calls from `chat.rc` and message maps; lists sort/filter and buttons enable exactly as in the original. |
| 11. Rules, automation, notifications, and auxiliary functions | `rules.*`, `CCDaemonExt`, `notif.*`, `notipage.*`, `autopage.*`, macros, and the source-supported part of `actions.*` are ported. Four-page Automation, version-1 rule/`.crs`/notification serialization, `REG_MULTI_SZ` macros, rule/notification registry roundtrips, matching, filtering, delay, flood, direct events, server-side enumeration, and definition/user windows are tested. `CChatServiceList` supplies server parameters to rules/notifications, and `aConnect` uses the original reconnect path. Sound and text-file actions are unimplemented and remain disabled without substitute data. Favorites, Save/Open/Print, URLs, and settings belong to their original modules. | Every module built by `chat.mak` is ported and its source-backed normal paths function; persistence formats and rule serialization remain compatible; disabled Modern download paths remain disabled exactly as in the original source. |
| 12. Platform boundaries and completion | OLE/DocObject/Automation and historical Win32 integrations do not always have Qt/Linux counterparts; NetMeeting is excluded from the Modern build. | Every visibly portable function is implemented; each blocker names the file, symbol, and missing platform capability. NetMeeting and unbuilt legacy behavior are not invented. Debug and Release CMake builds and all tests run from `v2.5-beta-1-qt/build`; normal code paths contain no placeholder or dummy output. |

### Completed work block: on-screen iteration of multiple comic pages

Source evidence: `CPageView::OnDraw` in `pageview.cpp` iterates
`CChatDoc::m_pages` from the head, reads `GetBBox()` for every page, stops when
a page lies below the clipping area, and invokes `CPage::Draw()` for an
intersection. `FindAvatarUnderPoint()` and `FindLabelUnderPoint()` use the
same page and panel order. The original comments explicitly leave visible
page lists and panel bounding boxes as unimplemented work.
`CUnitPanelPage::GetBBox` and `RefreshPanelN` include `m_leftX`/`m_topY`, while
the on-screen `Draw` starts panel coordinates at `(0,0)`. Only
`CChatDoc::AddNewPage()` writes these fields in the original tree, setting
every page to `(0,0)` with the comment `later calculate from existing pages`.
In addition, `m_panelsPerColumn=-1` and `CUnitPanelPage::AddPanel()` always
returns `TRUE`; the normal `AddLine` path therefore creates no subsequent
page.

Binding plan and acceptance criteria for this block:

1. `CPageView::paintEvent`, like `OnDraw`, draws every existing page in list
   order using only its real `CPanel::Draw` paths. It creates no test figures
   or substitute panels.
2. `updateScrollRanges` computes the extent as the union of original
   rectangles occupied by every page; it neither adds pages nor arranges them
   artificially below one another.
3. Avatar and label hit tests preserve the original page, panel, and
   element/body order. Because each page's original coordinates start at
   `(0,0)`, overlap is preserved as well.
4. A regression test may create empty structural original panel objects only
   to prove reachability. It must verify drawing order, bounding-box union,
   and hit order without names, messages, participants, or starring data.
5. The block is accepted only when the application and all CTests build and
   pass from `v2.5-beta-1-qt/build`. A visually stacked multi-page view remains
   `unresolved` without original source code defining it.

Verification: `paintEvent` iterates every page and real panel in the same
order as `OnDraw`; `updateScrollRanges` forms the maximum extent from all
`GetBBox()` results without adding page offsets. The per-page avatar/label hit
tests retain original list order. `original-page-iteration` uses empty
`CUnitPanel` structures to verify overdraw at the source-backed `(0,0)`, the
extent of three panels on another page, and the first overlapping label using
original resource value `ID_STARRING`; it creates no users, messages, or star
list. A test failure exposed a missing test precondition: `SetChatDoc`
immediately initializes the title page like the source, which requires
`InitializeComicsFonts`/`SetFonts`. A native backtrace established
`CChatDoc::InitMyDocument -> CUnitPanelPage::AddTitle ->
CLabel::BreakIntoLines`; the product path was not changed. The full build and
40/40 CTests pass. The absent page stacking remains documented as an original
TODO.

### Completed work block: `chatbars.*`/`coolbar.*`

Source evidence: the original `CMainFrame` owns exactly one
`CChatToolBar m_wndToolBar`. It is not a single button strip, but a
`CCoolBarEx` containing three managed `CCoolToolBarEx` bands in the default
order `IDR_MAINFRAME`, `IDR_USERTOOLBAR`, `IDR_TEXTTOOLBAR`.
`CChatToolBar::Create` derives visibility from `SB_TOOLBAR_MAIN`,
`SB_TOOLBAR_MEMBER`, and `SB_TOOLBAR_TEXT`, and loads the saved band state.
The Qt implementation preserves this module boundary instead of creating
three independent toolbar managers in `mainfrm.cpp`.

The original state format from `chatbars.cpp` is packed through `pshpack1`:
each band stores `WORD wID`, `WORD wLength`, and `BYTE byFlags`, followed by
an all-zero sentinel record. `CBBSV_NEWLINE=1` represents `RBBS_BREAK`.
Saving writes the visible band order before old bands that are not inserted;
loading removes every band and inserts them again in record order. Visibility
is separate from this format and remains in `m_iShowBars`. The Common
Controls 4.70 branch physically removes hidden bands. Qt follows the 4.71
boundary by changing band visibility, but reads and writes the same records.

`CChatToolBar::OnPrepareToolBar` assigns only the source-backed styles:
Favorites is a dropdown; Comic/Text is a check group; Chatroom List is a
group; Away is a check item; and Bold/Italic/Underline/Fixed/Symbol are check
items. A right click loads exactly `IDR_TOOLBARCONTEXT` with Main, Member, and
Text. `ToggleBar` switches individual bands through the three
`CHAT_TOOLBAR_*` indices or the whole coolbar through `CHAT_TOOLBAR_WHOLE`,
and writes only the `SB_TOOLBAR_ANY` bits to `theApp.m_iShowBars`. The
Favorites arrow opens the existing Favorites main menu and never creates
local favorite entries.

Binding plan and acceptance criteria for this block:

1. `coolbar.*` contains the original classes `CCoolBar`, `CCoolToolBar`,
   `CCoolToolBarEx`, and `CCoolBarEx`; Qt replaces only rebar/toolbar windows
   and layout messages. Ownership, the band map, default order, and the
   original methods `AddToolBarBands`, `AddSingleBand`, `FindBand`, `ShowBar`,
   `IsBarShown`, `GetToolBarFromID`, `SaveStateToBuffer`, and
   `LoadStateFromBuffer` remain at this module boundary.
2. `chatbars.*` builds the three bars directly from the unmodified `chat.rc`
   toolbar records and BMP sprite sheets. `OnPrepareToolBar`, `ToggleBar`,
   and `OnContextMenu` retain their original names and documented effects.
3. `mainfrm.*`, like the original, owns only one `CChatToolBar` manager.
   Actions remain bound to existing original command handlers; locally
   invented toolbar logic does not replace command-enable logic.
4. The test covers the three resource bars and their 13/8/7 entries,
   check/exclusive/dropdown styles, individual and whole-bar toggles, exact
   little-endian five-byte records, the sentinel, ordering, and newline
   restoration. Test menus and labels come only from `chat.rc`; no chat,
   user, or favorite data is created.
5. The block is accepted when its regression test, the UI structure test,
   full build, all CTests, and offscreen application startup pass. Persistent
   registry/QSettings integration uses the general
   `setupdlg.cpp::LoadFromReg`/`SaveToReg` boundary; no separate persistence
   schema is introduced.

Implementation: `CMainFrame` owns exactly one `CChatToolBar` manager.
`coolbar.*` contains all four original classes and manages the three bands in
the source-backed default order. `chatbars.*` creates the 13/8/7 buttons
directly from the `chat.rc` toolbar records and unmodified original BMP files;
images are neither converted nor reconstructed from an external rendering.
Dropdown, group, check-group, and check styles, the resource context menu,
individual and whole-bar toggles, and the packed five-byte state format with
its null sentinel are connected. `original-coolbar` verifies these boundaries,
including little-endian bytes, order, and newline restoration, without chat,
user, or favorite data. Verification includes a full build, 41/41 CTests,
and an offscreen startup that remains active until the intentional
three-second timeout. `LoadFromReg`/`SaveToReg` persists the buffer under the
original value name `ToolBarState`.

### Completed work block: `setupdlg.cpp::LoadFromReg`/`SaveToReg`

Source evidence: `LoadFromReg` reads the application values under
`Software\\Microsoft\\Microsoft Comic Chat`, followed by services, macros,
rules, and notifications. `SaveToReg(TRUE)` is not a reduced full save: when
a main frame exists it writes only `XFrame`, `YFrame`, `CXFrame`, `CYFrame`,
`Maximized`, the live `ShowBars` value, and `ToolBarState`, and returns.
`SaveToReg(FALSE)` writes panel, identity, connection, display, and option
values, services, macros, rules, and notifications, but does not write the
window placement or coolbar again. The original shutdown sequence invokes
the short frame save before the full save.

Field index for this port boundary:

| Original values | Source-backed meaning | Qt implementation / unresolved boundary |
| --- | --- | --- |
| `XFrame`, `YFrame`, `CXFrame`, `CYFrame`, `Maximized` | Normal main-frame geometry and maximized state; `MakeRectVisibleOnScreen` corrects stale or offscreen rectangles. | Same-named `CChatApp` fields; `QScreen::availableGeometry` replaces only the Win32 visibility check. |
| `ShowBars`, `ToolBarState` | Status bar plus live `SB_TOOLBAR_*` state; exact packed coolbar buffer. `ToolBarState2` belongs only to the excluded `CB32SUPPORT` build. | These exact value names; `QByteArray` remains byte-identical to the `COOLBARBANDSAVE` format. |
| `UPNLWidth`, `UPNLHeight`, `UnitsWide` | Static comic-panel dimensions and column count. | Direct `CUnitPanelPage` getters and setters. |
| `FavoritesDir`, `LastFavorite`, `FileTXDir`, `IRCServer`, `IRCChannel` | Stored paths/favorite and stored service/room selection. | Same-named application fields or the existing service model. Favorite and file-transfer functionality is unresolved separately; persistence invents no entry or file. |
| `Name`, `RealName`, `Email`, `HomePage`, `Profile`, `AwayMsg` | User values; the first four use their respective original limits. An empty profile is notably not deleted during save. | Existing original fields and limits; no prefilled test user. |
| `Character`, `Backdrop` | The original stores these only while comic view is active. | Only real, directly enumerated AVB/BGB/BMP names; no asset copy or conversion. |
| `ShowComicView`, `ComicsData`, `MemberListStyle`, `PromptForSave`, `AcceptWhispers` | View mode, UDI sending, global member display, and prompts. `ShowComicView` is written only when `m_bSaveViewMode` is set. | Existing original state functions; new documents inherit `MemberListStyle` from application state. |
| `AutoDownloadChars`, `AutoDownloadBackdrops`, `ShowArrivals`, `AllowInvites`, `AllowFileTXs`, `PlaySounds`, `NoMIDI`, `AcceptNMCalls`, `ShowIdentity`, `ListRegistered` | Option flags. This source reads but does not write `NoMIDI`. | Same-named original application fields. Stored flags do not claim that unresolved download, DCC, sound, or NetMeeting functions work. |
| `FloodControl`, `RulesControl` | Three and two packed bytes respectively. The loader accepts count and interval only when both are nonzero; it always accepts flood flags. | Same bit layout and validation. |
| `ComicsFont`, `ComicsColor`, `SoundPath`, `AutoGreeting`, `AutoGreetType`, `HostHighlight`, `TextSpacing` | Comic font/color, sound path, persistent `%name`/`%room` tokens, and text display. | `QFont` replaces `LOGFONT`; token replacement retains the original name `bReplaceMacroTokens` in `protsupp.*`. Sound execution is unresolved. |
| `TextFonts`, `HighlightedTextFonts` | Binary arrays of `NREGULARFONTS`/`NHIGHLIGHTEDFONTS` `CHARFORMAT` records. | The original role arrays and binary representation are retained; the data is not reinterpreted as one `QFont`. |
| `Flags1`, `Flags0`, `WhisperDims`, `NotifDims`, `OnConnect` | Feature bits, window rectangles, and connection action; full save sets `F0_ALREADYRUN`. | Direct fields/QRect and original values. |
| `Macros/<0..9>` | Defined slots as name-NUL-ControlFull-NUL-NUL; undefined slots are removed. | Existing `CMacro::Serialize`/`UnSerialize` and `QByteArray`, without sample macros. |
| Rules/Notifications subtrees | Separate versioned original binary formats and a separate lifecycle following application-value saving. | `rules.*`/`notif.*` store unmodified serialized bytes under `RuleSets`/`Notifications`; `QSettings` replaces only HKCU and REG_DWORD/REG_BINARY. There is no QVariant domain format and no persisted daemon user. |
| HKLM `BaseDir`/`ArtDir` | Machine-wide Win32 path override. | Not exposed as a mutable user path: the port uses the required CMake `COMIC_CHAT_ORIGINAL_ROOT` and reads original assets directly. |

Binding plan and acceptance criteria for this block:

1. `CChatApp` exposes the source-backed state fields and
   `LoadFromReg`/`SaveToReg` under their original names. `QSettings` replaces
   only HKCU; root and value names remain identical to the original.
2. `protsupp.*` contains `ReplaceToken` and `bReplaceMacroTokens` in source
   order. They process only `szUserToken`, `szRoomToken`, `IDS_USERVARIABLE`,
   and `IDS_ROOMVARIABLE`.
3. Startup initializes original defaults and fonts, loads state, and applies
   frame geometry/maximization before showing the frame. Shutdown performs
   short save and full save in the source-backed order.
4. A regression test uses an isolated QSettings path, resource values only,
   and directly enumerated original assets. It covers short/full key
   separation, coolbar bytes, panel dimensions, old toolbar-bit migration,
   flood/rules packing, token roundtrip, macro multi-string, conditional
   view/character persistence, the profile quirk, and the member-list
   default. It creates no users, messages, rooms, favorites, or starring data.
5. The block is accepted when its regression test, full build, all CTests,
   and offscreen startup pass. Text-font role arrays and binary
   rules/notification persistence retain their separate module boundaries.

Verification: `original-persistence` covers the short/full boundaries with
an isolated QSettings path. A full build, 42/42 CTests, and isolated offscreen
startup through the intentional three-second timeout pass. The test creates
no user, message, room, favorite, or starring data. Rules/notification
registry adapters are covered by their own work block; the text-font role
arrays are covered by the dedicated text-font block.

### Completed work block: rules/notification registry persistence

Source evidence: `CCDynaRules::bSaveRulesToReg` and `bLoadRulesFromReg` in
`rules.cpp` operate under
`Software\\Microsoft\\Microsoft Comic Chat\\RuleSets`. Each rule set is a
subkey bearing its unmodified name. `RuleSetFlags` is
`MAKELONG(m_wFlags & g_wActive, g_wVersion)`; rules follow as `REG_BINARY`
values `0`, `1`, and so on in `m_rgpRules` order, containing the unmodified
bytes from `CCRule::Serialize`. Saving removes only existing rule-set
subkeys. Loading initializes existing `CCRulesData`, skips names already
present, discards a set with the wrong version high word, masks the low word
to `g_wActive`, and accepts every fully deserializable binary value in the
registry backend's enumeration order. A missing root key notably still
returns `TRUE`; existing rule sets are not cleared globally.

`CCDynaNotifs::bSaveNotifsToReg` and `bLoadNotifsFromReg` in `notif.cpp`
operate under the same root in the `Notifications` subkey. The
`NotificationFlags` DWORD is `MAKELONG(m_wFlags, g_wVersion)`; definitions
are stored as `0`, `1`, and so on with the exact bytes produced by
`CCNotif::Serialize`. Saving does not delete the whole key; it attempts to
remove only numeric old values from the new count through the previous total
value count. A wrong version high word sets `m_wFlags=0` during load. A
binary value is accepted only if `UnSerialize` consumes the complete stored
length. Runtime notification users, WHO state, timers, and modified counters
are never persisted.

Binding plan and acceptance criteria for this block:

1. The two original modules expose their registry functions under the same
   symbol names. `QSettings` replaces only HKCU and REG_DWORD/REG_BINARY;
   keys, version words, masks, binary bytes, and add/skip/error order remain
   unchanged.
2. The sole new Qt helper is `src/qt/originalsettings.*`. No useful original
   counterpart exists because it only replaces global Win32 registry access
   with the shared QSettings root. It knows no domain data and introduces no
   persistence schema.
3. QSettings `childGroups()`/`childKeys()` replace only
   `RegEnumKeyEx`/`RegEnumValue`. No additional order index is invented; the
   backend enumeration order remains the documented platform boundary.
4. A regression test uses an isolated QSettings path. Rule sets, rule bytes,
   names, and parameters come only from existing `IDS_SAMPLES_*` and
   `IDS_GENERAL_*` resources. A notification uses only existing default and
   operator resources, as in the serialization test. It creates no chat
   participant, message, room, starring data, WHO user, or daemon user.
5. Tests cover exact root/value names and bytes, high/low-word flags,
   existing set names, duplicate skipping, invalid versions, complete
   deserialization, rule-set subkey cleanup, and the original limited
   notification-tail cleanup. Missing keys return `TRUE`, as in the source.
6. The block is accepted when its test, the rules/notification/application
   persistence tests, full build, all CTests, and isolated offscreen startup
   pass. Text-font role arrays retain their own work block.

Verification: `original-registry-persistence` proves exact keys, DWORDs,
binary bytes, and duplicate/version/full-consumption/cleanup boundaries. A
full build, 43/43 CTests, and offscreen startup isolated with its own
`XDG_CONFIG_HOME` through the intentional three-second timeout pass. Test
definitions use only existing rule/default resources; no IRC user, message,
room, starring data, WHO user, or daemon user is created.

### Work block: `TextFonts`/`HighlightedTextFonts` roles

Source evidence: `defines.h` defines `NREGULARFONTS=10`,
`NHIGHLIGHTEDFONTS=8`, and therefore `NFONTS=18`. In `chat.h`, `CChatApp`
owns exactly one `CHARFORMAT m_cfArray[NFONTS]` and separate
`m_bCfInitialized` and `m_bCfHLInitialized` flags. This is the 60-byte record
used by the ANSI RichEdit boundary, containing `cbSize`, `dwMask`,
`dwEffects`, `yHeight`, `yOffset`, `crTextColor`, `bCharSet`,
`bPitchAndFamily`, and the 32-byte `szFaceName` field. Registry persistence
must not replace it with `QFont` or a QVariant domain format.

`setupdlg.cpp::LoadFromReg` accepts `TextFonts` only when the supplied length
is exactly `sizeof(CHARFORMAT) * NREGULARFONTS`, and sets
`m_bCfInitialized` only in that case. If role 2 carries `CFM_COLOR`, it also
copies that value into `m_textColor`. Independently,
`HighlightedTextFonts` is accepted only at exactly
`sizeof(CHARFORMAT) * NHIGHLIGHTEDFONTS`. When the corresponding flag is set,
`SaveToReg(FALSE)` writes the raw records. Otherwise, it notably writes an
existing binary value with length zero. A missing value and a zero-length
value both select default formats during loading.

The role order in `textview.cpp::InitializeTextCore` is binding:

| Index | Original call/effect |
| --- | --- |
| 0 | `SetSpecificFont(..., mtJoin, FALSE)` for every member status |
| 1 / 2 | Normal header / normal text for every member status |
| 3 / 4 | Whisper header / whisper text for every member status |
| 5 / 6 | Thought header / thought text for every member status |
| 7 | Action text for every member status |
| 8 | One format for each type from `mtGetInfo` up to but excluding `mtEndEnum` |
| 9 | Leave text for every member status |
| 10..17 | Highlight formats 0..7 in the same order |

If regular roles are not initialized, the source sets only the text-view
default format to 10 points. With `bResetSay`, that default format is passed
to `CSayWnd::SetFont` through `ResetSayFont`/`bCHARFORMATToLOGFONT`; otherwise
role 2 is passed. `CSayWnd::SetFont` preserves the selected normal font name,
replaces fixed-pitch and symbol fonts with the GUI font, resets weight and
effects, and enforces the original input height
`-DpiScale(-nFontHeight)`. Once roles and highlights are applied,
`InitializeTextCore` applies the two `HH_BOLD_*` bits to host text and host
headers respectively. `InitializeTextCores` iterates every `g_docs` entry,
skips comic and status views, and initializes actual text views;
`InitializeWhisperCores` separately iterates all existing whisper leaves.
These functions create no document, tab, user, or text.

Binding plan and acceptance criteria for this block:

1. `src/qt/wincompat.h` contains only the fixed Win32 `CHARFORMAT` layout and
   the `CFM_*`/`CFE_*` bits used by these original paths;
   `static_assert(sizeof(CHARFORMAT) == 60)` protects the persisted ANSI
   layout. `CChatApp` owns the 18-element array under its original name.
2. `setupdlg.cpp` reads and writes the two exact original value names, byte
   ranges, and lengths. Invalid or empty input sets no initialization flag;
   full save writes an empty `QByteArray` for an unset flag. Short save is
   unaffected.
3. The Qt RichEdit boundary maps only properties selected by `dwMask` to
   `QTextCharFormat`. `SetSpecificFont`, `ResetSayFont`, and
   `InitializeTextCore` retain their names, role order, and source call order.
   `CFM_CHARSET` remains byte-identical in the record; Qt Unicode has no
   visual ANSI charset switch, so no substitute effect is invented.
4. `CSayWnd::SetFont` and `InitializeTextCores` are connected under their
   original names. Reinitialization modifies only existing main/whisper text
   cores and the existing Say window.
5. `original-persistence` covers zero-length values, exact record bytes,
   separate regular/highlight flags, the role-2 color, and invalid lengths in
   an isolated QSettings path. `original-text-view` covers all ten regular
   assignments, eight highlights, host-bold ordering, the default 10-point
   format, and the global document boundary. Fixtures use only font names,
   colors, and texts from original source or resources; they create no chat
   user, message, room, or starring data beyond source-backed text-view tests.
6. The block is accepted when both regression tests, full build, all CTests,
   and isolated offscreen startup pass and `git diff --check` is clean. The
   complete resource dialog `CTextFontPage`/`CMyFontDialog` from
   `proppage.*`, `txtfntdg.*`, and `IDD_SETTEXTFONT` remains an explicit UI
   gap; this block does not claim dialog parity.

### Implementation record

#### Runtime parity findings and acceptance boundaries

| Area | Source and runtime evidence | Implemented and verified result |
| --- | --- | --- |
| Member/Say icons and delayed initial paint | Runtime inspection of the Qt client found text-only members in comic icon mode, blank source-positioned Say buttons, and a stale first frame after the real NAMES enumeration. Source inspection gives three independent causes. First, original `AddToMembersList` calls `AddToImageList`; `CMemberList::OnGetdispinfo` then selects the original avatar's cached 40-pixel image and the ignored/away/operator/spectator/normal status image from `IDB_MEMBER`, while the Qt path inserted text only. Second, original `IDB_SAY_BAR` is `res/balloons.bmp` with `RGB(192,192,192)` as its mask color, but the Qt mask sense was reversed. Third, MFC passes the owning `CChatDoc` through `CCreateContext` when creating `CPageView`; the Qt constructor sampled `GetChatDoc()` before MDI activation and retained null or the wrong document. `paintEvent` therefore produced white cells, while a pointer hit-test lazily sampled the later active document and accidentally made the pending title/panels visible. Screenshot content is not used as a behavior specification; all expected images and state order come from these source paths. | `protsupp.cpp::AddToImageList` now obtains `GetIconPose()->GetDrawing()` from the directly loaded AVB image, caches it under the original avatar's `m_iconIndex`, and stores only a transient `QIcon`; no converted file is created. `CMemberList` maps the original 40-pixel avatar image list and direct `IDB_MEMBER` five-image strip to Qt's icon/list modes, including the original status-priority order, and `ChangeAvatarEntry` uses source-named `UpdateMemberListIcon`. `CSayWnd::createSayBar` reads `IDB_SAY_BAR` in place and applies `MaskInColor` to source gray. `CChatView::CreateComicView` passes its document explicitly to `CPageView`, mechanically replacing MFC's create context; `353` and `366` ordering is unchanged. `original-irc-state` compares every displayed avatar-region pixel with its decoded original icon pose, `original-ui-structure` checks every Say glyph mask pixel against the direct BMP, and `original-mdi-join` renders a non-white source-built initial page in the first event cycle after `366`, without a click or later message. The application target and all tests build, 44/44 CTests pass, isolated offscreen startup remains alive through its intentional timeout, and `git diff --check` is clean. No runtime server, room, nickname, or message value is retained. |
| Initial room send | An authorized IRC reproduction reached `CSayWnd::bLegalToSend` with `CX_NOCHANNEL`. The server-confirmed self `JOIN` then ran `CIUserJoin` and changed the same room to `CX_INCHANNEL`; a subsequent send passed. The original `saywnd.cpp` rejects `CX_NOCHANNEL`, so an earlier invented state change would be incorrect. | The gate remains source-identical. `original-comic-send` verifies the exact `IDS_ILLEGAL_TO_SEND` rejection and unchanged input at `CX_NOCHANNEL`, followed by a successful send and cleared input at `CX_INCHANNEL`. `original-join-starring`, `original-irc-state`, and `original-mdi-join` retain the server-callback transition and initial query coverage. |
| Say-input crash | The debugger stack repeated `CSplitSay::keyPressEvent` and its Qt `ForwardToSayWnd` adapter until stack exhaustion surfaced inside accelerator resource parsing. Original `CSplitChat::OnChar`, `CSplitChatV::OnChar`, and `CSplitSay::OnChar` synchronously send a newly constructed `WM_CHAR` to `CSayCtrl`; they do not resend the propagating parent event. `CTabBarTabCtrl::OnChar` uses the separate original `ForwardToSayWnd(UINT)` path. | `spltchat.cpp` constructs one fresh synchronous Qt key event for `CSayCtrl`, consumes the source event, and restores focus in the original order. `tabbar.cpp` uses `ForwardToSayWnd(UINT)` and source-defined Tab focus cycling. `original-ui-structure` starts with an ignored parent event and verifies exactly one insertion through each path without recursion. |
| Missing unformatted comic text | Runtime sending produced a real `CBWoodringNormal` with no formatting array. Port `CBWoodringNormal::Draw` called only `DrawFormattedText`, whose source-backed guard returns for a null array. Original `CBalloon::DrawText` instead selects `DrawFormattedText` only for nonempty formatting and otherwise emits every wrapped line with `TextOut`; the function was absent from the port header and implementation. The original `CLabel::Draw` contains the same formatted/unformatted selection, which the port also omitted. | `CLabel::Draw` and restored `CBalloon::DrawText` select the original branch and line-height progression. `CBWoodringNormal::Draw` calls `DrawText`. `original-comic-core` renders the source `ID_STARRING` label and continuation string into otherwise white images and requires glyph ink without a contour or avatar. |
| Tab color and icon mask | Original `CTabBar` uses the standard `CTabCtrl` with `TCS_HOTTRACK`, fills its bar with `COLOR_3DFACE`, and creates the image list from `IDB_TABS` using `RGB(0,255,0)` as the transparency key. The Qt-only stylesheet added a blue selected-tab fill not present in this module, and `MaskOutColor` inverted the intended transparency. | The selected-tab stylesheet rule is removed, and the direct BMP mask uses `MaskInColor`. `original-ui-structure` compares every pixel of a rendered 16-by-16 icon with the corresponding pixels read directly from `IDB_TABS`: source-green is transparent and every other source color is opaque. No converted asset exists. |

| Area | Evidence and implementation |
| --- | --- |
| The 18 text-font roles | `chat.h`, `setupdlg.cpp`, `textview.cpp`, `whisprbx.cpp`, `saywnd.cpp`, `format.cpp`, `txtfntdg.cpp`, `proppage.cpp`, `defines.h`, and the build-selected TextCore establish the 60-byte binary format, separate 10/8 flags, zero-length semantics, role order, Say-font reset, and host-bold application order. This scope covers state, persistence, and TextCore effects; the complete font dialog is a separate unresolved UI block. |
| Rules/notification registry persistence | `rules.*` and `notif.*` store and load their versioned binary records under the original `RuleSets` and `Notifications` keys. `src/qt/originalsettings.*` replaces only shared HKCU access. `original-registry-persistence` covers root/value names, exact serialized bytes, flags, duplicate/version/full-consumption/cleanup boundaries, and missing keys using existing rule/default resources. Verification includes a full build, 43/43 CTests, and isolated offscreen startup through the intentional three-second timeout. |
| Application persistence in `setupdlg.*` | `CChatApp::LoadFromReg`/`SaveToReg`, exact HKCU root/value names, separate short/full saves, coolbar bytes, panel dimensions, flags, flood/rules packing, QFont/QRect boundaries, and `Macros/0..9` are connected. `protsupp.*` contains `ReplaceToken`/`bReplaceMacroTokens`; documents inherit the global member-list style. The directly ported `utils.cpp::MakeRectVisibleOnScreen` clamps main-frame geometry, and startup sets `IDS_DEFAULT_BACKDROP` before loading. `original-persistence`, a full build, 42/42 CTests, and isolated offscreen startup through the intentional three-second timeout pass. |
| Coolbar/toolbar module boundary | `CChatToolBar` is the main frame's single `CCoolBarEx` manager. `CCoolToolBarEx` bands, direct original resource buttons/BMPs, styles, the Favorites menu arrow, `IDR_TOOLBARCONTEXT`, visibility bits, and the original packed band-state format are implemented. Verification includes a full build, 41/41 CTests, and offscreen startup through the intentional three-second timeout. `setupdlg.*` persists the buffer under `ToolBarState`. |
| Canonical source inventory | The original tree, `chat.mak`, and `chat.rc` establish 313 files and 84 canonical object modules. |
| Evidence boundary | Every decision derives exclusively from original source, resource, and build code; other documents and external renderings do not define product behavior. |
| Fixed constants | Incorrect Qt-port `CX_*` and `SB_TOOLBAR_*` values were identified. The port uses one centralized, source-identical constants base. |
| Original constants implementation | `defines.h` is copied byte-for-byte; duplicates in `chat.h`, `chatprot.h`, `userinfo.h`, and `protsupp.cpp` are removed. `original-constants` covers toolbar, connection, message, channel-mode, and limit values. |
| Direct original-asset path | CMake requires a canonical `COMIC_CHAT_ORIGINAL_ROOT`; `originalassets.*` returns only existing canonical files from the four original directories and creates, copies, or converts nothing. `original-assets` covers all 109 files in those directories. |
| Protocol/avatar byte foundation from `protsupp.cpp`, `avatario.cpp`, and `avatar.h` | `IndexToByte`, `ByteToIndex`, `SM2BM`, and `BM2SM` retain their original names and priorities. `EmotionToBytes` encodes emotion and intensity through `IndexToByte`; all eight special gestures, freeze values `1/2/3`, and avatar flags are retained. Empty substitute `CAvatarX` objects are not created; without a loaded AVB, the avatar path remains empty. |
| Canonical path accounting | The overview names all 187 source/build files. Together with 109 asset files and 17 excluded documentation/helper files, they account for exactly all 313 original files. |
| `avbfile.h/.cpp`, `avatar.h/.cpp`, `dib.h`, and `backdrop.h` format evidence | Binding structures and order include one-byte packing, magic `0x81`/`0x8181`, Simple/Complex/Backdrop types, version 2, old size-less tags and sized tags from 256, offset correction, global/local/Mono/MaskedMono/DualMask palettes, DIB and zlib images, the 2 MiB limit, lazy pose loading, ditto offsets, neutral/emotion/index selection, and BMP/BGB backdrop branches. `QImage` is only the transient replacement for `CDIB`/GDI. |
| `dib.*`, `avbfile.*`, `avatar.*`, `avatario.*`, and `vector2d.*` | Original packed records, a QFile stream with original reference-count semantics, palettes, DIB/zlib, lazy poses, MaskedMono/DualMask, Simple/Complex records, neutral/emotion/index paths, and avatar index are ported. Qt-only `wincompat.h` contains only fixed Win32 type/bitmap layouts. No file is converted or copied; `QImage` is only the transient GDI replacement. |
| AVB/DIB production-data tests | `original-avb` directly reads all 45 AVB files from `comicart`, `artpack1`, and `archive`, indexes every real avatar, selects its original neutral body, and lazily loads its real icon. It also reads all 43 directly stored BMP/DIB/RLE files from `res` and `archive`. Several BMPs contain two bytes after the final padded scanline; they are accepted exactly as by the original `CAvatarDIB::Load`. Four CTests cover this boundary. |
| BGB/backdrop implementation from `backdrop.*` and `avbfile.cpp` | `CChatBackdrop::LoadBackdrop`, `LoadFromBmp`, and `Load` follow the original BMP, magic `0x8181`, type, version, tag, and offset branches. `original-avb` opens all nine BGB files from `comicart` and `artpack1`, preserves URL/copyright, and loads the real backdrop pose. The build and four CTests cover the bundled local assets. |
| `bodycam.h/.cpp`, avatar drawing, and `chat.rc` strings | Ported evidence includes `MAXBULL=159`, `MINBULL=93`, cursor/icon dimensions `5/20/26`, bullseye center and two-stage radius, eight angles, 20-percent neutral detent, `StringFromEmotion`, status updates only during MouseDown, temporary freeze, arrow-key snap tables, the Ctrl/Shift pixel branch, exact freeze toggle, and `<Chr>`. Character double-click has no substitute dialog until its `proppage.*` counterpart is implemented. `originalassets.*` replaces `CString::LoadString`/`CMenu::LoadMenu` by reading strings and menu entries directly from `chat.rc`. |
| `CBodySingle::DrawBody` / `CBodyDouble::DrawBody` | The original methods remain in `bodycam.cpp`. `GetBodyBox`, `FlipBodyBox`, bottom-center scaling, head/torso offsets, `TORSOFIRST`, `HEADMASK`, `TORSOMASK`, aura `MERGEPAINT`, and drawing `SRCAND` target a transient QImage buffer. `CDIB::Draw` emulates only the three source-backed GDI ROPs and negative destination widths; it creates no converted asset. `original-avb` renders all 45 real neutral poses and requires nonempty geometry and image pixels for each. Four CTests cover this path. |
| Avatar startup and enumeration glue | An empty Character field at application startup follows the `LoadFromReg` fallback through `GetNextAvatarName` and unsorted original AVB enumeration. `StartHistoryEntry::Execute` sets the title and avatar when starting a room; `SetMyAvatar`, `RefreshBodyCam`, and `AssignArbitraryAvatar` retain their original names. Other users receive avatars only from real `353` members; no local dummy member or starring name is created. The non-canonical placeholder `wmini.cpp` is excluded from CMake. |
| Comic geometry in `bbox.*`, `traj.*`, `spline.*`, `splinutl.cpp`, `arc.cpp`, `semantic.cpp`, and `pe.h` | Active original algorithms for bounding-box operations, lines/arcs, manual `100/100` dashing, cardinal/beta splines, matrix caches, Bezier splitting, flatness, nearest point, flattening, and horizontal walking are ported under original filenames. `QPainterPath` replaces only the GDI path. Notable source semantics such as `bbox_within_bbox`, the return value of `bbox_intersect`, and the cardinal cache key are preserved. `semantic.cpp` has no active production path in the canonical build: `AddSemantics` is empty and the experimental code is inside `#if 0`. |
| Source-value geometry tests and `format.cpp` | `original-format-geometry` covers special bounding-box semantics, original dash order, and the cardinal matrix. The actual format bits `0x0100..0x8000`, Control-Less/Full, color tables, range copy/insert/split/move, and `MarkHotLinks` are ported; the invented local format enumeration in `rtfctrl.h` is removed. RichEdit extraction, `IdentifyURLs`, and browser launch remain at their original module boundaries. |
| `balloon.h/.cpp` | Class structure, text measurement, line wrapping, format chunks, Woodring Normal/Whisper/Think/Box, spline/wave contours, tail/nimbus/bubble drawing, bounding boxes, route regions, docking, and `SplitHeight` follow the original source. The source-defined unformatted branches in `CLabel::Draw` and restored `CBalloon::DrawText` emit each line through the Qt text replacement for `CDC::TextOut`; formatted arrays still use `DrawFormattedText`. `QtPaintDC`/`QPainter` replace only `CDC`/GDI. Recursive-split URL-start transfer and the click action in `url.cpp` are separate unresolved boundaries. |
| `backdrop.*`, `fonts.cpp`, `panel.*`, `pageview.*`, and `chatdoc.*` | `CBackDrop` uses local original IDs, screen cache, original source-rectangle calculation, and directly loaded BMP/BGB files. `InitializeComicsFonts`, Normal/Whisper/Title/Shout, and twips/leading rules use values from `chat.rc`. `CPanel`/`CUnitPanelPage` implement Clone/Replace/FetchSpeaker, Talk-To/hysteresis order, avatar zoom, balloon collision, panel transition, Reaction, title, `ID_STARRING`, `AddStars`, and `AddStarsAux`. `CPageView` contains no substitute figures or balloons; it renders document-owned panels into a transient retained buffer and scrolls with original twips spacing. |
| Comic-core build/start verification | CMake builds from `v2.5-beta-1-qt/build`; five CTests cover the referenced scope. A five-second Qt offscreen run remains alive until the intentional timeout. This is startup/lifetime evidence, not layout evidence. |
| State-based `original-comic-core` test | The test uses a real unsorted enumerated AVB name and original lazy loader `GetAvatar3`. Without a member map, the title panel contains only the title and `ID_STARRING`; inserting that real user produces exactly its AVB icon and `CStarLabel`. `CUnitPanelPage::AddLine` creates a real body and `CBWoodringNormal`. Separate white-surface draws prove unformatted label and balloon glyph ink independently from borders, bodies, and backdrops. The fixture uses the original lazy index path. Six CTests cover this scope. |
| Startup/setup/status against `setupdlg.*`, `proppage.*`, `chat.rc`, `ircsock.*`, `protsupp.cpp`, and `chatdoc.*` | `CChatApp::InitVals` sets `IDS_DEFAULT_REALNAME`, `IDS_DEFAULT_CHANNEL`, `IDS_DEFAULT_NICK`, empty email/homepage, and greeting text formed from `IDS_DEFAULTGREETING` and variables. Global original getters/setters such as `GetMyName`, `GetMyNickName`, `GetMyUserName`, and `GetMyRealName` exist. `CChatDoc::SaveConnectStatus`/`ResetStatus` use `ID_DISCONNECTED`, `ID_CONNECTING`, `ID_NOCHANNEL`, `ID_CONNECTED`, and `ID_USER_SINGULAR/PLURAL` with original token replacement. |
| Connect property sheet | `IDD_SETUPDIALOG` has no port spin control in `chat.rc`. The Qt dialog mirrors Connect, Personal, Character, and Background pages. Favorites stays empty and disabled without the Windows Favorites path. Server, three `CA_*` radio buttons, and Channel follow `CSetupPage`; Personal follows `IDD_PERSONALPAGE_IRC`, including nick comma filtering, blank checks, and limits. Character/Background enumerate only real AVB/BMP/BGB assets and display them through `CBodyCam` or the original backdrop loader. Exact DLU geometry, Favorites/registry/service groups, RichEdit profile formatting, and additional option pages are unresolved. |
| Original connect/login order | `CIrcSocket::OnConnect` sends exactly `MODE ISIRCX\r\n` through `qpIsIrcX/ctModeIsIrcX` and starts the source-backed 50000 ms timeout. `HrModeIsIrcXFailure` removes the query before invoking `HrIrcLogin`, which sends optional `PASS`, virtual `ChatChangeNick`, and exactly `USER <space-free user> <hostname|g_szNoMachine> . :<realname>`. `CIrcProto::SendMessageText` is virtual as in the source. The two-stage 800 path has its source structure; security-package selection/SSPI is unresolved. |
| `original-irc-login` and `original-join-starring` | The login test uses the original virtual `SendMessageText` seam without a network and verifies `MODE ISIRCX` followed by byte-identical `NICK`/`USER` using resource defaults. The join test uses a resource nick/room and a genuinely enumerated AVB: self `JOIN` leaves the title panel at two elements, `353` creates self/member but no star, and only `366`/`ProcessEndEnumeration` creates the AVB icon and `CStarLabel`. Eight CTests cover these paths. |
| Runtime debugger boundary for delayed self `JOIN` | `ChatJoinAux` is reached synchronously from `OnLogin` while the first `handleReadyRead` is processing `001`; the outbound room and empty-password branch are correct. A second `readyRead` consumes the remaining MOTD block through `376` but contains no `JOIN`. No third signal follows. With the event loop stopped after that slot, `m_pending` is empty while the underlying `QAbstractSocket` holds 209 unread bytes; a non-consuming debugger `peek` shows the server-confirmed self `JOIN`, followed by `353` and `366`. Later inbound traffic merely causes those already buffered lines to be consumed. Original `CIrcSocket::OnReceive` explicitly rechecks its shared input buffer after each `ProcessMessage` because processing is reentrant. `handleReadyRead` therefore drains `readAll`, processes every complete line, and repeats while the same socket reports buffered bytes; it does not change join state optimistically. Once the lines reach the existing dispatcher, `bProcessAddChannel` selects the room document, updates its title, and transitions it to `CX_INCHANNEL`. `original-irc-parser` reproduces the measured state with a `QTcpSocket` test seam: the first source-defined `PONG` makes a second source-defined `PING` readable without another signal, and the same outer drain must emit both exact `PONG` replies. The application and all targets build, 43/43 CTests pass, and isolated offscreen startup remains alive through its intentional timeout. |
| Assisted end-to-end comic conversation | A clean authorized IRC run reached outbound `ChatJoinAux`, the server-confirmed self-`JOIN` branch, and `ProcessEndEnumeration` without unrelated user traffic. The room title and starring panel then contained only the real enumerated users. A source-focus character entered `CSayCtrl` once, Return passed through `SendSayFromReturn`, `bLegalToSend`, `ShowSay`, `SayEntry::Execute`, `CChatDoc::ProcessLine`, `CUnitPanelPage::AddLine`, and balloon drawing without a fault. An inbound `PRIVMSG` reached `OnTextMsg`, `ProcessSay`, `ShowSay`, `SayEntry::Execute`, `CChatDoc::ProcessLine`, `CUnitPanelPage::AddLine`, and `CBalloon::DrawText`. | The visible result matched the source state: complete text and punctuation were readable, the avatar came from the real starring member, unchanged panel composition reused the current panel, and the source-defined composition change created a new panel that later local speech reused. No server, room, nickname, or message content from this assisted run is retained as documentation or test data. |
| Sending/comic-annotation requirements from `protsupp.cpp:3008-3288`, `userinfo.cpp:85-111`, `ircproto.cpp:398-709`, `avatario.cpp`, and `format.cpp` | `G/E/R/M/T` is produced from `MyAvatar()->GetIndices/GetEmotions`, `EmotionToBytes`, `IndexToByte`, and at most five actually selected PUIs. Plain IRC receives `(#...) ` and IRCX receives `DATA <target> CCUDI1 :#...`. Think without comic data is sent as CTCP ACTION using `ID_THINK_PREFIX` with `%1` removed. Whisper uses only real addressees. Successful local echo enters `CChatDoc::AddLine` with original modes and avatar indices. Long byte strings follow the original 80-percent space/format boundary and server maximum length. Windows code-page behavior is not claimed or replaced by an invented rule outside the `intl.*`/JIS/SJIS boundary. |
| Send/receive core in original modules | `userinfo.*`/`memblst.*` provide `MListTalkTosToPuiself` and `GetSelectedPuis`; `protsupp.*` contains `g_rgpuiWhisperees`, `CRoomInfo::bSendWhispers`, `GetAddressees`, `GetWhisperedAddressees`, `bInsertAnnotations`, resource-identical `ProcessNonComicsMsg`, and `ShowSay`. No invented status line or whisper TODO is emitted. Received UDI invokes `SetIndices`, or `BytesToEmotion`/`SetEmotions` for `OTHERMAPPED`, before panel construction; speech count is stored on the avatar as in the source. `ircproto.*` quotes `0x10`, CR, and LF according to `ccommon.cpp`, accounts for the receiving nick/user/host prefix, uses `m_nMaxMsgLength`, splits through `nGetBreakingPoint`, continues formatting, and repeats IRCX DATA for each chunk. `ircsock.*` accepts a larger maximum from the first 800 reply. `original-comic-send` proves byte-identical IRCX `DATA`, plain-IRC `(#...)`, Talk-To, local Woodring panel echo, Think CTCP, external whisper without annotation, low-level quoting, and multiple chunks from real AVB state and source/resource text. Nine tests cover ASCII and the IRCX UTF-8 branch; non-ASCII Windows ACP/DBCS conversion remains at the `intl.c`/`ConvertEncodingOut` boundary. |
| `ircsock.cpp`/`query.*` dispatcher index | The original contains 29 named command branches (`AUTH` through `WHISPER`) and 96 numeric `RPL_*`/`ERR_*` case labels. Comic/room-critical acceptance covers self/other `JOIN` including ident length, `MODE`/324 through `ParseChannelMode`, `TOPIC`/331/332, `NICK`, `PART`, `QUIT/KILL`, `KICK`, `353/366`, `352/315`, query removal, and case-insensitive PUI lookup. Additional groups are LIST/LISTX, WHOIS, MOTD/LUSERS, BAN, IRCX PROP/ACCESS/EVENT, and errors. `query.*` preserves order and oldest-query search; `dtRule`/`dtNotif` reference counting and `PRUSERMATCH` belong to the rules/notification boundary. |
| Dispatcher and room state | On self JOIN, `ircsock.*` sets the source-calculated ident length and `SetMyIdent`; it binds other JOINs only to the matching room. It implements `ParseChannelMode` for `MODE`/324 (`p/s/i/t/n/m/l/k/q/o/v/f/y`), topic state and topic/mode query completion, WHO ident transfer and query completion, room-bound `NICK`, `PART`, `QUIT/KILL`, and `KICK`. `ProcessEndEnumeration` runs only for the matching initial 366 query. `protsupp.*` contains `CIUserPart`, `ProcessNick`, `ChatChangeAdmin`, `UpdateIgnoreOnEntry`, and `GotPartChannel`; PUI lookup is case-insensitive like the original no-case map. `memblst.*` removes departed users. `original-irc-state` uses only resource values, original constants, and real AVB names to cover query lifecycle, 324, 332, 353/352/315/366, host/voice, nick rekey, part, and self room departure. Ten tests cover this scope. User-mode branches, topic formatting, and remaining indexed commands/numerics remain explicit gaps where not covered by later entries. |
| Main layout source evidence | In comic mode, `chatview.cpp` creates exactly `CSplitChatV` with one row and two columns. Its left side is a mouse-locked `CFixedSplitter` containing `CPageView`/`CSayWnd`; its right side is `CSplitChat` containing `CMemberList`/`CBodyCam`. `spltchat.cpp` defines 80 percent left width (100 percent only in show mode), a 30/70 member-list/BodyCam split, and a DPI-scaled 23-pixel minimum Say height that remains fixed during resize. `tabbar.cpp` defines a 29-pixel bar, five-pixel top control offset, 16-pixel original icons from `IDB_TABS`, status icon 2, and room icon 0. Status occupies tab position 0; non-status tabs skip status documents and sort alphabetically. `mainfrm.cpp`, `chatbars.cpp`, and `chat.rc` define Main/Member/Text band order, button order, check groups, the complete `IDR_MAINFRAME` menu, and accelerators. |
| Resource-based main frame and UI structure | `originalassets.*` reads `MENU`, `TOOLBAR`, `ACCELERATORS`, bitmap/icon paths, and symbolic command IDs directly from `chat.rc`; it does not guess numeric values from the absent external MFC `afxres.h`. `mainfrm.*` builds all nine main menus and submenus, Main/Member/Text bands, status/tooltip text, 31 main accelerators, application title/icon, and the 75-pixel member-status pane from this data. Unimplemented commands are visible but disabled and emit no placeholder output. `spltchat.*`/`chatview.*` use `CSplitChatV`, `CSplitChat`, `CSplitSay`, and `CFixedSplitter` with 80/20, 30/70, the DPI-scaled 23-pixel Say minimum, and a locked left handle. `tabbar.*` directly uses `IDB_TABS`, 29/5/16-pixel metrics, resource font weight, status/room icons, and status-first sorting. `original-ui-structure` verifies source/resource-derived state offscreen. No unsupported `900x640` size or invented tab caption remains. |
| Say/input-window source evidence from `saywnd.h/.cpp`, `rtfctrl.*`, and `chat.rc` | Seven bit positions and commands occur in this order: Say, Think, Whisper, Action, Whisper Action, Sound, Whisper Sound. `IDB_SAY_BAR` is the unmodified 118x17x4 bitmap strip with 17x17 cells. Buttons occupy 24 pixels including border and `m_cxSayBar=count*24`. Resize places the edit at `0,0,cx-m_cxSayBar,cy` and the bar at `cx-m_cxSayBar-6,-3,m_cxSayBar+12,29` (Whisper uses y=-2 and a taller variant). `MAX_INPUTLEN=350`; leading whitespace is discarded, Ctrl+L clears, PageUp/Down targets output, and Tab invokes `CycleFocus`. In an empty comic input path, Enter sends `<Chr>` only when `GetSendComicsData()` and the original two-second `bCanDance()` permit it. Status, connection, and empty-input messages use only `IDS_*` resources. Sound remains disabled rather than simulated until `sounddlg.*` is ported. |
| Say/focus implementation | `saywnd.*` contains `CSayToolBar`, `CSayCtrl`, and `CSayWnd`, all seven original flags, direct cells from unmodified `IDB_SAY_BAR`, source-identical 24-pixel slots, and absolute resize coordinates. `MAX_INPUTLEN`, leading whitespace, Ctrl+L, PageUp/Down, Tab/Shift-Tab focus, resource-based send-permission messages, clearing input before send, and the empty `<Chr>` dance path follow the source. `chatdoc.*`, `mainfrm.*`, `tabbar.*`, `memblst.*`, and `rtfctrl.*` provide the source-backed focus order. The Sound button remains visible but disabled without `sounddlg.*`. `original-ui-structure` also covers button order, IDs, disabled Sound, `m_cntBalloons`, `m_cxSayBar`, and both resize variants. A full build and eleven CTests cover this boundary. |
| RichEdit/text-output evidence from `rtfctrl.*`, `doskey.*`, `textcore.*`, `textview.*`, `status.*`, `artifacts/inc/{textview.h,msgtype.h,textview.rc}`, and `artifacts-modern/core/textview.cpp` | Original `textcore.cpp` intentionally includes the shared 2,557-line TextView core. Binding behavior includes its 256,000-character client buffer, 90-percent warning/20-percent line cut, selection/autoscroll preservation, header/text separation, spacing `never/different/always`, indent `lIndent+72`, 19 `MSG_TYPE` and four `MEMBER_STATUS` states, resource tokens `%1..%3`, exact default colors/effects, eight highlight formats, and format DWORDs per text range. `CTextView::TextLine` suppresses only `<Chr>` pose lines, maps `BM_SAY/THINK/WHISPER/EXCHAN/ACTION/NOFORMAT` to these core methods, and uses actual addressees/member roles. `CStatusView` sets blank spacing to zero, exposes only Copy/Clear in its context menu, and disables comic/text switching. `CDosKey` is a 64-entry ring with separate main/whisper instances and copied format arrays. `CSayWnd::SetFont` sets input height to `-DpiScale(-nFontHeight)` with `nFontHeight=-14`; comic twips height `240` is not a widget-pixel size. Visible headers come directly from `textview.rc`. URL recognition/launch remains empty outside the separate `url.*`/`urlfind.cpp` source boundary. |
| TextCore resource boundary | `src/original/textcore.*` and `msgtype.h` are part of the CMake build. The core reads every header/status template directly from `artifacts/inc/textview.rc`, selected by original `chat.mak`; there is no Qt substitute text. `CTextView`, `CTextEdit`, and `CStatusView` are bound under their original filenames, and non-comic `ShowSay` reaches `CTextView::TextLine`. |
| TextView/status integration and receive path | `src/original/textview.*` and `status.*` replace generic widgets in `CChatView`. `CChatDoc` stores separate typed comic/text-view pointers and returns the actual edit control for `CHATFOCUS_TEXTVIEW`. `CTextView::TextLine`, Join/Part/Nick/Info, focus forwarding, direct `IDR_VIEWCONTEXT`/`IDR_STATUSVIEW` menus, 10-point TextCore default, host bold, and `CStatusView` blank spacing follow original functions. In text mode, `ShowSay` and `ProcessSay` reach `TextLine`; incoming Control-Full formatting is split into identical DWORD ranges through `SzControlLess` before display. In comic mode both paths use restored original names `CChatDoc::ProcessLine`/`TallySpeech`. `original-text-view` covers the 256,000-character buffer, direct resource headers, `<Chr>` suppression, local Think echo, incoming bold range, actual view classes, and status spacing. URL, `CIrcPrint`, and uncovered printing paths remain at their original boundaries. |
| `textpose.cpp` and RC-rule evidence | The canonical original initializes eleven rules in fixed order: `SHOUT`, `LAUGH`, `HAPPY`, `SAD`, `POINTOTHER`, `POINTSELF`, `WAVE`, `COY`, `ANGRY`, `SCARED`, `BORED`, directly from `ID_RULE_*` in `chat.rc`. The final three resources decode exactly to `""`, which contains no registerable rule. Binding functions are `CheckForUppers` (no lowercase and more than one uppercase), word-bounded `CheckWord`, `. ! ?` sentence separators, bytewise `ToLower`, parser `Function("arg");Strength`, exactly seven registered function names, separate General/Word/Sentence lists, and priority transfer to `CEmotionOpts::Add`. `ChatPreSendText` acts only with a comic document and an unfrozen real avatar, then invokes `GetBodyFromEmotion`/`UpdateBody`; `CChatDoc::ProcessLine` invokes it only for uncooked text. The notable sentence-loop branch tests `buff/lower` again despite calculated `bptr/lptr`; that control flow is not creatively corrected. Rules are read unmodified from `chat.rc`, including RC doubled-quote escaping. Non-ASCII ACP/DBCS remains at the `intl.*` boundary. |
| `textpose.cpp` implementation | `src/original/textpose.cpp` preserves original function names, order, lists, priorities, and the sentence-loop branch. Only `CString`/`CPtrList`/`LoadString` are replaced by `QString`/`QList` and the direct `chat.rc` reader. `originalassets.cpp` understands Microsoft RC doubled-quote escaping, so no rule is copied or reformatted. `CChatApp::run` initializes rules after `LoadEmotionStrings` and frees them after the event loop; `CChatDoc::ProcessLine` invokes `ChatPreSendText` only when `cooked == FALSE`. `original-textpose` extracts all text from the eleven original resources and covers caps, General, Word, and Start rules with original priorities and cooked/uncooked panel bodies. A full build and thirteen CTests cover this boundary. No Qt-specific rule is invented for non-ASCII ANSI/ACP handling. |
| Build/compatibility and excluded small modules | Completely inspected: `stdafx.cpp/.h`, `defines.h`, `dpiscale.h`, `pe.h`, `safectype.h`, `ui.h`, `helpids.h`, `chatver.h/.rc`, `cchat.rcv`, `res/chat.rc2`, wrappers `ccommon.cpp`, `ccomp.cpp`, `dlylddll.c`, `textcore.cpp`, `urlutil.cpp`, `jis2sjis.cpp`, `sjis2jis.cpp`, `avatario.h`, `base/makefile`, `base/icbcore.idl`, `base/imsconf2.idl`, and unbuilt `bothdlg.*`, `cllist.cpp`, `dumbwnd.*`, `guids.cpp`, `script.*`, `semantic.cpp`, `url.cpp/.h`. The Modern build assumes `_MBCS`, fixed limits/flags, a global 256-color palette, and 96-DPI no-op behavior; Qt replaces those effects only at original module boundaries. The seven wrappers contain no domain logic and only include external core files, so their implementation cannot be inferred from wrapper text. `bothdlg` is commented out even in its header; `dumbwnd` is a red test window; `script` is an incomplete state scaffold; semantic text hacks and panel calls are disabled; `url.cpp` is superseded by the active URL implementation in `format.cpp`. These excluded modules are not reactivated for visible Qt behavior. `base/icbcore.idl`/`imsconf2.idl` reference unavailable NetMeeting IDLs and remain outside the build with the excluded `CB32SUPPORT` path. |
| OLE/DocObject/Automation | Completely inspected: `binddcmt.cpp`, `binddoc.cpp/.h`, `bindipfw.cpp/.h`, `binditem.cpp/.h`, `bindtarg.cpp`, `bindview.cpp`, `bindauto.cpp`, `chatitem.cpp/.h`, `ipframe.cpp/.h`, `mfcbind.cpp/.h`, `oleobjct.cpp`, `base/icchat.idl`, generated `icchat.h`, `icbcore.h`, `imsconf2.h`, and `mschat.h`. `CDocObjectServerDoc` provides exactly one embedded view, menu merging, SaveAs/Print OLECMD, IPrint, and `ICChatAutomation`. Its 67 automation methods contain no second domain implementation: except `ShowMenu`/`HideMenu`, they forward exactly to existing `WM_COMMAND`/`ID_*` commands. Those visible commands therefore belong in `mainfrm`, the document, and original domain modules; no Qt automation protocol may be invented. `CChatItem::OnGetExtent` calls its 3000x3000 HIMETRIC size arbitrary/TODO and `OnDraw` draws nothing, so it defines no Qt comic size. `ipframe` duplicates toolbar, status, help, and palette realization for embedded containers, not the normal layout. Generated `icbcore.h`/`imsconf2.h` define NetMeeting/CB32 interfaces; the Modern build excludes `nmproto.cpp`/CB32. `mschat.h` contains only context-help IDs. COM embedding, registry registration, and container menu merging have no direct Qt/Linux equivalent; underlying visible commands remain normal port requirements. |
| Rules, automation, and logon notifications | Completely inspected: `rules.cpp/.h`, `actions.cpp/.h`, `autopage.cpp/.h`, `notif.cpp/.h`, `notipage.cpp/.h`, associated dialog templates, and `DLGINIT` data in `chat.rc`. Evidence includes eleven events, thirty actions, at most three event/action parameters, RuleSet format version 1 (`.crs`), registry branch `\\RuleSets`, default flood limit of 12 executions in four seconds, rules timer 82 (12 then 150 seconds), delayed timer 84 (one second), greeting, exactly ten Alt+0..9 macros, Auto-Ignore ranges 1..255/1..255, rule-set/rule management, active/stopped icons, match-case/word, and action delay. Notifications have four identity fields (Nick/User/Host/Network), Any/Equals/Contains/StartsWith/EndsWith operators, binary registry format, timer 83 (0/10/150 seconds), WHO-based updates, and a separate sortable/resizable user window with Invite/Whisper/Join/Update/Clear. `CMacro::Invoke` splits original Control-Full text by lines, expands only source-backed variables, and passes it through `ChatPreSendText` and `bChatSendText`; it creates no sample macro. Source quirks remain documented: duplicate `m_strShortDesc` in `rules.h`, two consecutive frees in `rules.cpp::bReplaceMessage`, duplicate network comparison in `CCNotif::operator==`, and function-local declaration `BOOL bCanInvite();` at the start of `CNotificationUsers::UpdateButtons`. Observable semantics and required memory safety are separated and regression-tested. |
| Conversation history, document, and application shell | Completely inspected: `histent.cpp/.h`, `chatdoc.cpp/.h`, `chat.cpp/.h`, and `print.cpp`. The conversation format starts with `#CHATCONVERSATION` and includes `SayEntry`, `JoinEntry`, `PartEntry`, `ChangeAvatarEntry`, `GetInfoEntry`, `ComicCharacterEntry`, `StartHistoryEntry`, `ChangeBackDropEntry`, and `NickEntry`, with separate `HM_LIVE=1`, `HM_RELOAD=2`, and `HM_LOAD=4` execution. Say entries store complete `CUserDisplayInfo`, format ranges, and `G/E/R/M/T`; loading returns through `AddAndExecute`, never a substitute Qt chat. `.ccr` is a locator, `.ccc` a conversation, and `.rtf` text output. `CChatDoc::CycleFocus` follows Tabbar, Comic, Text, Input, Member List, Emotion and filters by view type. `CChatApp` initializes protocol, fonts, text-pose rules, backdrops, registry, rules, OLE, MDI document template, status document, and Favorites watcher. The Modern source deliberately returns before old WinInet avatar/backdrop downloads. `print.cpp` implements no print path and returns `E_NOTIMPL` from all `IPrint` methods. The source Easter egg is reachable only through its exact original trigger, never as startup, test, or dummy conversation. |
| Internationalization and platform helpers | Completely inspected: `intl.c/.h`, `mcithrd.cpp/.h`, `webreq.cpp/.h`, `filesend.cpp/.h`, and `utils.cpp/.h`. The active INTL core treats only Far East code pages 932/949/950/936 specially and defines byte-identical forward/backward movement, trail-byte handling, punctuation correction, text measurement, fit search, word width, and charset fallback; the large `#if 0` MIME/browser block is inactive. MCI is a self-terminating lockable request thread for sequencer Stop/Play/Loop. WebReq is a WinInet queue with seven states, temporary-file callbacks, up to 16 workers, a 2048-byte buffer, and optional size limit. FileSend is the original CTCP DCC SEND path with starting port 7011, 120-second accept and 60-second receive timeouts, 1024-byte send chunks, 8192-byte receive chunks, and 32-bit network ACKs; `IDD_FILE_TRANSFER` defines visible layout. `utils.*` includes lists, user-bound encode/decode data, file enumeration, `StrFindSubString`, MIDI, Browse Folder extension, dialog resizing, and monitor clamping. Source bugs such as `CObjectPtr` counting/indexing, `StrFindSubString` ignore-case lifetime, the MCI wait condition, and WebReq/socket cleanup edges do not define new semantics; safe ports require isolated regression boundaries. |
| Administration, channel dialogs, MOTD, and MDI child | Completely inspected: `admindlg.cpp/.h`, `chanprop.cpp/.h`, `motd.cpp/.h`, `chicdial.cpp/.h`, `childfrm.cpp/.h`, their callers in `protsupp.cpp`, and `IDD_KICK`, `IDD_BAN`, `IDD_INVITE`, `IDD_INVITATION`, `IDD_CHANNELPROP`, `IDD_CHANNELCREATE`, `IDD_MOTD`, `IDD_AWAYDLG` in `chat.rc`. Kick/Ban/Invite/Invitation, topic/mode changes, and room creation connect only through source-backed original functions. Auditorium/NoWhispers remain inactive as their original controls are hidden. `CChildFrame` binds document activation, menu, status, tab, and stored window state; uncovered multi-room details remain an explicit gap. |
| Room/user lists, whisper, sound, and text-font dialog | Completely inspected: `roomlist.cpp/.h`, `userlist.cpp/.h`, `rtfcmb.cpp/.h`, `whisprbx.cpp/.h`, `sounddlg.cpp/.h`, `txtfntdg.cpp/.h`, and their dialog resources. Room/User lists have persistent server caches, growth by 2000, one-shot F5 refresh, source-fixed filtering/sorting, and Join/Invite/Whisper calls. Whisper Box is a separate modeless tab window with a real `CSayWnd` per active conversation; tabs arise only from real user/whisper events. Sound files come only from the original semicolon-separated search path and WAV/MID/RMI types. The font dialog has 19 message-type entries and a resource-only preview. Each implementation remains at these original module boundaries. |
| Server/connection management | `chatsrv.cpp/.h` is completely inspected. Evidence covers registry model, service/group/server names, authentication modes, encrypted password records, migration, transactional configuration dialogs, DNS resolution, up to five parallel sockets, and the 50 ms connection timer. Qt may replace only registry, resolver, and socket mechanics; selection order, retry state, and handoff to `serverConn.OnConnect` remain source-identical. Documented source quirks in the server detail index do not become new semantics. |
| Exclusions and build authority | Completely inspected: `cache.cpp`, `nmproto.cpp/.h`, `urlfind.cpp`, `wmini.cpp`, `chatprot.h`, `chat.mak`, `base/sources`, and `resource.h`. `cache.cpp` and `wmini.cpp` are unfinished unbuilt drafts; `nmproto.*` belongs only to excluded `CB32SUPPORT`/NetMeeting; `urlfind.cpp` is the obsolete unbuilt RichEdit/browser implementation. None is activated or used to invent visible behavior. `chat.mak` and `base/sources` define canonical module scope; numerically overlapping IDs in `resource.h` are interpreted only within their resource type. |
| Complete `chat.rc` audit | All 3,021 lines are inspected: direct icon/bitmap/DIB references, three toolbars, main/in-place/context menus, four accelerator tables, 35 dialog templates, DesignInfo, Automation `DLGINIT`, and every string table. Resource sample values, default rules, default names/server/room, Easter-egg text, and help text apply only in paths proven by original code; their presence does not authorize Qt demo or dummy initialization. Numeric values for externally included MFC standard resources are not guessed. |
| Build-selected shared cores | Completely inspected: wrappers `ccommon.cpp`, `ccomp.cpp`, `jis2sjis.cpp`, `sjis2jis.cpp`, `urlutil.cpp`; selected files `artifacts/core/{ccommon,ccomp,jis2sjis,sjis2jis}.cpp`, `artifacts-modern/core/urlutil.cpp`; and `artifacts/inc/{ccommon,ccomp,urlutil}.h`. Evidence covers IRCX UTF-8/escape conversion, low-level quoting, charset conversion, user-mask comparison, stateful JIS/Shift-JIS conversion, and active URL prefix/suffix/canonicalize/launch paths. Qt may replace only WinNLS/WinInet/ShellExecute; tables, boundaries, ownership, and control flow remain authoritative. |
| Delay-load shared core | Completely inspected: one-line wrapper `dlylddll.c`, selected `artifacts/core/dlylddll.c`, and `artifacts/inc/dlylddll.h`. The core contains only cached `LoadLibrary`/`GetProcAddress` thunks for eight MSRATING calls, ANSI/Unicode `InternetCanonicalizeUrl`, and three MSCONF calls with global DLL handles. It contains no alternative rating, URL, or NetMeeting domain logic. Qt replaces only concrete platform capability; absent Content Advisor/NetMeeting services remain disabled blockers and are not simulated as success. |
| Complete original-path coverage | The category/build index covers every one of the 187 source/build files, all completely inspected. The direct-asset ledger names all 109 paths in `res`, `comicart`, and `artpack1` (108 binary assets plus `res/chat.rc2`) and their original parser/direct-load verification. All 17 auxiliary paths are inspected or, for `cchat.hlp`/`readme.gif`, classified by format/hash without rendering. Debug IRC captures, the octal-encoded `strings.txt` dump, distribution readmes, Help, and GIF files are explicitly not product-behavior sources. Implementation uses only original functions recorded in this plan and no documentation sample data. |
| Conversation history/replay architecture | `src/original/histent.*` is the sole execution and replay layer: all nine original types, `HM_LIVE/HM_RELOAD/HM_LOAD`, `G/E/R/M/T`, Control-Full format ranges, Join/Part/Nick, avatar/backdrop changes, and info entries retain their original class names. `CChatDoc` owns the document history list, `ExecuteHistory`, `DestroyHistory`, structurally identical `#CHATCONVERSATION` records, `ShowInfo`, and source-identical retention of the final avatar entry for each active participant during clear. `bSingleJoin`, incoming `ProcessSay`, local `ShowSay`, and IRC Nick/Part/Quit/Kick use `AddAndExecute`; `CPageView::SetPanelsWide` rebuilds panels through `HM_RELOAD`. `IdentifyURLs` remains bound to the shared URL core and AutoGreet/Away to their own original functions. `.ccc` stream byte encoding remains an explicit caller boundary outside `intl.c`, not silently fixed to UTF-8. |
| Conversation history/replay verification | A full build includes all nine entry classes. `CChatDoc::InitMyDocument` restores the original precondition that a comic document has a real title page before a join; `InitHistory` creates only Start/Backdrop entries and no participant. Comic/text view changes and `SetPanelsWide` reconstruct output through `ExecuteHistory(HM_RELOAD)` and repopulate the member list only from existing non-departed PUIs. `original-history` uses only `chat.rc` defaults, directly enumerated AVB names, real `JOIN/353/366` parser lines, and original invisible control word `<Chr>` to cover entry order, no PUI duplication on replay, starring reconstruction, and structural `#CHATCONVERSATION` records. URL marking, stream ACP/DBCS encoding, unknown-field dialog reporting, AutoGreet, and Away remain assigned to their original modules rather than local replacements. |
| Coolbar and toolbars | `chatbars.cpp/.h` and `coolbar.cpp/.h` are completely inspected. The three resource toolbars are rebar bands with independent visibility, order, width, and newline bit. Their packed persistence record is `WORD id`, `WORD length`, `BYTE flags` plus a null sentinel. Common Controls 4.70 physically removes hidden bands; 4.71 uses `RB_SHOWBAND`. Command UI, check-group/dropdown styles, whole/individual switching, context menu, and status-prompt ownership are source-backed. The port implements bands, record format, visibility, styles, and context menu; `LoadFromReg`/`SaveToReg` provide persistence. |
| Setup, options, and persistence | `setupdlg.cpp/.h` and `proppage.cpp/.h` are completely inspected. Coverage includes application defaults, every registry field, macro records, short/full save, `.ccr` locator save/load, DBCS filter edit, nick/channel/password dialogs, Connect/Favorites, Settings, Personal/Profile RTF, Character/BodyCam, backdrop preview, text fonts, comic fonts/panels, and Servers property page. Pages commit only through original `OnOK`/Apply paths; real AVB/BMP/BGB assets are directly enumerated and never converted. Documented locator/filter/fallback source defects do not authorize new Qt semantics. |
| IRC parser, query, user/member state, and sending | `query.cpp/.h`, `userinfo.cpp/.h`, `memblst.cpp/.h`, `ircproto.cpp/.h`, and `ircsock.cpp/.h` are completely inspected. Coverage includes query ownership/order, every user flag/request bit, member-list status/sorting, exact send bytes/chunk limits, IdentD, sorted command table, parser offsets, login/IRCX/SSPI, all command branches, and handled result/error codes. Join remains server-driven: self `JOIN` establishes room and initial queries; only `353` creates real `JoinEntry` users; `366` removes the NAMES query. |
| IRC/comic glue | All 5,260 lines of `protsupp.cpp` and `protsupp.h` are completely inspected. Coverage includes user/member lifecycle, Appears-As/Profile/Backdrop comments, UDI and plain-IRC annotations, CTCP/Action/Sound/Away/DCC/Info, Ignore/Flood/Rules, slash parser, comic/whisper sending, room/user-list bridge, room switching, `ProcessEndEnumeration`, rating, encoding, and connect/reconnect. `ProcessEndEnumeration` calls `UpdateTitle` and member sorting only after real enumeration. The explicitly source-backed `OfflineEditInits` path creates a local self join solely for offline comic editing; it remains separate from online JOIN/NAMES/starring and is never network content or test-data source. |
| History/comment protocol | A full CMake build and fourteen CTests cover the boundary. Under its original name in `protsupp.cpp`, `ProcessComment` handles source-backed annotations `# Appears as`, `# GetInfo`, `# HeresInfo:`, `# GetCharInfo`, `# BDrop:`, and `# BDrop2:`. Responses use original request credits, `ChatGetInfo`, `ChatGetAvatarInfo`, `ChatSyncBackDrop`, and history entries. Low-level unquote accepts only quote+`n`, quote+`r`, and doubled quote, as in the shared core; a wholly invalid sequence remains unchanged. Backdrop changes are accepted only from non-ignored operators. The Modern source disables downloads, so neither a downloader nor substitute asset is created. |
| Ignore/Flood/Away/AutoGreet | `CUserInfo` owns `m_uIntervalStart`, `m_uMsgCount`, and `IsFlooding`; it evaluates original defaults `FLOOD_IGNORE`, count 8, and interval 8 in a 16-bit time window, excluding self. `AddIgnore`, `RemoveIgnore`, `IsIgnored`, `IgnoreUser`, `CIrcProto::DoIgnoreUser`, WHOIS query path 311/318, and `UpdateIgnoreOnEntry` use identity data from the parser and three original feedback resources. `g_docs` is document-wide as in `chatdoc.cpp`. `ExpandVariables` and `AutoGreet` replace only resource variables for the actual PUI/room and send through `bChatSendText`, retaining comic UDI. `CRoomInfo::ChatSetAway`, `CIrcProto::ChatSetAway`, `ShowAway`, and `DoUserAway` send/display only original CTCP-AWAY and resort actual member state. `JoinEntry::Execute` invokes these in the source self/other branch. `original-presence-protocol` uses only resource values and real AVB names to cover greeting, flood threshold, ignore/unignore, WHOIS identity, and Away/Back. A full build and fifteen CTests cover this path. |
| `ProcessSay` priority | Original symbols `actionID`, `soundID`, `versionID`, `pingID`, `timeID`, `emailID`, `urlID`, `netMeetingID`, `awayID`, `clientInfoID`, `fileDCCID`, `xvchatID`, and their lengths are in `ircproto.h`. `ProcessSay` performs low-level unquote, plain UDI, Action/Comic Action/Sound, VERSION, PING, TIME, DCC, EMAIL, URL, NETMEET, AWAY, CLIENTINFO, X-VCHAT, old `\x01*` replies, and unknown-CTCP suppression in source order; Ignore/Flood is counted at the same decision points. `Reply*`, `ChatGet*`, `Show*`, request credits, `IdentifyWhispers`, and `AcceptWhispers` retain original names. `GetVersionString` uses `ID_SIMPLE_VERSION` and unmodified `chatver.h`. `<Chr>` reaches History/Panel and creates the original reaction; only `textview.cpp` hides it. The presence test covers VERSION/PING/CLIENTINFO wire bytes and `<Chr>` using payloads produced by original functions. Unimplemented `filesend.*`, `mcithrd.*` sound playback, excluded CB32/NetMeeting, and other unavailable effects remain suppressed without substitutes. |
| `KICK` and `ParseChannelMode` | For a matching room, `ircsock.cpp` calls original glue symbol `OnKick`. It loads only `ID_KICK_MESG`/`ID_KICK_NO_MESG`, replaces `%1..%3` with actual kicker/kickee data, converts Control-Full through `SzControlLess`, and for a known kicker sets all six UDI indices to 0, Cooked 0, Requested 1, `BM_ACTION`, and exactly the kickee as Talk-To. It inserts `SayEntry` before `PartEntry`; for self kick it then runs `GotPartChannel` and the original dialog. An external/unknown kicker creates no action panel. `original-irc-state` covers resource text, entry order, and UDI without invented messages. Original mode parsing intentionally does not consume one argument per flag; it passes only `szArg2`/`szArg3` to every flag, and the port preserves that observable behavior. `UpdateSpectators` updates real list entries from operator/voice on each `+m`/`-m`. IRCX `+f` sets `CM_NOFORMAT`, `m_bSaveViewMode=FALSE`, and calls `CChatDoc::OnViewText`; `-f` does not switch back. `+y` to `FixMICChannelName` remains at the original encoding boundary without a QString substitute. |
| Original `ParseIt`/command-table and receive-loop foundation | `ircsock.h/.cpp` contains `MAXARGS=10`, all 47 `g_rgIrcCmd` entries in original order with length/flags/minimum arguments, `enumCmdId`, `NGetCmd`, `IRCPARSE::bHasPrefix/nArgs/nOffsets/lastString/uCode`, and global symbol `ParseIt`. It parses UTF-8 bytes rather than QString character positions, splits `nick!user@machine`, treats channel prefixes and bare server prefixes as `nick`, recognizes trailing `:`, retains both double quotes in slash mode, limits tokens to `MAX_TOKEN-1`, and after argument ten puts the unconsumed remainder including leading separator in `lastString`. A missing prefix body safely yields empty state instead of copying the source assertion/null access. `original-irc-parser` uses only original commands, default resources, and source fallback `NoMachine` to cover prefix fields, byte offsets, numerics, trailing text, quotes, ten-argument boundary, case-insensitive binary search, and repeated draining when protocol processing makes additional socket bytes readable without another `readyRead`. |
| Slash-command source boundary | `protsupp.cpp`, `ircproto.cpp/.h`, `ircsock.cpp/.h`, `chatdoc.cpp/.h`, `chat.rc`, and `resource.h` define shared `ParseIt(..., TRUE)`, `NGetCmd`, syntax from `g_rgSyntax`/`IDS_AT_*`, Generic/RAW/QUOTE, MODE, PROP, NICK, AWAY, and ME/THINK outside Whisper Box. `/JOIN` and `/CREATE` must pass through `bSwitchToRoom` and MDI document management; `/PART` closes the document through `OnLeave`; external `/MSG`, Whisper-Box ME/THINK, and SOUND require `whisprbx.*` or `mcithrd.*`. Missing modules are never simulated with a single-document session, substitute dialog, dummy PUI, or invented audio. Outside `intl.*`/ACP/JIS, `StrEncodeCommandParam` may pass Unicode losslessly only while retaining original channel/nickname decisions, quote trimming, and encoding mode. Acceptance requires exact wire strings/state changes, no new slash syntax, and explicit MDI/Whisper/audio gaps. |
| Shared slash dispatcher and original encoding names | `chatdoc.*` provides document-wide case-insensitive `LookupDoc`; `ircproto.*` provides `bExtendedNickname`, `EncodeNick`, `DecodeNick`, `EncodeChan`, `DecodeChan`, and `StrEncodeCommandParam`. Source-backed IRCX prefix/escape branches (`'`, `%#`/`%&`, `\\n`, `\\r`, `\\t`, `\\b`, `\\c`, `\\\\`), quote trimming, channel-vs-nickname selection, and `ENC_DBCS`/`ENC_UTF8` selection are restored. ACP/DBCS/JIS byte conversion remains at `intl.*`; Qt passes Unicode losslessly to the socket boundary and claims no invented legacy code page. `protsupp.cpp` implements `CRoomInfo::SlashRaw`/`ProcessSlashCommand`, `CIrcProto::ProcessSlashCommand`, `GetSyntaxFromCmdId`, `StrSyntaxMessage`, `SlashGeneric`, `SlashMode`, `SlashProp`, `SlashNick`, `SlashAway`, ME/THINK outside Whisper Box, and internal `/MSG` targets resolved to actual users. `bChatSendText` dispatches `/` to this path. RAW/QUOTE reject JOIN/CREATE with arguments; MODE set uses `bRegisterMode` and MODE read sends directly. The original double space in user MODE remains. Unsupported module-dependent commands send no simplified substitutes. `original-slash-command` uses original commands and purpose-specific default/Away resources to cover exact NAMES, INVITE, ISON, KICK without invented reason, channel/user MODE, PROP get/set, NICK, RAW, AWAY/Back, syntax resources, and channel encoding. |

| Document-owned nick/user maps | Original state is not one process-wide nick map: each `CChatDoc` owns `m_mapNickToPtr`, while `g_mapNickToPtr` is only an alias switched to the active document by `CChatDoc::LoadDocData`. The Qt port preserves this ownership. `LookupPui`, `PuiFromDocNickIdent`, `CIUserJoin`, `ProcessNick`, `ReinstallPui`, `bProcessAddChannel`, and `CPage::AddStarsAux` access the affected document map; `SetChatDoc`/`LoadDocData` only switch the active alias. Identical or different nicks in multiple rooms therefore cannot modify the wrong room state or comic panel. A full CMake build and seventeen CTests cover this foundation. |
| Incoming multi-room routing and external PUI search | `ircsock.cpp` routes `JOIN`, `MODE`/324, `TOPIC`/332, `353`, `366`, `DATA`, `PRIVMSG`/`NOTICE`, `NICK`, `KICK`, `PART`, `QUIT`/`KILL`, `PROP`, and `WHISPER` through original `LookupDoc`/`g_docs`, not the active room. `PuiFromDocNickIdent` searches the explicit document, active document, and other visible in-channel documents before creating a real global External PUI. Private `# Appears as` messages enter only rooms where the sender is actually a member. Topic Control-Full formatting remains in `m_prgdwTopicFormatting`; `366` invokes `ProcessEndEnumeration` only for the addressed room, so starring still follows real `353` data. `original-multiroom-routing` uses only resource values and directly enumerated original AVB names to cover room-specific JOIN, MODE, TOPIC, private message, Appears-As, NICK, and QUIT. A full build and eighteen CTests cover this path. |
| IRCX CLIENT/property functions | `ChangeKeyString`, `GetValueFromKeyString`, `EnumKeyString`, `CIrcProto::ChatSetClientData`, `HandleClientDataChange`, `ChangeProperty`, `CRoomInfo::OnPropertyChange`, and `PROP` command handling follow original implementations. The deletion branch of `ChangeKeyString` returns `TRUE` but does not write its changed local intermediate string back, preserving the observable source quirk. The `bk` tokenizer ends backdrop names only at comma or whitespace, as `GetToken(..., ",")` does, retaining a real `.bgb` extension; the URL token follows `GetToken2(..., ",", ",)")`. Result codes 818/819 and global `serverConn` ownership are included in the source boundary. |
| Global IRC connection ownership | `ircproto.cpp`, `ircsock.cpp`, `chatdoc.cpp`, `ui.h`, and `protsupp.cpp` establish exactly one global `CIrcSocket serverConn` owning socket buffers, maximum length, and query list. `NewDefaultProto(CChatDoc*)` creates a `CIrcProto` for a document but always assigns `m_pSock = &serverConn`. The Qt port uses the same single `serverConn`; `CIrcProto` does not destroy it and `NewDefaultProto` retains its original name. Login/connection callbacks bind to the connection protocol, while incoming room events route exclusively through `LookupDoc`. |
| IRCX CLIENT/property verification | Key-string functions follow grammar `key=value;...`, quote values containing semicolon/equal signs, strip outer quotes during reads, and preserve the deletion quirk. `ChangeProperty` succeeds only for an IRCX room owner, queues `qpSetClient/ctPropSet`, and sends exactly `PROP <channel> CLIENT :<data>`. A matching server echo removes the query without a second change because `m_strClientData` already contains the local update. A different `PROP CLIENT` creates `ChangeBackDropEntry` through `HandleClientDataChange` and `OnPropertyChange`; `.bgb` and the URL read directly from the original asset remain unchanged. `818/RPL_PROPLIST` handles `qpJoinBackUrl`; `819/RPL_PROPEND` ends the query. PICS Join/Create has no invented success without a rating provider. The multi-room test uses two real BGB files and `IDS_DEFAULT_SERVERLIST` and covers shared `serverConn`. |
| Default/room protocol and socket ownership | `CommunicationInits` creates exactly one documentless default `CIrcProto`; `GetIrcProto`/`GetDefaultProto` return this connection protocol and `CommunicationCleanup` destroys it. Each `CChatDoc` owns its own `NewDefaultProto(this)` and deletes it in its destructor. Every protocol points to global `serverConn`. Self `JOIN` creates a room protocol; `bProcessAddChannel` searches the addressed document and then an empty document slot, deletes that slot's old protocol, and binds the room, keeping default and room objects separate. `m_iConnected`, `m_bIrcXServer`, query list, and `m_nMaxMsgLength` belong to `serverConn`; only `m_bInRoom`, channel, modes, topic, and CLIENT data belong to a room protocol. Join, history, and state tests cover ownership without a stack object at an original deletion site; eighteen CTests and a five-second offscreen lifetime run pass. If no empty document exists, the unresolved MDI `ID_FILE_NEW` branch is not replaced by an invented room slot; the source error path sends `PART`. |
| `CREATE` receive path and user visibility | `cmdidCreate` creates a document-bound `NewDefaultProto`, moves the room into in-channel state through `bProcessAddChannel`, and queues `qpInitialNames`, `qpInitialTopic`, `qpInitialMode`, `qpInitialWho`, plus IRCX-only `qpJoinBackUrl` in source order; it creates no participant or starring data. Before the connection action, `CIrcProto::OnLogin` invokes original `SetVisibility(theApp.m_flags1 & F1_USERVISIBLE)`. `ctSetUserMode` sends exactly `MODE <nick> +i` for `qpSetInvisible` and `MODE <nick> -i` for `qpSetVisible`; a self `MODE <nick> :+i/-i` echo changes `F1_USERVISIBLE` and removes the matching query by the source branch. Codes `221`, `301`, `305`, and `306` change no application state and select only `CIrcPrint` formatting. `924` removes a matching PICS Join/Create query but does not continue with invented success without rating provider/RoomInfo store. Login and state tests cover exact visibility bytes, query purposes, flag changes, and the `CREATE` initial-query set. |
| IRC status output in `status.*` | `CIrcPrint` contains source types `PT_NOTINIT`, `PT_WHOLESTRING`, `PT_LASTSTRING`, `PT_NONE`, and `PT_OFFSET` with color, offset, and group newline. `AddToStatus` remains in `status.cpp`, searches only the actual `CStatusView` document, extracts trailing or offset-based text, strips CR, shifts optional format ranges, maps fixed `COLORREF` to `QTextCharFormat`, and writes through `CTextCore::iDisplayInfo(mtGetInfo)`; without a status view it has no effect. `ProcessMessage` finalizes each handled path through this descriptor. PING/JOIN/CREATE/chat/Part/Quit/Kill/Whisper are suppressed in status; user MODE, `221`, `301`, `305`, and `306` use source formats; Welcome is displayed once before `OnLogin`. Unknown commands/replies remain invisible as in the original release path. `DecodeNickForScreen` in `ircproto.*` decodes IRCX nicks and adds original outer quotes around C1-control/blank-equivalent characters; `CUserInfo`, `GetMyScreenName`, nick changes, and Away reporting use the helper. TextView/parser tests cover status color/separation and normal/quoted names derived from original resources. |
| MDI base model, hidden status document, and self-`JOIN` child frame | `childfrm.*` retains its original name and contains exactly one `CChatView` client per document; `QMdiArea` replaces only MFC MDI mechanics. Startup creates a dedicated `CChatDoc` with `m_bStatusView=TRUE`, `STATUS_WINDOW_NAME`, and a real `CStatusView` before normal room documents, but keeps it hidden and without a tab as `CreateStatusWnd` does. Showing/closing status sets `F0_SHOWSTATUSWINDOW`, adds/removes the status tab, and hides rather than destroys it. MDI activation invokes document-bound `SetChatDoc`/`LoadDocData`/`ResetStatus`; tabs activate the corresponding child. Empty document titles use `Room` from the `IDR_MAINFRAME` document string with MFC-style sequence numbers, and the frame title follows `AFX_IDS_APP_TITLE - [RoomN]`. On self JOIN, `bProcessAddChannel` uses a matching/empty slot or creates a child through the `ID_FILE_NEW` branch before binding room protocol, connection state, and four initial queries. `original-mdi-join` uses only default resources and source-backed JOIN form and proves no self PUI or starring exists without `353`. Cascade, horizontal/vertical tile, base Autoarrange flag, and status command are connected. Exact child occlusion, `F1_MAXMDI`/window-rectangle persistence, menu switching, and complete Close/SaveModified behavior are unresolved. A full build and nineteen CTests cover this scope. |
| Member context menus and information actions | For empty space, a normal participant, and self operator, `memblst.cpp` loads `IDR_MEMBERCONTEXT`, `IDR_IRC_MEMBER`, or `IDR_MEMBERADMIN` directly from `chat.rc`; no handwritten labels or TODO output remain. The `IDD_BAN` resource placeholder removed at runtime by `AddMacroMenu` is hidden without a macro list, leaving the submenu empty/disabled. `CChatDoc` provides `GetNextSelectedMember`, `SelectedMemberCount`, `OnMemberGetinfo`, `OnMemberIgnore`, `OnGetidentity`, `OnGetVersion`, `OnPingUser`, `OnGetLocaltime`, `OnSendEmail`, and `OnVisitHomepage`. Enable/check conditions follow corresponding `OnUpdate...` functions. `CIrcProto::ChatGetIdentity` displays known `user@host` through `IDS_REPORT_IDENT2` and `GetInfoEntry`, or queues `qpGetIdent/ctWhoIs`; `311` calls `ShowIdentity` only for that query and `318` ends it. Qt list sorting preserves selection and the focused item as original ListView sorting does. `original-member-commands` uses only default resources and a directly enumerated AVB name to cover exact Profile/WHOIS/VERSION/TIME/EMAIL/URL/PING requests, identity history, and ignore toggle. |
| Direct dialog resources | `src/qt/originalassets.*` reads `DIALOG`/`DIALOGEX`, `CAPTION`, and multiline controls with quoted commas directly from unmodified `chat.rc`; no resource is copied and no asset converted. `setupdlg.cpp` obtains Setup radio buttons, welcome text, Favorites/Server, Channel/Password labels, tabs, and standard buttons from `IDD_SETUPDIALOG`, `IDD_CHANNEL`, and original property pages. `proppage.cpp` obtains Personal/Character/Background labels and `IDS_OPTIONS` from the same resources. Non-original class name `CChannelPropDlg` is replaced by original `CChannelProp`; `CChannelCreateDlg` derives from it and uses `IDD_CHANNELCREATE` controls. `original-assets` covers dialog dimensions and concrete original text, including a quoted comma. A full build and twenty CTests cover resource origin; DLU-exact layout and completeness are separate criteria. |
| `chanprop.*` room properties/create controls and topic/mode protocol | `CChannelProp` owns original fields, `CRtfCtrl m_rtfTopic`, host/topic-only/read-only messages, every visible control, mutually exclusive Hidden/Private, `MAX_TOPICLEN`, `MAX_CHANNELPWD`, IRC/IRCX room-name limits, user limit 0..10000 with waiver for an existing higher value, and Auditorium/No-whispers controls hidden by `chat.rc`. Dialog font, DLU coordinates, caption, text, style, and visibility come directly from `IDD_CHANNELPROP`/`IDD_CHANNELCREATE`; Qt calculates only documented dialog-unit mapping. `CRoomInfo::DoChannelDialog` copies topic formatting and room state, calls `ChatSetTopic` for text/format differences, forms modes starting at `CM_NOEXTERN`, and invokes `ChatSetMode`. `CIrcProto::ChatSetTopic`, `GetModeChars`, and `ChatSetMode` send source-backed query/mode lines with `p,s,i,t,n,m,l,k` order and key unset before set. Main and text context menus enable `ID_CHANNELPROPS` with original in-channel/self/member condition. `original-channel-properties` uses only original resources/constants to cover resource geometry, visibility, three rights cases, Create fields, topic query, and exact mode bytes. Non-ASCII ACP/DBCS remains an unresolved encoding boundary. |
| Enter/Create/Leave source behavior | From `chat.cpp`, `protsupp.cpp`, `ircsock.cpp`, `ircproto.cpp`, `setupdlg.*`, `chanprop.*`, and `chat.rc`: `ID_SESSION_NEWROOM` invokes `ChatSwitchChannel(NULL)`, `ID_ROOM_CREATEROOM` invokes `ChatCreateRoom(g_enterInfo)`, and `ID_SESSION_LEAVE` closes the active in-channel document. `ChatSwitchChannel` reads `IDD_CHANNEL`, sets `g_bEnterOnCreate=FALSE`, and passes through `bSwitchToRoom`. `ChatCreateRoom` reads `IDD_CHANNELCREATE`, gathers selected flags/limit/key starting at `CM_NOEXTERN`, copies topic and formatting, sets `m_bSetMode=TRUE`, encodes the channel, sets `g_bEnterOnCreate=TRUE`, and also calls `bSwitchToRoom(NULL)`. `bSwitchToRoom` confirms Away, activates a live room, closes a same-named disconnected room, or sets `g_bCXPrompt=FALSE` and invokes `InitializeChannelConnection`, which sends `JOIN` or `CREATE` by path. Create modes and formatted topic are applied only upon matching `324 RPL_CHANNELMODEIS`: `CChatApp::m_enterInfos` finds exact `CRoomInfo`, `ParseChannelMode` reads server state, then `ChatSetMode` and optional `SzControlFull`/`ChatSetTopic` run before the RoomInfo entry is removed or `g_enterInfo` is cleared through `bInitEnterInfo`. Acceptance covers null vs explicit channel argument, room activation, dialog cancel, comma failure, `g_nCXKeepServer`/`g_bCXPrompt`, exact wire strings, and RoomInfo lifetime. Self `CREATE`, `353`, and `366` never expose local Create data as participant or starring. |
| Enter/Create/Leave implementation and deferred Create completion | `CChatApp` provides `m_enterInfos`, `GetRoomInfoFromName`, `AddRoomInfo`, `RemoveRoomInfo`, and `CleanRoomInfos`, with `g_enterInfo` at index 0 and original case-insensitive clone-suffix search. `protsupp.*` provides `g_nCXKeepServer`, `g_bCXPrompt`, `g_bEnterOnCreate`, `InitializeChannelConnection`, `bInitEnterInfo`, `ChatSwitchChannel`, `ChatCreateRoom`, `bSwitchToRoom`, `ShowBadChannelName`, and `ConfirmAway`. `IDD_CHANNEL` and `IDD_CHANNELCREATE` are modal in these paths; existing live documents activate and disconnected ones close through MDI. Main menu/toolbar bind `ID_SESSION_NEWROOM`, `ID_ROOM_CREATEROOM`, and `ID_SESSION_LEAVE` with original status conditions; `/JOIN`, `/CREATE`, and `/PART` use the same glue/document functions. The GUI Create source path calls `bSwitchToRoom(NULL)` with default `bCreateRoom=FALSE`, so it sends `JOIN`; only slash `CREATE` passes `bCreateRoom=TRUE` and sends `CREATE`. Code 324 finds matching RoomInfo before `ParseChannelMode`, handles `+y` through original `FixMICChannelName`, sends exact unset/set `MODE` and optional `SzControlFull` topic for `m_bSetMode`, then removes/resets data. `original-room-switch` uses only original resources/constants to cover clone search, initialization, three exact wire strings, format controls, reset, and empty member/self state. MFC `SaveModified` prompting for a changed disconnected document and embedded `DeleteContents` are unresolved. |
| `motd.*` source behavior: Away and Message of the Day | `motd.h/.cpp`, `chat.cpp::OnAwayToggle/OnMotd`, `ircproto.cpp::bChatShowMOTD`, MOTD/LUSERS cases in `ircsock.cpp`, and `IDD_AWAYDLG`/`IDD_MOTD` define this boundary. `CAwayDlg` builds its 186x87 DLU caption/font/static/`IDC_AWAYMSG`/OK/Cancel directly from resources, applies `MAX_INPUTLEN`, splits Control-Full into text and format array on open, enables OK only for nonempty left-trimmed text, and returns formatting through `PRGDWGetFormatting`. Enabling Away uses only `IDS_DFLTAWAYMSG` for the initial value, sets Away/prompt only after accepted dialog, and sends `AWAY :<control-full>`; disabling shows no dialog and sends `AWAY`. `CMOTD` mirrors 298x146 DLU, creates a read-only edit at hidden `IDC_EDITPOS`, displays LUSERS blue, optional newline, and MOTD black, and stores `F1_SHOWMOTD` through `IDC_SHOW_MOTD`. `ID_MOTD` is enabled only in `CX_INCHANNEL`/`CX_NOCHANNEL` without an active request, queues `qpLUsersMOTD/ctLUsersMOTD`, and sends exactly `LUSERS\r\nMOTD\r\n`. Code 375 stays invisible; 372/377 remove only source-backed `- ` or standalone `-` prefixes; 376 performs dialog/buffer/query cleanup by query and flag; 422 reports `IDS_ERR_NOMOTD`, may display accumulated LUSERS, and performs the same cleanup. No server message, MOTD line, or LUSERS count is locally created. |
| `motd.*`, Away, and MOTD implementation | `src/original/motd.*` provides `CAwayDlg` and `CMOTD` at absolute DLU positions read from `IDD_AWAYDLG`/`IDD_MOTD`, without Qt layout or substitute native dialog. Away splits/reconstructs Control-Full through existing original functions, limits to `MAX_INPUTLEN`, enables OK only for nonempty left-trimmed text, and sends exactly `AWAY :<Text>\r\n` or `AWAY\r\n`. `ID_MOTD` follows original enable logic; `bChatShowMOTD` queues `qpLUsersMOTD/ctLUsersMOTD` and sends exactly `LUSERS\r\nMOTD\r\n`. `ircsock.*` accumulates only received 251--255/265/266 lines and handles 375/372/377/376/422 with source prefix, dialog, status, query, and cleanup effects. `CMOTD` displays LUSERS blue and MOTD black and stores only `F1_SHOWMOTD`. `original-motd-away` covers resource geometry, read-only/color state, blank validation, exact wire bytes, parser buffers, and query lifecycle using original resources/case forms. A full build and twenty-three CTests cover the path. No MOTD, LUSERS, or Away content is invented locally. |
| `roomlist.*`/`userlist.*` source behavior | All 611 lines of `roomlist.cpp`, 156 of `roomlist.h`, 607 of `userlist.cpp`, 169 of `userlist.h`, callers, LIST/LISTX/WHO/TOPIC/error branches, `IDD_ROOMLIST` 400x255, and `IDD_USERLIST` 395x263 are inspected. Original classes are `CRoom`, `CRoomListPersist`, `CRoomListCtrl`, `CRoomList`, `CUser`, `CUserListPersist`, `CUserListCtrl`, and `CUserList`. Controls, caption, font, and absolute DLU coordinates come from the two resources; headers/widths only from `ID_RL_*`/`ID_UL_*`. Room persistence is server-bound, retains insertion order, filters case-insensitively by room and optional topic/Registered/min/max 0--9999, sorts through original IRCX/IRC/non-letter sort byte, and enables Join/List Members only for one selection. User persistence retains AddRef/Release, four search types, case-insensitive filter, and four sort columns with nick tie-breaker; Invite/Whisper/Join use only selected real WHO results. `ID_CHATROOM_LIST`/`ID_USER_LIST` require no active search and default-protocol `CX_INCHANNEL`/`CX_NOCHANNEL`. `ChatFillRoomList` sends exact `LIST`, `LIST <arg>`, `LISTX`, or `LISTX N=<arg>`; `ChatFillUserList` sends source-built WHO masks. 321/811 initialize/clear, 322 or 812/813 create server rooms, 323/816/817 sort/end/remove query; 352 creates real `CUser` only for `qpUserListDlg`, and 315 ends/removes. List Members sends `TOPIC <encoded room>` before WHO on 331/332/442; 403 ends with `IDS_ERR_NOSUCHCHANNELANYMORE`. A missing Content Advisor is not replaced by an invented rating decision: only the original fallback “Ratings DLL not found = ratings disabled” applies. |
| Room/user-list implementation | All eight original classes remain under original filenames. Dialogs build directly from `IDD_ROOMLIST`/`IDD_USERLIST` without Qt layouts, including DLU geometry, resource caption, controls, and table columns. Server cache, refresh lock, filters, four/three sorting paths, AddRef/Release, and selection conditions follow source. Main menu and slash `LIST`/`WHO` use `OnChatroomListAux`/`OnUserListAux`; `ircproto.*` sends exact LIST/LISTX/WHO/INVITE; `ircsock.*` handles 321--323, 811--817, 352/315, and TOPIC-staged List Members with 331/332/442/403. Lists contain only received server records and never alter member map, comic, or starring. `original-room-user-list` covers resource geometry, sorting, exact wire bytes, query cleanup, and TOPIC→User dialog→WHO. A full build and twenty-four CTests cover this path. Nonempty PICS ratings remain hidden without an equivalent provider. `CUser::operator==` is declared but uncalled in the original, so its semantics remain unresolved. |
| `admindlg.*` source behavior | All 307 lines of `admindlg.cpp`, 156 of `admindlg.h`, callers/update handlers, INVITE/WHOIS/BANLIST parser cases, query/types/resources, relevant menus, and `IDD_KICK` 186x89, `IDD_BAN` 186x170, `IDD_INVITE` 186x89, `IDD_INVITATION` 186x93 are inspected. Original classes are `CKickDialog`, `CBanDlg`, `CInviteDlg`, and `CInvitationDlg`. Kick is enabled for exactly one non-self selection and queues `qpKickDlg/ctWhoIs`; code 311 uses `GetBanString` to produce exact `*!*@host` for plain IRC or `~user`, otherwise IRCX `*!user@host`, opens the dialog, optionally bans first, then always sends `KICK <room> <nick> :<trimmed reason>`. Ban accepts zero or one non-self selection: zero sends `MODE <room> +b`; one runs WHOIS then the same ban-list query with suggested mask. Code 367 accumulates only server masks and 368 opens `CBanDlg`; Ban/UnBan immediately sends `MODE <room> +/-b <mask>` and changes only dialog combo. Invite requires `bCanInvite`, accepts at most 255 characters, splits exactly on space/comma/CR/LF, IRCX-encodes extended nicks only for IRCX, and sends one `INVITE <nick> <room>` per real token. Code 341 shows only `IDS_INVITE_CONF`. Incoming INVITE forms identity from `user@machine`, checks AllowInvites, rating fallback, Ignore, and re-entry; only Accept calls `bSwitchToRoom`, and Ignore applies after Accept/Reject but not Cancel. Host/Speaker/Spectator sends exact `+o`, optional `-o`, moderated-room `+v`, or `-v` through `ChatSetOperator`; enable/radio state follows self operator, selection, and `CM_MODERATED`. No nick, reason, ban pattern, or room is prefilled except from real WHOIS/resource/invitation data. |
| `admindlg.*`, administration/invite protocol, and roles | `CKickDialog`, `CBanDlg`, `CInviteDlg`, and `CInvitationDlg` are in `src/original/admindlg.*` and build caption, font, controls, text, and absolute DLU placement directly from four unmodified resources, without Qt layouts or example values. `chatdoc.*`, `memblst.*`, and `mainfrm.*` use original selection/self/operator/invite-only/moderated conditions and radio state. `ircproto.*`, `protsupp.*`, and `ircsock.*` implement WHOIS 311/318, optional Ban-before-Kick, BANLIST 367/368, Ban/UnBan, tokenized INVITE fan-out, 341 confirmation, incoming INVITE gates, and exact `+o`/`-o`/`+v`/`-v`; outgoing dialogs restore Say focus. `original-admin-dialog` uses only source/resource values to cover geometry, limits, button state, query lifecycle, and wire strings. Its fixture initializes fonts through `InitializeFonts` and `CUnitPanelPage::SetFonts`, matching the source precondition. A full build and twenty-five CTests cover the path, including source-identical `eOnInvitation`. |
| `whisprbx.*` source behavior | The evidence base is all 828 lines of `whisprbx.cpp`, 105 of `whisprbx.h`, callers, `resource.h`, and `chat.rc`. Original symbols are `CWhisperLeaf`, `CWhisperBox`, `CreateWhisperBox`, `WhisperBox`, `DestroyWhisperBox`, `InitializeWhisperCores`, `bAddToWhisperBox`, and `bWhisperInBox`. `IDD_WHISPERBOX` is a separate modeless minimizable/resizable 334x196-DLU desktop window with resource caption and 8-point MS Sans Serif: `IDC_TAB1` at 7,7,320,147 with button/multiline style; hidden placement marker `IDC_SAYPOSITION` at 7,155,320,18; `IDC_DELETE_TAB` at 277,178,50,14; `IDC_IGNORE_WBOX` at 216,180,51,10. It uses `IDI_WHISPER`, 267x175-pixel minimum, `IDR_STATUSVIEW` context without `ID_CLEAR_HISTORY`, and `IDR_WHISPERACCEL` (Ctrl+W Whisper, Ctrl+E Whisper Action, Ctrl+G Whisper Sound) plus RichEdit accelerators. Tabs arise only from real `CUserInfo` or received external private messages, sort case-insensitively by `GetScreenName()`, and resolve exactly by `GetName()`; creation starts with zero tabs and no sample content. Each tab owns one read-only text view/`CTextCore`, 65536-character maximum, PUI-derived label/nick/fullname/ignore state, and the same display calls. Switching shows the new view before hiding old, synchronizes Ignore, and removes `" *"`; Delete selects the preceding tab or hides an empty box. Resize uses `RIGHT/LEFT/TOP=7`, `BOTTOM=3`, `INTERBUTTON=7`, `SAYTOPFROMBOTTOM=40`, `TABBOTTOMFROMBOTTOM=46`; non-minimized geometry stores in `theApp.m_rectWhisper`. Escape is swallowed and close restores room Say focus. Receive intercepts `BM_WHISPER` only for an external PUI or an in-room PUI with an existing tab; an in-room private whisper without a tab remains in the room. Sending places exactly the active leaf's resolved PUI in `g_rgpuiWhisperees`, calls `bChatSendText(..., echo=FALSE, whispereesFilled=TRUE, invokedByWhisperBox=TRUE)`, and explicitly draws self header/text/action once in the leaf. |
| Whisper Box implementation | `src/original/whisprbx.h/.cpp` restores original symbols and data partition. `CWhisperBox` is a parentless modeless window without Qt layout; caption, 334x196 DLU base size, four controls, 267x175 minimum, and `IDI_WHISPER` come directly from `chat.rc` and unmodified assets. The Qt adapter for `TCS_BUTTONS|TCS_MULTILINE` owns no chat model: `CWhisperLeaf` is the sole leaf boundary and each real tab has read-only `QTextEdit` plus 65536-character `CTextCore`. There are zero startup tabs and no local content. Real PUIs sort by screen name, resolve by exact nick, and support unread ` *`, Ignore, Delete/Hide, and font reinitialization. `ProcessSay` catches only external whispers or room whispers with existing tabs. `bWhisperInBox` resolves through `PuiFromDocNickIdent`/`ExternalPui`, fills one `g_rgpuiWhisperees` entry, sends without room echo, and displays header/text/action exactly once. `CSayWnd`, Ctrl+W/Ctrl+E, PageUp/Down, Tab focus, `/MSG`, `/ME`, `/THINK`, `CChatDoc::OnWhisperboxMlist`, member menu, and user-list return code 8181 use this path. `original-whisper-box` uses only source/resource values and real `CUserInfo` objects to cover geometry, empty start, sorting/lookup, buffer, unread state, external-vs-room receive, exact PRIVMSG/CTCP ACTION bytes, no duplicate echo, Ignore, user list, and lifecycle. A full build and twenty-six CTests cover it. Ctrl+G/Sound remains disabled without `sounddlg.*`/`mcithrd.*`; no substitute effect is generated. |
| `rules.*`/`actions.*` | `src/original/rules.*` and `actions.*` restore the original events, actions, metadata, rule classes, delayed actions, filters, resource text, binary Rule/`.crs` format, pre/post display hooks, and daemon enumeration. Unsupported sound/file/service-dependent actions stay inert until their original modules exist. |
| `eOnKick` | `OnKick` uses the source action array `{1, aHighlightMessage}`, builds event identity from the real kickee and optional full name, runs the pre-rule before UDI/history, and runs the post-rule after the Part/Self path with Highlight rejected. |
| Disconnect | `ChatServerDisconnect` exists again in `protsupp.*`: socket close, identity suffix save, query/app cleanup, socket reset, forced part for real room documents, default status, and one `eOnDisconnect` hook. Multi-server resume, IdentD, and full Windows connector edges remain platform/source gaps. |
| Join/Leave/NewHost/Connect/Invitation rules | The rule hooks are wired only at their source positions: self `353` after `bSingleJoin`, live foreign `JOIN`/`PART`, operator gain for `eOnNewHost`, `001` before `OnLogin`, and invitation after Allow/Rating/Ignore/Reentry gates. |
| Rule and notification daemons | `CCDaemonExt`, `CCNotif`, and `CCDynaNotifs` retain source ownership, query refcounts, two result generations, WHO/LIST/LISTX feeds, timer intervals, versioned binary formats, sort/equality rules, and login/disconnect coupling. Presence and notification users come only from real parser rows. |
| Notification and automation UI | `notipage.*` and `autopage.*` keep original class names, direct DLU resources, icons/BMPs at original paths, definition lists, macro slots, dynamic rule editor controls, Apply/Cancel behavior, and disabled source-missing sound/file actions. No converted assets or local notification users are produced. |
| Chatserver and connector | `chatsrv.*` remains the only server configuration model. QSettings/QHostInfo/QTcpSocket replace Registry/resolver/socket mechanics only. Service forms, migration, auth records, transactions, icons, five-slot connector behavior, 50-ms continuation, retry/resume, and socket handoff are source-backed; SSPI Auth 2/3 is unresolved. |
| Encoding and wire bytes | `ccommon.*`, `fechrcnv.h`, `jis2sjis.cpp`, `sjis2jis.cpp`, and `intl.c/.h` restore IRCX escaping, quoting, legacy UTF-8, JIS/Shift-JIS conversion, Far-East DBCS walking/wrapping, and byte-to-format-offset boundaries. Qt text conversion is limited to socket and painter adapter edges. |
| Comic interaction, URL, context menus, and printing | `pageview.*`, `panel.*`, `balloon.*`, `urlutil.*`, `format.*`, `memblst.*`, `textview.*`, and `chatview.*` own resize replay, hit testing, member selection, URL ranges, hotlink routing, resource context menus, and visible comic/text printing. `print.cpp` stays `E_NOTIMPL`; no alternate print algorithm is inferred from it. |

#### Detail Index And Acceptance: Conversation History, Document, And App

This block is source-read. Qt targets remain `src/original/histent.*`, `chatdoc.*`, `chat.*`, and the existing original views. Qt may replace archive, MDI, Registry, OLE, and file-dialog mechanics only; a separate conversation model or session controller is not allowed.

| Original Module / Symbol | Source-Backed Behavior | Qt Status / Acceptance |
| --- | --- | --- |
| `histent.cpp/.h` | Nine entry classes own the source `Execute` and `WriteSelf` paths: `SayEntry`, `JoinEntry`, `PartEntry`, `ChangeAvatarEntry`, `GetInfoEntry`, `ComicCharacterEntry`, `StartHistoryEntry`, `ChangeBackDropEntry`, and `NickEntry`. | All nine classes, ownership, `HM_LIVE/HM_RELOAD/HM_LOAD`, format/UDI copies, structural conversation records, replay, and Clear are ported/tested. |
| `CChatDoc` history and files | Conversation files begin with `#CHATCONVERSATION`; locators use `#CHATLOCATOR`; `.ccr`, `.ccc`, and `.rtf` are chosen by original extension logic. Clear/replay uses the same history list, not widget state. | Stream roundtrip and replay exist; visible file/dialog integration, locator quirks, RTF stream, save/open errors, and command-state edges remain. |
| `CChatApp` | Startup initializes communication, fonts, emotions, backdrops, Registry defaults, rules, OLE, MDI template, status document, command line, and favorites watch. Art downloads in the Modern source return `FALSE` before old WinInet code. | Defaults, app/frame/coolbar/service/macro/rule/notification persistence, status document, startup, and shutdown order are ported. TextFont roles, favorites watcher, and more shell paths remain. |
| `print.cpp` | DocObject `IPrint` methods return `E_NOTIMPL`. | No product behavior is derived from `print.cpp`; visible printing is in view modules. |

#### Detail Index And Acceptance: INTL, Audio, Download, DCC, And Utilities

| Original Module / Resource | Source-Backed Behavior | Qt Status / Acceptance |
| --- | --- | --- |
| `intl.c/.h` | Active code covers CP932/949/950/936 DBCS stepping, trail-byte checks, punctuation-aware wrap, measuring, fit search, and charset selection; disabled MIME/browser blocks remain excluded. | Active paths are ported and byte-tested; unused broken helpers do not become active code. |
| `mcithrd.*` and sound helpers | MCI thread play/stop/loop/notify/lock semantics and `SND_*` meanings are source-defined. | Missing; Say Sound and Whisper Sound remain disabled without substitute sounds. |
| `webreq.*` | WinInet request queues and callbacks exist but normal avatar/backdrop callers return before using them. | Not ported for productive use. No Qt downloader is created from dead code. |
| `filesend.*`, `IDD_FILE_TRANSFER` | CTCP `DCC SEND`, quoting, port selection, block/ACK sizes, receive limits, dialogs, and status strings are fixed by source and resources. | Missing; file transfer remains disabled. |
| `utils.*` | Utility code covers version/media paths, icons, lists, encode/decode, combos, browse dialog, file enumeration, string search, unique names, no-case maps, resizing, and monitor clamping. | Only needed canonical callers are ported. `GenericUser` remains an internal encryption fallback, never a visible nick. |

#### Detail Index And Acceptance: Administration, Room Dialogs, MOTD, MDI Child, Lists, Whisper, Sound, And Fonts

| Original Module / Resource | Source-Backed Behavior | Qt Status / Acceptance |
| --- | --- | --- |
| `admindlg.*` | `IDD_KICK`, `IDD_BAN`, `IDD_INVITE`, and `IDD_INVITATION` define exact DLU dialogs, limits, enable states, WHOIS/BANLIST query flow, optional Ban-before-Kick, Invite fan-out, incoming Invite gates, and role mode commands. | Implemented/tested through original resources and real server/user data only. |
| `chanprop.*` | `IDD_CHANNELPROP` and `IDD_CHANNELCREATE` define topic RichEdit, visible modes, hidden inactive Auditorium/NoWhispers controls, topic/password/room limits, Hidden/Private exclusion, user limit, and mode/topic send order. | Properties are implemented/tested. Create dialog fields exist; full create caller and some room-switch failure edges remain. |
| `motd.*` | `CAwayDlg` and `CMOTD` use direct DLU resources. Away carries Control-Full format and exact `AWAY` bytes; MOTD/LUSERS displays only received server text with source colors and cleanup. | Implemented/tested; no local MOTD, LUSERS, or Away content is generated. |
| `childfrm.*` | MDI activation loads document state, menus, status, and tab; deactivation clears room/UI globals. Status close hides. Coverage and `F1_MAXMDI` persistence are source behavior. | Multiroom child frames, activation, status hide, tabs, and basic autoarrange exist; exact occlusion, menu switching, frame persistence, and full close/save flow remain. |
| `roomlist.*` / `userlist.*` | Dialogs use direct resources, server-bound caches, filters, sorting, F5 refresh, LIST/LISTX/WHO, List Members staging, Invite/Whisper/Join conditions, and no local records. | Implemented/tested; nonempty PICS remains without provider; unresolved `CUser::operator==` stays unresolved. |
| `whisprbx.*` | Separate nonmodal resource window, one read-only `CTextCore` per real PUI, empty startup tabs, external/existing-tab receive boundary, rules, unread `*`, ignore, delete, exact send path, and accelerators. | Implemented/tested. Sound alias and URL launch wait for original modules. |
| `rtfcmb.*`, `sounddlg.*`, `txtfntdg.*` | RichEdit combo overlay, sound picker, and 19-entry message-type font dialog are source/resource-defined. | `rtfcmb.*` and `sounddlg.*` remain missing; TextFonts core is partial and full dialog coupling remains a TODO. |

#### Detail Index And Acceptance: Chatserver, Shared Cores, Setup, And IRC Core

| Original Module / Symbol | Source-Backed Behavior | Qt Status / Acceptance |
| --- | --- | --- |
| `chatsrv.*` | Registry root, service forms, groups, auth/password records, UI transaction, combo icons, connector resolution, five parallel attempts, retry, and source quirks. | Ported/tested under original names with platform replacements only. SSPI Auth 2/3 remains unresolved. |
| Excluded/obsolete files | `cache.cpp`, `nmproto.*`, `urlfind.cpp`, `wmini.cpp`, and similar non-canonical or incomplete files are not normal Modern product code. | Reuse `X`; they are not compiled or used to invent behavior. |
| Shared cores | Wrappers for `ccommon`, `ccomp`, JIS converters, `urlutil`, and `dlylddll` select external core files. | Selected code is ported or explicitly bounded. Tables, byte ownership, URL bounds, and delayed-DLL semantics stay source-defined. |
| Setup/options/persistence | `setupdlg.*` and `proppage.*` define registry fields, macros, filters, Connect, Favorites, Settings, Personal/Profile, Character, Background, Text/Comic fonts, Servers, and Coolbar persistence. | Major defaults, persistence, services, macros, rules, notifications, coolbar, art pages, and server page are ported. TextFonts, locator, favorites, some dialogs, and page metrics remain. |
| `query.*`, `userinfo.*`, `memblst.*` | Queries own purpose/type/data/refcounts/rank. Users own exact flags/request bits/avatar/profile state. Member list owns source sorting, status, context menus, and selection semantics. | Query core, daemon ownership, selected user/member behavior, and many commands are ported; full flags, icons, request counters, and enable edges remain. |
| `ircproto.*` | Source bytes define login, join/create/part, send chunking, PRIVMSG/DATA, modes, admin, queries, identity, visibility, annotation send, low-level quoting, and encoding boundaries. | Large core is ported/tested; ACP/DBCS/JIS, CP932 single-chunk, IdentD, and more query paths remain. |
| `ircsock.*` | Source parser/table, login/auth, command dispatch, numerics, errors, JOIN/NAMES, messages, status printing, and query cleanup are authoritative. | Parser, table, login base, join/message state, invite, WHOIS, banlist, room/user lists, rule/notif daemon branches, and many status paths are ported; remaining dispatch/results/errors/SSPI paths remain. |
| `protsupp.*` | User/member lifecycle, comments, UDI, CTCP/action/sound/DCC/info, ignore/flood, rules, slash commands, sends, room switching, enumeration end, rating, encoding, and reconnect are the glue boundary. | Join/starring glue, UDI, send/receive core, whisper, ignore/flood, info, admin, lists, service connect, disconnect, and many slash paths are ported. CTCP/comment completeness, sounds, encoding, keystrings, multiroom edges remain. |

### Canonical Build And Port Status

This list comes from `chat.mak` and `base/sources`. `Partial port` means a same-named Qt source exists; it is not a feature release.

| Original Object | Original Source | Reuse | Qt Status |
| --- | --- | --- | --- |
| `stdafx` | `stdafx.cpp`, `stdafx.h` | R3 | No Qt PCH required; definitions are mapped individually. |
| `dlylddll` | `dlylddll.c` and shared core | R3 | Audited; Windows DLL thunks remain platform boundaries. |
| `actions` | `actions.cpp`, `actions.h` | R1 | Source-backed actions are ported; sound/text-file actions remain inactive. |
| `admindlg` | `admindlg.cpp`, `admindlg.h` | R2 | Four admin dialogs and invitation hook are ported/tested. |
| `arc`, `bbox`, `vector2d`, `spline`, `splinutl`, `traj` | geometry files | R0/R1 | Geometry helpers are ported or bounded with Qt as drawing primitive replacement only. |
| `autopage`, `rules`, `notif`, `notipage` | automation/rules/notification files | R1/R2 | Core and UI are ported/tested except source-missing sound/file edges and undefined declarations. |
| `avatar`, `avatario`, `avbfile`, `backdrop`, `bodycam`, `dib`, `fonts`, `textpose` | art/avatar/display files | R1/R2 | Direct original asset formats and core rendering are ported; download/artpack/metadata edges remain where noted. |
| `balloon`, `pageview`, `panel` | comic output files | R1/R2 | Comic layout, panels, balloons, starring, hit tests, context menus, URL, and printing are ported with known screen multi-page placement gap. |
| `bind*`, `mfcbind`, `oleobjct`, `chatitem`, `ipframe`, `icchat_i` | OLE/COM files | R3 | Platform boundary; command semantics are ported only where visible outside COM. |
| `chat`, `mainfrm`, `chatdoc`, `chatview`, `childfrm`, `tabbar`, `chatbars`, `coolbar`, `spltchat`, `status` | app/UI shell files | R2 | Main shell, MDI/status/tabs, views, splitters, bars, and many commands are ported; full close/save/menu/focus edges remain. |
| `chatsrv`, `setupdlg`, `proppage`, `chanprop`, `motd`, `roomlist`, `userlist`, `whisprbx` | dialogs/service/list files | R1/R2 | Major original dialogs and service/list/whisper models are ported/tested; font, sound, file-transfer, and selected validation gaps remain. |
| `format`, `rtfctrl`, `rtfcmb`, `saywnd`, `textcore`, `textview`, `txtfntdg` | text/input files | R1/R2 | Formatting, input, TextCore, text view, printing, and RTF control are mostly ported; RTF stream, `rtfcmb`, font dialog, URL/sound edges remain. |
| `ircproto`, `ircsock`, `protsupp`, `query`, `userinfo`, `memblst` | IRC/protocol/state files | R1/R2 | Parser, sending, join/starring glue, rules, lists, admin, info, disconnect, and much state are ported; remaining numerics/errors, SSPI, IdentD, encoding, CTCP/comment completeness, and icons remain. |
| `ccommon`, `ccomp`, `jis2sjis`, `sjis2jis`, `intl`, `urlutil` | shared/encoding/URL cores | R0/R1 | Active byte, mask, conversion, DBCS, and URL semantics are ported or explicitly bounded; disabled legacy blocks stay excluded. |
| `filesend`, `sounddlg`, `mcithrd`, `webreq` | file/audio/network adjuncts | R1/R2 | Audited; normal product paths remain disabled or unimplemented without substitutes. |
| `chat.res` | `chat.rc`, `resource.h`, related resources | R2/A | Resources are audited and mirrored directly; no converted image assets or guessed MFC stock resources. |

Files outside the canonical Modern build are Reuse `X`: `bothdlg.cpp`, `cache.cpp`, `cllist.cpp`, `dumbwnd.cpp`, `guids.cpp`, `nmproto.cpp`, `script.cpp`, `semantic.cpp`, `url.cpp`, `urlfind.cpp`, and `wmini.cpp`. The Qt build does not include them. Commented experiments remain excluded. COM/NetMeeting IDL/build files are indexed as platform paths.

## Qt Port: UI Analysis Before Code

This analysis is the binding basis for the Qt port. It describes only behavior visible in `v2.5-beta-1-modern`; everything else is unresolved.

| UI Element | Original Files | Resource IDs | Window Position | Visible Behavior And Interaction | Planned Qt Target |
| --- | --- | --- | --- | --- | --- |
| Main window/frame | `mainfrm.*` | `IDR_MAINFRAME`, toolbar/status/tabbar commands, status indicators | Menu top, coolbar/rebar top, tabbar dock top, MDI/client center, statusbar bottom. | Creates toolbar manager, statusbar, and tabbar; toggles bars; writes menu status help; handles palette/fixed-color updates. | `src/original/mainfrm.*`, `src/qt/win98palette.*` |
| Chat client layout | `chatview.*`, `spltchat.*` | runtime splitters, `ID_SAYCTRL` | Comic: left output/input, right member/bodycam. Text: text/input/member. Status: status/input. | Source split percentages 80/20 and 30/70, 23-pixel Say minimum, output-first resize, no draggable splitter base, key forwarding to Say. | `src/original/chatview.*`, `src/original/spltchat.*` |
| Tabbar | `tabbar.*` | `IDC_TAB1`, `IDB_TABS`, title/font strings | Top docked bar, height 29. | Status doc first, rooms alpha-sorted, image list from `IDB_TABS`, selection activates child, focus/key forwarding. | `src/original/tabbar.*` |
| Menus/toolbars/accelerators | `chat.rc`, `resource.h`, `chatbars.*`, `coolbar.*` | main menu, toolbars, context menus, accelerators | Menu/rebar at top. | Resource-built menus and three toolbar bands with Favorites dropdown, Comic/Text group, Away check, text-format checks, context menu, and packed persistence. | `src/original/chatbars.*`, `src/original/coolbar.*`, `src/original/mainfrm.*` |
| Comic output | `pageview.*`, `panel.*`, `balloon.*`, `wmini.cpp` | `IDR_VIEWCONTEXT`, `IDR_AVATARCONTEXT`, `ID_STARRING`, title strings | Left upper comic area. | Pages draw panels; title/starring from `AddTitle`/`UpdateTitle`; stars from real `g_mapNickToPtr`; balloons from source geometry. | `src/original/pageview.*`, `src/original/panel.*`, `src/original/balloon.*`; excluded `wmini.cpp` only if proven active |
| Input/Say window | `saywnd.*`, `rtfctrl.*`, `format.cpp` | `ID_SAYCTRL`, Say bar, send/action/sound/format commands, `IDR_FORMATTING` | Lower left/input area. | Enter sends through `bChatSendText`; buttons invoke original handlers; RTF control handles formatting, accelerators, context, Doskey; format core maps control codes/URLs. | `src/original/saywnd.*`, `src/original/rtfctrl.*`, `src/original/format.cpp` |
| Member list | `memblst.*`, `userinfo.*`, `protsupp.*` | member context resources and commands | Right upper comic area or right text area. | Icon/list modes, source sorting, double-click profile, context actions, and protocol-driven insert/remove. | `src/original/memblst.*`, `src/original/userinfo.*`, `src/original/protsupp.*` |
| BodyCam | `bodycam.*`, `avatar.*`, `avatario.*`, `textpose.cpp` | body context, emotion strings, face icons | Lower-right pane. | Draws avatar preview and emotion wheel; mouse/keys update `CEmotion`; freeze/send-expression use source commands; emotion bytes from `avatario.*`. | `src/original/bodycam.*`, `src/original/avatar.*`, `src/original/avatario.*`, `src/original/textpose.cpp` |
| Connect/setup/options/room dialogs | `setupdlg.*`, `proppage.*`, `chanprop.*`, `chat.rc` | setup, settings, personal, character, background, servers, channel, properties, create resources | Modal property sheets/dialogs. | Servers/favorites/channel, join/list/connect-only actions, identity/profile, art previews, server/security config, enter room, room properties, and create room all use source controls and validation. | `src/original/setupdlg.*`, `src/original/proppage.*`, `src/original/chanprop.*` |

## Qt Port: Original Join And Starring Flow

The Qt port must preserve this order and must not replace it with local fake data.

1. `RPL_WELCOME`/`001` in `CIrcSocket::ProcessMessage` completes connection, sets the server-confirmed nick through `SetMyNameNick`, writes Welcome to status, sets `CX_NOCHANNEL`, queues `qpInitialLUsersMOTD`, and calls `CIrcProto::OnLogin()`.
2. `CIrcProto::OnLogin()` starts rules/notifications, sets visibility, and follows `theApp.m_iOnConnectAction`: `CA_JOINROOM` calls `ChatJoinChannel(g_enterInfo)`, `CA_ROOMLIST` opens Room List.
3. `ChatJoinChannel()` checks IRCX/ratings through `qpJoinPics` when needed; `ChatJoinAux()` sends exactly `JOIN <channel>\r\n` or `JOIN <channel> <password>\r\n`.
4. Self `JOIN` handles both `JOIN :#room` and `JOIN #room`; foreign nicks create `JoinEntry` immediately. Self calls `bProcessAddChannel`, clears topic/mode/userlimit, optionally stores ident, creates `qpInitialNames/ctNames` and `qpInitialTopic/ctTopic`, then sends `MODE <room>` and `WHO <room>` plus IRCX back-url PROP query when applicable.
5. The source path does not explicitly send `NAMES` or `TOPIC` for the initial cells through `bExecuteQuery()`. Those cells classify `353`, `366`, and optional `332`.
6. `353` removes a missing-topic cell if needed, finds `ctNames/qpInitialNames`, resolves the document from `args[3]`, iterates `lastString` with `bForEachWord(..., bSingleJoin, pDoc, ..., " ")`, and suppresses status output.
7. `bSingleJoin()` creates a real `CUserInfo`, inserts a `JoinEntry` through `AddAndExecute`, and returns true when the inserted user is `doc->m_puiSelf`; that return triggers self `eOnJoin` rules.
8. `JoinEntry::Execute(HM_LIVE)` calls `CIUserJoin()`: self sets `g_puiSelf`, `doc->m_puiSelf`, `ComicUser(TRUE)`, own avatar fields, maps, all-channel list, and member list; foreign users receive a real enumerated avatar in comic mode. Moderated rooms can set `UF_SPECTATOR`.
9. `AddToMembersList()` refuses insertion outside `CX_INCHANNEL`, inserts sorted, updates status, and calls `UpdateTitle(doc)`/sort only when not in enumeration.
10. `366` finds and removes the matching names query and suppresses status output. The read source files do not show a direct `ProcessEndEnumeration()` call in that handler.
11. `ProcessEndEnumeration()` in `protsupp.cpp` clears enumeration lock, parts when self is missing, updates title in comic mode, sorts icon members, and clears modified state. The exact source call site remains unresolved unless another source proof is found.
12. Starring is produced by `CPage::AddTitle`/`CUnitPanelPage::AddTitle`, `UpdateTitle`, `AddStars`, and `AddStarsAux` from `g_mapNickToPtr`; self is first, others sort by departed state and `m_nSends`, and avatars without `m_icon` are ignored.
13. Self `JOIN` alone is not starring data. Names come from `353`; enumeration end is `366` plus the unresolved `ProcessEndEnumeration` bridge. Missing implementation stays empty rather than showing invented stars.

## Qt Port: Source-Backed Implementation Coverage

| Area | Qt Files | Original Basis | Status / Open Boundary |
| --- | --- | --- | --- |
| Message mode bits | `chat.h` | `defines.h` `BM_*` constants | Values match source. |
| Persistence | `setupdlg.cpp`, `rules.*`, `notif.*`, `originalsettings.*` | App/rules/notifications Registry code | App defaults, coolbar, macros, rules, notifications are ported; TextFonts remain. |
| IRC login/sending/receive | `ircsock.cpp`, `ircproto.cpp`, `protsupp.cpp` | login, parser, send, UDI, `ProcessSay` | Core wire paths are ported; SSPI, IdentD, all numerics/errors, ACP/DBCS/JIS, sound/DCC/NetMeeting remain as source gaps. |
| Comic output | `pageview.cpp`, `panel.cpp`, `balloon.cpp`, `backdrop.cpp`, `avatar.*` | pages, panels, balloons, AVB/BGB/BMP | Real assets and source geometry are used; no stick-figure or generic Qt replacement remains. |
| Join/NAMES/Starring | `ircsock.cpp`, `protsupp.cpp`, `panel.cpp` | JOIN, 353, 366, `bSingleJoin`, `CIUserJoin`, `AddStarsAux` | Members and stars are real parser/user/avatar data only. Empty before real data is the correct fallback. |
| UI shell | `mainfrm.*`, `chatview.*`, `spltchat.*`, `tabbar.*`, `chatbars.*`, `coolbar.*` | frame, MDI, menus, bars, splitters | Main shell and many commands are ported; exact menu/child/focus/close edges remain. |
| Dialogs/lists/admin | `admindlg.*`, `chanprop.*`, `motd.*`, `roomlist.*`, `userlist.*`, `whisprbx.*` | direct resource dialogs and protocol handlers | Major dialogs and lists are ported/tested; file transfer, sound, font dialog, and platform providers remain. |
| Rules/notifications/automation | `rules.*`, `actions.*`, `notif.*`, `notipage.*`, `autopage.*` | original rule/notification models and resources | Core/UI are ported; source-missing action dependencies remain disabled. |
| Text/input | `saywnd.*`, `rtfctrl.*`, `format.cpp`, `textcore.*`, `textview.*`, `txtfntdg.*` | RichEdit/input/text output | Formatting, input, text output, and printing core are ported; full font dialog, RTF stream, and some URL/DBCS edges remain. |

## Quick Lookup

| Need | Entry Point |
| --- | --- |
| App startup, Registry, menus, downloads, favorites | `chat.cpp`, `chat.h`, `mainfrm.*`, `setupdlg.cpp`, `proppage.*` |
| Servers, auth, connect/retry | `chatsrv.*`, `proppage.*`, `setupdlg.*`, `ircsock.*`, `protsupp.*` |
| Document, view switching, history, save/load | `chatdoc.*`, `chatview.*`, `pageview.*`, `panel.*` |
| IRC socket, parser, numeric replies | `ircsock.*` |
| IRC/IRCX commands, JOIN/PART/PRIVMSG/DATA | `ircproto.*`, `chatprot.h`, `protsupp.*` |
| Comic Chat annotations | `protsupp.cpp`, `defines.h`, `avatario.cpp` |
| Avatar formats AVB/BGB | `avbfile.*`, `avatar.*`, `avatario.*`, `dib.*` |
| Comic panel, balloons, layout | `panel.*`, `balloon.*`, `pageview.*` |
| Emotion/BodyCam | `bodycam.*`, `avatar.*`, `textpose.cpp`, `avatario.*` |
| Text mode, RichEdit, formatting | `textview.*`, `textcore.*`, `rtfctrl.*`, `format.cpp`, `saywnd.*` |
| Rules, macros, notifications | `rules.*`, `actions.*`, `autopage.*`, `notif.*`, `notipage.*` |
| URL detection/browser launch | `urlutil.*`, `format.cpp`, `rtfctrl.cpp` |
| OLE/ActiveX/DocObject | `bind*.cpp`, `bind*.h`, `mfcbind.*`, `base/*.idl`, `ic*.h` |

## Category Index

### App Shell, Document Model, MDI, And OLE

| File | Category | Purpose |
| --- | --- | --- |
| `chat.cpp` / `chat.h` | App/Shell | `CChatApp`, startup, command line, Registry/profile fields, fonts, status, menus, options, room/user lists, favorites, downloads, connection continuation, and modal helpers. |
| `mainfrm.*` | UI/Shell | `CMainFrame`, toolbar/statusbar, menu status help, window arrangement, palette/fixed colors, and child-window broadcasts. |
| `childfrm.*` | UI/Shell | MDI child frame, activation, layout, and per-room frame behavior. |
| `ipframe.*`, `bind*`, `mfcbind.*`, `oleobjct.cpp`, `chatitem.*` | OLE/DocObject | In-place frame, DocObject, IOle interfaces, command target, menu merge, server item, and embedded document behavior. |
| `guids.cpp`, `base/*.idl`, `ic*.h`, `imsconf2.h` | COM/OLE | GUIDs, IDL, generated headers, automation, and NetMeeting interfaces. |

### Document, Views, And Comic Pages

| File | Category | Purpose |
| --- | --- | --- |
| `chatdoc.*` | Document/UI/Protocol bridge | Pages, history, save/load, view switching, menus, admin/member commands, formatting, shortcuts/favorites, protocol forwarding. |
| `chatview.*` | UI/View | Comic/text/status splitter view, switching, print hooks, focus forwarding. |
| `pageview.*` | Comic/UI | Scrollable comic output, retained buffers, scroll/autofit, hit testing, printing, context menus, redraw. |
| `panel.*` | Comic/Layout | Panel/page lifecycle, speaker assignment, balloons/avatars, title/starring, page breaks, hotlinks. |
| `status.*`, `tabbar.*`, `spltchat.*`, `chatbars.*`, `coolbar.*`, `chicdial.*`, `dumbwnd.*` | UI support | Status view, tabs, splitters, toolbars, coolbar, dialog base, helper windows. |

### Comic Layout, Drawing, Geometry, Avatars, And Image I/O

| File | Category | Purpose |
| --- | --- | --- |
| `balloon.*`, `arc.cpp`, `bbox.*`, `vector2d.*`, `spline.*`, `splinutl.cpp`, `traj.*` | Comic geometry | Speech balloons, curves, bounding boxes, vectors, splines, trajectory logic. |
| `wmini.cpp`, `semantic.cpp`, `cache.cpp` | Excluded/legacy | Old or incomplete experiments not in canonical Modern product build. |
| `avatar.*`, `avatario.*`, `avbfile.*`, `bodycam.*`, `backdrop.*`, `dib.*`, `fonts.cpp`, `textpose.cpp` | Avatar/art/text emotion | AVB/BGB, BMP/DIB, avatar and body classes, emotion protocol, BodyCam, backdrops, comic fonts, and automatic text pose rules. |

### IRC, IRCX, User/Room State, And Network

| File | Category | Purpose |
| --- | --- | --- |
| `chatprot.h` | IRC abstraction | Virtual `CRoomInfo` API for room/protocol operations. |
| `ircproto.*` | IRC protocol | Outgoing IRC/IRCX commands, sending, encoding, modes, queries, IdentD, client data. |
| `ircsock.*` | IRC socket/parser | Socket, login/auth, parser, dispatch, numeric replies/errors, JOIN/PART/NAMES/MODE/PRIVMSG/DATA. |
| `protsupp.*` | IRC glue | User/member lifecycle, annotations, `ProcessSay`, slash commands, send/receive, ratings, reconnect. |
| `userinfo.*`, `roomlist.*`, `userlist.*`, `memblst.*`, `query.*` | IRC state/UI | Users, display info, lists, member UI, async query ownership. |
| `nmproto.*`, `mcithrd.*`, `webreq.*`, `dlylddll.c`, `chatsrv.*` | Adjunct/platform | NetMeeting, audio thread, WinInet downloader, delayed DLL, server configuration. |

### Text Input, RichEdit, Formatting, URL, Rules, And Notifications

| File | Category | Purpose |
| --- | --- | --- |
| `saywnd.*`, `rtfctrl.*`, `rtfcmb.*` | Text input/RichEdit | Say input, RTF formatting, controls, toolbar, IME/max text/paste, combo overlay. |
| `textcore.*`, `textview.*`, `whisprbx.*` | Text/Whisper UI | Text output core, text view, status-like views, printing, Whisper Box tabs and send logic. |
| `format.cpp`, `txtfntdg.*`, `histent.*`, `doskey.*` | Formatting/history | Format ranges, RTF/IRC conversion, URLs, font dialog, input/history containers. |
| `url.cpp`, `urlfind.cpp`, `urlutil.cpp` | URL | Active URL behavior comes from build-selected `urlutil` and callers; legacy URL files are excluded unless proven active. |
| `intl.c/.h`, `jis2sjis.cpp`, `sjis2jis.cpp` | I18N | DBCS, charset, line breaking, and Japanese conversion. |
| `rules.*`, `actions.*`, `autopage.*`, `notif.*`, `notipage.*`, `script.*` | Rules/automation/notifications | Rule model/actions/UI, macros, notification model/UI, and small script state. |

### Dialogs, Settings, Utilities, Build, And Resources

| File | Category | Purpose |
| --- | --- | --- |
| `setupdlg.*`, `proppage.*`, `chanprop.*`, `admindlg.*`, `bothdlg.*`, `motd.*`, `filesend.*`, `sounddlg.*`, `colordlg.*` | Dialogs/settings | Connect/options pages, room dialogs, admin dialogs, MOTD/Away, file transfer, sound picker, color picker. |
| `utils.*`, `ccommon.cpp`, `ccomp.cpp`, `cllist.cpp`, `defines.h`, `dpiscale.h`, `ui.h`, `pe.h`, `mschat.h`, `safectype.h` | Utilities/core | Platform helpers, shared wrappers, constants, DPI, UI, DIB/PE-adjacent definitions, COM helpers. |
| `resource.h`, `helpids.h`, `chat.rc`, `cchat.rcv`, `chatver.*`, `chat.mak`, `stdafx.*` | Build/resources | Resource IDs, Help IDs, dialog/menu/string/icon/bitmap resources, version resources, legacy project file, MFC PCH inputs. |
