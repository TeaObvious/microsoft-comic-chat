#include "ccomp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)
}

int main()
{
    REQUIRE(bMatchAll("*", 1));
    REQUIRE(bMatchAll("????*", 5));
    REQUIRE(!bMatchAll("????", 4));

    REQUIRE(bIsMaskCompare("?usti?", 6, "Justin", 6));
    REQUIRE(bIsMaskCompare("*.microsoft.com", 15,
                           "chat.microsoft.com", 18));
    REQUIRE(!bIsMaskCompare("*.microsoft.com", 15,
                            "microsoft.net", 13));
    REQUIRE(bIsMaskCompare("A*?D", 4, "abcd", 4));

    PRUSERMATCH match;
    REQUIRE(bGetUserMatchFromMask(
        "?usti?!regisb@*.microsoft.com", &match));
    REQUIRE(match.cbNickname == 6);
    REQUIRE(match.cbUserName == 6);
    REQUIRE(match.cbIPAddress == 15);
    REQUIRE(std::strncmp(match.szNickname, "?usti?", match.cbNickname) == 0);
    REQUIRE(std::strncmp(match.szUserName, "regisb", match.cbUserName) == 0);
    REQUIRE(std::strncmp(match.szIPAddress, "*.microsoft.com",
                         match.cbIPAddress) == 0);
    REQUIRE(bIsMatch(&match, "Justin", "REGISB", "chat.microsoft.com"));
    REQUIRE(!bIsMatch(&match, "Justin", "regisb", "microsoft.net"));

    PRUSERMATCH quoted;
    REQUIRE(bGetUserMatchFromMask("Justin!*@*", &quoted));
    REQUIRE(bIsMatch(&quoted, "'Justin", "user", "host"));
    REQUIRE(bGetUserMatchFromMask("'Justin!*@*", &quoted));
    REQUIRE(bIsMatch(&quoted, "Justin", "user", "host"));

    PRUSERMATCH all;
    REQUIRE(bGetUserMatchFromMask("*!*@*", &all));
    REQUIRE(!all.szNickname && !all.szUserName && !all.szIPAddress);
    REQUIRE(bIsMatch(&all, "", "", ""));

    PRUSERMATCH copied = match;
    match.m_maskStorage.fill('x');
    REQUIRE(bIsMatch(&copied, "Justin", "regisb", "chat.microsoft.com"));
    return 0;
}
