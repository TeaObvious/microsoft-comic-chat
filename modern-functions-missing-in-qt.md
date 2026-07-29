# Modern-Funktions- und Deferred-Inventur im Qt-Port

Stand: 2026-07-28
Abgeglichen gegen `v2.5-beta-1-modern/`, den aktuellen Qt-Arbeitsstand und
den Vertrags-/Abnahmestand in `source-overview.md`.

## Zweck und Zählregeln

Diese Datei ist eine Funktionslücken- und Deferred-Inventur, keine rohe
Signaturliste. `v2.5-beta-1-modern/` bleibt die autoritative Produktbasis.
`v2.5-beta-1/` darf nur für den in `source-overview.md` dokumentierten
Provenienz-Audit verwendet werden; Alt-only-Funktionen zählen nicht als
Qt-Anforderung.

Eine Funktion gilt nur dann als fehlend, wenn der kanonische Modern-Build oder
sein aktiver Ressourcen-/Aufrufpfad ein Verhalten definiert und der Qt-Port
dieses Verhalten nicht ausführt. Ein abweichender C++-Funktionskopf, ein
fehlender MFC-Handlername oder eine nicht vorhandene Win32-Hilfsdatei genügt
nicht. MFC-Nachrichten, Registry, GDI, Winsock und ähnliche Plattformmechanik
dürfen unter Qt anders aussehen, solange Verhalten, Reihenfolge und Daten
source-identisch bleiben.

Verwendete Status:

| Status | Bedeutung |
| --- | --- |
| **Abgeschlossen** | Source-definiertes Verhalten ist implementiert und regressionsgetestet. |
| **Offen** | Aktives, nicht deferred Modern-Verhalten fehlt oder ist nur teilweise vorhanden. |
| **Deferred** | Bewusst zurückgestellte, überwiegend Windows-spezifische Integration; nicht durch einen Qt-Ersatz erfinden. |
| **Source-unbestimmt** | Modern definiert selbst keine vollständige Semantik; ohne neue Source-Evidenz nicht „reparieren“. |
| **Plattformmechanik** | Verhalten ist portiert, aber ein Win32-Detail besitzt kein identisches portables Qt-Pendant. |
| **X / ausgeschlossen** | Datei wird vom kanonischen Modern-Ziel nicht gebaut und begründet keine Qt-Funktion. |

Der frühere Stand vom 2026-07-19 leitete aus Regex-Unterschieden 1.404
Datei-/Symbolpaare und Paketgrößen wie 312 oder 199 „fehlende Funktionen“ ab.
Diese Zahlen sind verworfen:

- 682 der gemeldeten Namen stehen bereits in der jeweiligen Qt-Datei.
- Mehrdeutige Namen wie `Draw`, `Add` und `OnInitDialog` verloren Klasse,
  Parameter und Überladung.
- Konstruktoren sowie Tokens wie `BOOL`, `CString` und `HRESULT` wurden
  fälschlich als Funktionen gezählt.
- `.c`, IDL, Ressourcen, Buildauswahl und aktive Aufrufpfade fehlten ganz.
- Die frühere DCC-Zahl enthielt fachfremde JOIN-, MODE-, IdentD- und
  Statusfunktionen.

## Kurzstatus

| Bereich | Aktueller Stand |
| --- | --- |
| Main UI, Menüs, Accelerators, Toolbars, Status, MDI, Fokus und Command UI | **Abgeschlossen** für den vollständigen nicht-deferred Umfang. |
| Options/Settings, Profile, Comic/Text Fonts sowie Room/User Lists | **Abgeschlossen** für den dokumentierten kanonischen Dialogblock. |
| Conversation-Dateien, Child-Frame-Persistenz und sichtbare Druckpfade | **Abgeschlossen**; COM-`IPrint` bleibt eine getrennte Deferred-Grenze. |
| DCC/CTCP `SEND` | **Abgeschlossen** in `src/original/filesend.cpp/.h`; nicht mehr als fehlende Datei oder Priority-4-TODO führen. |
| Textdatei-Regelaktionen, `rtfcmb.*` und Art-Server-Hinweis | **Abgeschlossen** und regressionsgetestet. |
| Tatsächliche nicht-deferred Restlücken | Keine im verifizierten kanonischen Modern-Umfang. |
| Deferred Plattformintegrationen | IdentD, MCI/Sound, WinInet/Art-Download, SSPI Auth 2/3, NetMeeting, COM/OLE/DocObject/externe Automation, WinHelp sowie Windows Shell/Favorites. |
| Gesamt-Parität | Vollständige nicht-deferred Parität ist durch den abschließenden Gesamt-Gate belegt; Deferred-, source-unbestimmte und Plattformgrenzen bleiben ausdrücklich ausgenommen. |

