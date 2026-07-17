#include "defines.h"

static_assert(SB_TOOLBAR == 1);
static_assert(SB_STATUSBAR == 2);
static_assert(SB_TOOLBAR_MAIN == 4);
static_assert(SB_TOOLBAR_MEMBER == 8);
static_assert(SB_TOOLBAR_TEXT == 16);
static_assert(SB_TOOLBAR_ANY == 28);
static_assert(SB_TOOLBAR_OLDREAD == 32);

static_assert(CX_DISCONNECTED == 0);
static_assert(CX_INCHANNEL == 1);
static_assert(CX_CONNECTING == 2);
static_assert(CX_NOCHANNEL == 3);
static_assert(CX_CONNECTED == 4);

static_assert(SM_SAY == 1);
static_assert(SM_WHISPER == 2);
static_assert(SM_THINK == 3);
static_assert(SM_SHOUT == 4);
static_assert(SM_ACTION == 5);

static_assert(BM_SAY == 0x0001);
static_assert(BM_WHISPER == 0x0002);
static_assert(BM_THINK == 0x0004);
static_assert(BM_ACTION == 0x0008);
static_assert(BM_SOUND == 0x0010);
static_assert(BM_AWAY == 0x0020);
static_assert(BM_HERESINFO == 0x0040);
static_assert(BM_NOFORMAT == 0x0080);
static_assert(BM_EXCHAN == 0x0100);

static_assert(MT_CHANNELSEND == 0x01);
static_assert(MT_PRIVATEMSG == 0x02);
static_assert(MT_WHISPER == 0x04);
static_assert(MT_PRVMSG == 0x08);
static_assert(MT_NOTICE == 0x10);
static_assert(MT_DATA == 0x20);
static_assert(MT_DATAREQUEST == 0x40);
static_assert(MT_DATAREPLY == 0x80);

static_assert(CM_PRIVATE == 1);
static_assert(CM_HIDDEN == 2);
static_assert(CM_INVITEONLY == 4);
static_assert(CM_TOPICHOST == 8);
static_assert(CM_NOEXTERN == 16);
static_assert(CM_MODERATED == 32);
static_assert(CM_USERLIMIT == 64);
static_assert(CM_CHANNELKEY == 128);
static_assert(CM_NOFORMAT == 256);
static_assert(CM_MIC == 512);

static_assert(MAX_INPUTLEN == 350);
static_assert(MAX_TOPICLEN == 94);
static_assert(MAX_ANNOTATIONS == 256);
static_assert(MAX_COMMAND == 128);
static_assert(MAX_TOKEN == 201);
static_assert(MAX_NICK == 51);

int main()
{
    return 0;
}
