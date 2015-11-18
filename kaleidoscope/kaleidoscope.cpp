#include <stdio.h>

ISSPACE(C) (C == (int)' ')
ISALPHA(C) (('a' <= C && C <= 'z') || ('A' <= C && C <= 'Z'))
ISNUM(C)   ('0' <= C && C <= '9')
ISALNUM(C) (ISALPHA(C) || ISNUM(C))

enum Token {
    tok_eof = -1,

    // commands
    tok_def    = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number     = -5,
};

static std::string IdentifierStr;
static double NumVal;

static int gettok()
{
    static int LastChar = ' ';

    // skip any whitespace.
    while (ISSPACE(LastChar))
        LastChar = getchar();

    if (ISALPHA(LastChar)) {
        IdentifierStr = LastChar;
        for (;;) {
            LastChar = getchar();
            if (ISALNUM(LastChar)) {
                
            } else {
                break;
            }
        }
    }
}