## 1. Abgeschlossene letzte nicht-deferred Funktionen

### 1.1 Regelaktionen „Send File Line“ und „Whisper File Line“

Originale Evidenz:

- `actions.cpp::bSendOrWhisperFileLine`
- `actions.cpp::bExecuteAction` für `aSendFileLine` und
  `aWhisperFileLine`
- `autopage.cpp::AddTextFileToComboBox`
- `autopage.cpp`-Aufruf von `EnumFiles` für rekursive `*.txt`-Aufzählung
- die vorhandenen Action-/Parameterdefinitionen für `ptTextFileName` und
  Zeilenbereich beziehungsweise `g_szRandomLine`

Modern öffnet eine relative Textdatei unter `m_strBaseDir`, liest die
angegebenen Zeilen oder eine Zufallszeile und sendet jede nichtleere Zeile
entweder in den Zielraum oder als Whisper an die source-definiert aufgelösten
Empfänger. Das ist kein DCC-Dateitransfer und hängt nicht von `filesend.*` ab.

Qt-Zustand:

- `bExecuteAction` dispatcht beide Aktionen über den original benannten
  `bSendOrWhisperFileLine`.
- Relative Pfade, gespeicherte Backslashes, case-insensitive Dateiauflösung,
  ACP-Text, Leerzeilenzählung, explizite und umgekehrte/überlappende Bereiche,
  der originale `RND`-Restzeilen-/Rewind-Ablauf sowie der source-definierte
  „handled“-Rückgabewert für fehlende Dateien und ungültige Laufzeitbereiche
  sind implementiert.
- Raumziele werden über das tatsächliche Zieldokument gesendet. Whisper
  behalten Semikolon-/Nick-/Ident-Auflösung, IRCX-Encoding,
  Self-/Duplikatfilter und Room/Whisper-Box-Routing pro Zeile.
- `CEditRule` bietet beide Aktionen an. `ptTextFileName` wird rekursiv unter
  `m_strBaseDir` gefüllt, überspringt Punktverzeichnisse, behandelt `.txt`
  case-insensitiv, erzeugt source-identische relative Backslash-Namen und
  schließt `Readme`, `License` und `Support` aus.

Status: **Abgeschlossen**. Gezielte Regressionen decken Ranges, Random,
Leerzeilen, fehlende/ungültige Dateien, Cross-Room- und Whisper-Routing,
Validierung und rekursive Inventur ab. Der Pfad bleibt ausdrücklich getrennt
von DCC `filesend.*`.

### 1.2 `rtfcmb.cpp/.h`: RichEdit-Combo in der Automation-Regel-UI

Originale Evidenz:

- `CRtfCmb` und `CRtfCmbEdit`
- `bSetRtfMode`, `bAttachRtfCtrl`, `RedirectFocus`,
  `RedirectSelection`
- `OnDropDown`, `OnCloseUp`, `OnEditChange`, `OnSelEndOK`,
  `OnShowWindow`
- Verwendung durch `CEditRule::m_cmbEvents`, `m_cmbActions` und
  `m_cmbActionParams`

Modern legt bei RTF-Parametern ein `CRtfCtrl` über den Edit-Bereich, lässt den
Combo-Pfeil sichtbar und synchronisiert Dropdown-Auswahl, Text,
Default-Format, Markierung und Fokus.

Qt-Zustand:

- `src/original/rtfcmb.cpp/.h` existieren und sind im Qt-Ziel eingebunden.
- Der formatierte einzeilige Editor liegt nur über dem Combo-Editfeld; Pfeil
  und Dropdown bleiben sichtbar. Text, Dropdown-Auswahl, Defaultformat,
  Markierung, Sichtbarkeit, Geometrie, Enabled/Font/Palette, Fokus und
  Mode-Teardown werden synchronisiert.
