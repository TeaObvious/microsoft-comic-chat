// Ported byte-for-byte in ordering from artifacts/inc/msgtype.h, selected by
// v2.5-beta-1-modern/textcore.cpp.

#pragma once

enum MSG_TYPE
{
    mtNormal = 0,
    mtWhisper = 1,
    mtThought = 2,
    mtBroadcast = 3,
    mtAction = 4,
    mtPrivate,
    mtExChan,
    mtJoin,
    mtLeave,
    mtAppearsAs,
    mtBackground,
    mtChr,
    mtGetInfo,
    mtStatusChange,
    mtAliasChange,
    mtTopicChange,
    mtKicked,
    mtGetRealname,
    mtEndEnum
};

constexpr MSG_TYPE mtBeginActions = mtJoin;
constexpr MSG_TYPE mtBeginInfo = mtGetInfo;
constexpr MSG_TYPE mtURL = mtEndEnum;

enum MEMBER_STATUS
{
    msHost,
    msParticipant,
    msSpectator,
    msRoom,
    msEndEnum
};

constexpr short g_nHost = 0x01;
constexpr short g_nParticipant = 0x02;
constexpr short g_nSpectator = 0x04;
constexpr short g_nNoWhisper = 0x08;
constexpr short g_nIgnored = 0x10;
constexpr short g_nJoining = 0x20;
constexpr short g_nLeaving = 0x40;
constexpr short g_nMe = 0x80;
constexpr short g_nStatMin = g_nHost;
constexpr short g_nStatMax = g_nMe + g_nLeaving + g_nIgnored
    + g_nNoWhisper + g_nSpectator;