- `CEditRule` verwendet die Grenze für Events, Actions und normale
  Action-Parameter, behält den source-definierten Tab-Ablauf, aktualisiert
  Keywordlisten bei Eventwechsel ohne formatierten Text zu löschen und
  verwendet für Serverparameter weiterhin die eigene Service-Combo.

Status: **Abgeschlossen**. Direkte Modul- und integrierte Automation-Tests
decken Overlay/Pfeil, Einzeilen-/128-Zeichen-Grenze, Dropdown, Format,
Selection, Visibility, Fokus und Tabreihenfolge ab.

### 1.3 Einmaliger Modern-Hinweis auf abgeschaltete Art-Server

Originale Evidenz:

- `chat.cpp::NoteArtServersGoneOnce`
- Aufruf am Anfang von `CChatApp::StartDownloadingAvatar`
- Aufruf am Anfang von `CChatApp::StartDownloadingBackdrop`

Modern zeigt einmal pro Prozess einen Informationsdialog und kehrt danach wie
beabsichtigt ohne Netzwerkdownload zurück.

Qt-Zustand:

- Beide frühen Returns rufen denselben `NoteArtServersGoneOnce`-Helfer auf.
- Ein gemeinsamer funktionslokaler Guard zeigt den exakten Modern-Text mit
  Applikationstitel, Information-Icon und OK genau einmal pro Prozess.
- Avatar-Flags werden erst nach dem Hinweis gelöscht; Backdrop kehrt danach
  unverändert mit `FALSE` zurück.

Status: **Abgeschlossen**. Der Regressionstest belegt exakten Dialoginhalt,
Flag-Effekt und den geteilten Once-Zustand über Avatar und Backdrop. Weder
`webreq.*` noch ein anderer Downloader wurden aktiviert.

## 2. Abgeschlossene Pakete, die nicht mehr als fehlend zählen

| Früher als offen geführt | Belegter aktueller Zustand |
| --- | --- |
| Menüs/Submenus/Accelerators/Toolbars/Statusbar/Child-Frame/Command UI | Ressourcenstruktur, Besitzer, Enable-/Check-Regeln, Fokus, MDI und nicht-deferred Kommandos sind portiert und getestet. |
| Settings/Profile/Comic Fonts/Room/User Lists | Kanonische Seiten und Dialoge, Validierung sowie Apply/Cancel sind portiert und getestet. |
| Conversation-Datei/Child-Frame-Persistenz/Druck | `.ccc`, `.ccr`, `.rtf`, Dokument-Lifecycle, `F1_MAXMDI` und sichtbare Comic/Text/Status-Druckpfade sind abgeschlossen. |
| DCC/CTCP-SEND | `filesend.cpp/.h`, `ID_SEND_FILE`, Angebot/Parser, Sender/Empfänger, signed ACKs, Timeouts, Dialoge und Cleanup sind implementiert und getestet. |
| `filesend.cpp/.h` als fehlende Dateien | Falsch und entfernt: beide Dateien existieren unter `v2.5-beta-1-qt/src/original/` und `filesend.cpp` ist im CMake-Ziel. |
| Textdatei-Regelaktionen | `bSendOrWhisperFileLine`, beide Action-Dispatches, source-exakte Range-/Random-/Routing-Semantik und rekursive `ptTextFileName`-Inventur sind implementiert und getestet; kein DCC. |
| `rtfcmb.cpp/.h` | Original benannte Combo-/Edit-Grenze, Automation-Kopplung, Dropdown, Format, Selection, Visibility, Fokus und Tabfolge sind implementiert und getestet. |
| Art-Server-Hinweis | Exakter Modern-Dialog mit einem geteilten processweiten Once-Guard ist an beiden beabsichtigten No-download-Returns implementiert und getestet. |

## 3. Deferred Plattformintegrationen

Diese Einträge sind dokumentierte Grenzen, keine Erlaubnis für generische
Qt-Ersatzfunktionen.

### 3.1 IdentD

| Originaldateien | Zentrale Symbole | Qt-Zustand |
| --- | --- | --- |
| `ircproto.cpp/.h`, Aufrufe in `protsupp.cpp` und `ircsock.cpp` | `CIdentdSocket`, `g_identd`, `g_ident0`, `StartIdentD`, `StopIdentD`, `CIdentdSocket::OnAccept`, `OnReceive` | Kein Listener auf Port 113 und keine Start/Stop-Aufrufe. WHOIS-/Identity-Anzeige ist davon unabhängig portiert. |

Status: **Deferred**. Keinen lokalen Identity-Daemon oder erfundene Antwort
hinzufügen.

### 3.2 MCI/Sound

| Originaldateien | Zentrale Symbole | Qt-Zustand |
| --- | --- | --- |
| `mcithrd.cpp/.h` | `CMciPlaybackThread`, `CMciPlaybackWnd`, `Play`, `Stop`, `WaitForCompletion`, `OnMCINotify` | Dateien und MCI-Thread fehlen. |
| `sounddlg.cpp/.h` | `CSoundList`, `CSoundDlg`, `bTryToPlaySound`, `bFindAndPlaySound` | Dialog, Enumeration, Test und Playback fehlen. |
| `utils.cpp/.h` | `sndPlayMidiSound` | Fehlt. |
| `protsupp.cpp/.h`, `saywnd.cpp`, `chat.cpp`, `actions.cpp`, `whisprbx.cpp` | `PrepareSound`, `bChatSendSound`, `StrGetSoundAction`, `CSayWnd::OnPlaySound`, `CChatApp::OnSoundsOff`, `aPlaySound`, `aSendSound` | Empfang kann den source-definierten Text-/Annotationspfad erreichen; Wiedergabe und Versand bleiben deaktiviert. Persistierte Soundwerte haben keinen Playback-Effekt. |

Status: **Deferred**. `ID_PLAY_SOUND`, `ID_WHISPER_SOUND`,
`ID_TURN_OFF_SOUNDS` und Sound-Regelaktionen bleiben ohne Ersatzwirkung.

### 3.3 WinInet und Art-Download

| Originaldateien | Zentrale Symbole | Qt-Zustand |
| --- | --- | --- |
| `webreq.cpp/.h` | `CInternetRequest`, `CChatFileRequest`, `CInternetRequestor`, `IsAvailable`, `AddToWorkQueue`, `ThreadWorker` | Kein Requestor/WinInet-Worker. |
| `chat.cpp/.h` | `StartDownloadingAvatar`, `StartDownloadingBackdrop`, `HandleDownloadedFiles`, `AvatarTransferError`, `m_pNetRequestor`, `m_mapDownloads` | Moderns beabsichtigtes frühes No-download-Verhalten und der einmalige gemeinsame Hinweis sind erhalten; Requestor, Download und Netzinstallation bleiben deferred. |
| `backdrop.cpp`, `userinfo.cpp`, `protsupp.cpp`, `chatdoc.cpp`, `panel.cpp` | Download-/Installationskopplungen | Lokale Original-Art-Packs funktionieren; keine Netzinstallation. |

Status: **Deferred**. Modern selbst erreicht den alten WinInet-Code hinter den
frühen Returns nicht mehr. Auto-Download-Optionen bleiben kompatible
Persistenzwerte ohne Netzwerkeffekt.

### 3.4 SSPI / IRCX Auth 2 und 3

| Originaldateien | Zentrale Symbole | Qt-Zustand |
| --- | --- | --- |
| `ircsock.cpp/.h` | `HrIrcXLogin`, `HrAuthenticate`, `HrGenerateAndSendAuthMsg`, `CloseSSPI`, `SetAuthentication`, `cmdidAuth`, Credential-/Context-Zustand | Keine Security-DLL, SSPI-Funktionstabelle, Credentials oder Token-Aushandlung. `AUTH` wird erkannt und unterdrückt, aber nicht lokal als Erfolg behandelt. |
| `chatsrv.cpp/.h` | `authtypeServerPackages = 2`, `authtypeCustomPackages = 3`, Paketlisten und Settings | Typen und Roundtrip bleiben erhalten; Auth 0 und Plaintext/OPER Auth 1 sind portiert. |

Status: **Deferred**. Paket-Reihenfolge, ANON-Fallback und originaler
Fehler-/Disconnect-Pfad bleiben erhalten; kein erfundener Auth-Erfolg.

### 3.5 NetMeeting

| Originaldateien | Zentrale Symbole | Qt-Zustand |
| --- | --- | --- |
| aktiver Rand in `protsupp.cpp/.h` und `chatdoc.cpp/.h` | `bNMInstalled`, `ConfConnect`, `ConfListenThread`, `DoNetMeetingCX`, `ChatStartNetMeeting`, `OnStartNetmeeting`, CTCP `NETMEET` | Aufrufpfad fehlt; sichtbare Kommandos sind deferred/disabled. Eingehendes `NETMEET` wird source-getreu erkannt und ohne Ersatzwirkung unterdrückt. |
| `nmproto.cpp/.h`, `imsconf2.h`, `base/imsconf2.idl` | `CNmProto`, `CCb32CoreNotify` und Conference-Schnittstellen | `nmproto.*` ist bereits vom Modern-Build ausdrücklich ausgeschlossen und zählt nicht als zusätzliche Qt-Lücke. |
| `chat.cpp/.h`, Settings | `m_bAcceptNMCalls` | Reiner Persistenzwert ohne Call-Effekt. |

Status: **Deferred**. `CLIENTINFO` nennt weiterhin source-getreu `NETMEET`;
kein Launcher, Paketprobe oder Ersatz-Call.

### 3.6 COM/OLE/DocObject und externe Automation

| Originaldateien | Zentrale Symbole / Schnittstellen | Qt-Zustand |
| --- | --- | --- |
| `binddoc.cpp/.h`, `binddcmt.cpp`, `bindview.cpp`, `bindtarg.cpp`, `oleobjct.cpp` | `CDocObjectServerDoc`, `IOleObject`, `IOleDocument`, `IOleDocumentView`, `IOleCommandTarget` | Keine COM-Aktivierung oder In-place-Einbettung. |
| `bindipfw.cpp/.h`, `ipframe.cpp/.h`, `binditem.cpp/.h`, `chatitem.cpp/.h`, `mfcbind.cpp/.h` | In-place Frame, Shared Menu, Server Item und Binder-Helfer | Fehlen als Windows-spezifische Integration. |
| `bindauto.cpp`, `base/icchat.idl`, erzeugtes `icchat.h`/`icchat_i.c` | `IDispatch`, `ICChatAutomation`, Dispatch-/Menümethoden | Externe COM-Automation fehlt. |
| `print.cpp` | `CDocObjectServerDoc::XPrint` / `IPrint` | Modern liefert aus allen Produktmethoden `E_NOTIMPL`; sichtbares Drucken liegt in den View-Modulen und ist portiert. |
| `chat.cpp`, `chatdoc.cpp/.h` | `AfxOleInit`, `COleTemplateServer`, `/Embedding`, `/Automation`, `OnGetEmbeddedItem`, Compound Storage | CFB-Magic wird bewusst abgewiesen; der flache Qt-Adapter behauptet keine OLE-Kompatibilität. |

Status: **Deferred**. Das portierte `CAutomationPage`-/Regel-/Makro-UI ist
nicht die fehlende externe `IDispatch`-Automation.

### 3.7 WinHelp und kontextsensitive Hilfe

| Originaldateien | Zentrale Symbole | Qt-Zustand |
| --- | --- | --- |
| `chat.cpp/.h`, `ipframe.cpp/.h` | `CChatApp::OnHelpTopics`, `CInPlaceFrame::OnHelpTopics` | `ID_HELP_TOPICS` ist deferred/disabled. |
| `chicdial.cpp/.h`, `helpids.h`, Dialog-`m_nHelpIDs` | `CCSDialog::OnHelp`, `OnHelpContextMenu`, entsprechende Property-Page/-Sheet-Pfade | Keine `WinHelp`, `HELP_WM_HELP`- oder `HELP_CONTEXTMENU`-Route. |

Status: **Deferred**. Die portierten Online-URL-Kommandos sind eigenständige
aktive Modern-Kommandos und kein WinHelp-Ersatz.

### 3.8 Windows Shell, Desktop und Favorites

| Originaldateien | Zentrale Symbole | Qt-Zustand |
| --- | --- | --- |
| `chat.cpp/.h` | `GetDesktopOrFavorites`, `OnFavoritesOpenfavorites`, `AddFavoritesFromDirectory`, `AddFavoritesToMenu`, `OnFavorite`, `FavoriteMonitorFunc`, `m_arrFavorites` | Keine Shell-Folder-Ermittlung, dynamischen Favorite-IDs oder Verzeichnisüberwachung. |
| `chatdoc.cpp/.h` | `OnFileCreateshortcut`, `OnFavoritesAddtofavorites`, `SaveShortcut` | Sichtbare Shortcut-/Favorites-Kommandos sind disabled; der vorhandene flache Save-Helfer wird von keinem Shell-Kommando aufgerufen. |
| `setupdlg.cpp`, `utils.cpp/.h` | Favorites-Verzeichnis, `.ccr`-Enumeration, Shell-/Browse-Helfer | Favorites-Feld bleibt deaktiviert; kompatible Pfadwerte werden nur persistiert. |
| `chat.cpp::OnHelpReleaseNotes`, `OnIrcChat` | `ShellExecute` | Release Notes ist disabled; normaler URL-Start über `FLaunchBrowser` bleibt getrennt aktiv. |

Status: **Deferred**. Kein generisches Bookmark-, Desktop-Shortcut- oder
Dateiwächter-Modell hinzufügen.

### 3.9 Zusätzlicher Windows-Provider: Content Advisor / PICS

Originale Symbole `bCanViewUnrated`, `bRatingsEnabled`, `bPassesRatings` und
die `msrating`-Thunks in `dlylddll.c` benutzen den Windows Ratings Provider.
Qt bewahrt den Modern-Fallback „Ratings-DLL fehlt = Ratings deaktiviert“:
unbewertete Inhalte dürfen passieren; nichtleere PICS-Werte werden ohne
Provider nicht optimistisch freigegeben.

Status: **Deferred Plattformprovider**, keine lokale Rating-Entscheidung
erfinden.

## 4. Fehlende Dateinamen korrekt klassifiziert

Ein fehlender Dateiname ist nicht automatisch eine fehlende Produktfunktion.

### 4.1 Keine verbleibende nicht-deferred Dateilücke

`rtfcmb.cpp/.h` existieren nun unter `src/original/`, sind im kanonischen
Qt-Ziel eingebunden und regressionsgetestet.

### 4.2 Kanonisch gebaute, aber bewusst deferred Plattformmodule

- COM/OLE/DocObject/Automation:
  `bindauto.cpp`, `binddcmt.cpp`, `binddoc.cpp`, `binddoc.h`,
  `bindipfw.cpp`, `bindipfw.h`, `binditem.cpp`, `binditem.h`,
  `bindtarg.cpp`, `bindview.cpp`, `chatitem.cpp`, `chatitem.h`,
  `ipframe.cpp`, `ipframe.h`, `mfcbind.cpp`, `mfcbind.h`,
  `oleobjct.cpp`
- MCI/Sound: `mcithrd.cpp`, `mcithrd.h`, `sounddlg.cpp`, `sounddlg.h`
- WinInet: `webreq.cpp`, `webreq.h`
- WinHelp-Anteil/Wrapper: `chicdial.cpp`, `chicdial.h`, `helpids.h`,
  `mschat.h`
- COM-Erzeugnisse: `base/icchat.idl`,
  erzeugtes `icchat.h`/`icchat_i.c`

`print.cpp` gehört technisch zum COM-Block, definiert aber nur die
`E_NOTIMPL`-`IPrint`-Oberfläche. Es ist keine fehlende sichtbare
Druckfunktion.

`dlylddll.c` muss symbolweise eingeordnet werden: Seine
MSRATING-/MSCONF- und alten WinInet-Thunks bleiben deferred. Der
`InternetCanonicalizeUrl`-Thunk unterstützt dagegen den aktiven URL-Pfad;
dessen Verhalten ist portiert. Die Datei ist deshalb nicht als Ganzes
deferred.

### 4.3 Reine Build-/MFC-/Adapterdateien ohne eigene Produktfunktion

- `stdafx.cpp`
- `stdafx.h`
- `dpiscale.h`
- `safectype.h`
- `ui.h`

Ihre relevanten Modern-Effekte sind über Qt-/Compilermechanik oder die
source-benannten Besitzer abgedeckt. Die bloßen Dateinamen werden nicht
nachgebaut.

### 4.4 Vom kanonischen Modern-Ziel ausgeschlossene `X`-Dateien

- `bothdlg.cpp`
- `bothdlg.h`
- `cache.cpp`
- `cllist.cpp`
- `dumbwnd.cpp`
- `dumbwnd.h`
- `guids.cpp`
- `nmproto.cpp`
- `nmproto.h`
- `script.cpp`
- `script.h`
- `semantic.cpp`
- `url.cpp`
- `url.h`
- `urlfind.cpp`
- `wmini.cpp`

Diese Dateien bleiben gelesen/indexiert, aber liefern ohne aktiven
Modern-Aufrufpfad keine Qt-Anforderung. Der aktive URL-Kern liegt in
`urlutil.*`; die aktive Comic-Geometrie liegt in `balloon.*`, `panel.*` und
den gebauten Geometriemodulen.

### 4.5 Erzeugte oder externe Schnittstellenheader

- `icbcore.h`
- `icchat.h`
- `imsconf2.h`

Sie gehören zu den dokumentierten COM-/NetMeeting-/externen
Schnittstellengrenzen und sind keine eigenständigen Qt-Produktmodule.

## 5. Source-unbestimmte oder bewusst sichere Grenzen

Diese Punkte dürfen nicht als normale fehlende Funktion implementiert werden:

| Grenze | Klassifikation |
| --- | --- |
| Mehrseitige Comic-Anordnung | `CChatDoc::AddNewPage` setzt jede Seite auf `(0,0)` und hinterlässt die Positionsberechnung als TODO. Qt bewahrt Überlagerung; künstliche Stapelabstände wären erfunden. |
| `ID_VIEW_MACROS` | Resource/Accelerator existiert, Modern hat aber keinen Handler. Qt lässt das Kommando als `NoHandler` inert. |
| Print Preview | Modern deaktiviert es ausdrücklich über `OnUpdateFilePrintPreview`; nicht durch eine Qt-Vorschau ersetzen. |
| DCC-Angebot ohne positive Größe | Modern promptet zwar, läuft danach aber in eine unsichere negative Schreiblänge. Qt erlaubt den Dialogpfad, schreibt jedoch keine Datei und startet keinen erfundenen EOF-Transfer. |
| DCC-Win32-Share-Modi | `FILE_SHARE_READ` beziehungsweise Share-mode zero sind mit portablem `QFile` nicht identisch ausdrückbar; Open-before-offer, Delayed Truncate und Dateierhalt sind umgesetzt. |
| Druckerauflösung `DMRES_MEDIUM` | Symbolische Windows-Treiberzuordnung besitzt keinen source-definierten numerischen Qt-Wert. |
| Arrange-Icons-Geometrie | Modern delegiert die konkreten Metriken an MFC; Qt darf nur Frameworkmechanik verwenden. |
| Quoted SoundPath | Der Modern-Zweig definiert keine brauchbare Transformation. Qt bewahrt den vollständigen Wert unverändert. |
| `CUser::operator==` | Nur deklariert und im Original ungerufen; keine Semantik erfinden. |

## 6. Verifikations- und Übergabestand

- Der aktuelle Arbeitsstand baut frisch in Debug und Release.
- In beiden Konfigurationen bestanden alle 45 CTests.
- Beide Binärdateien blieben in isolierten Offscreen-Starts mit leerer
  Konfiguration über die vollständige Fünf-Sekunden-Prüfzeit aktiv; der
  erwartete Timeout-Code war jeweils 124.
- `git diff --check`, Added-Line-Dummy-Content-Scan und absoluter
  Workspace-/Buildpfad-Scan sind sauber.
- Es bestehen keine aktiven nicht-deferred Modern-Lücken. Künftige Arbeit darf
  nur durch neue kanonische Modern-Evidenz oder eine ausdrückliche Änderung des
  Deferred-Umfangs eröffnet werden. Deferred, source-unbestimmte und
  Plattformgrenzen bleiben unverändert und dürfen nicht stillschweigend durch
  Qt-Ersatzverhalten gefüllt werden.
