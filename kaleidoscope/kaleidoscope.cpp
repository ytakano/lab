#include <stdio.h>

ISSPACE(C) (C == (int)' ')
ISALPHA(C) (('a' <= C && C <= 'z') || ('A' <= C && C <= 'Z'))
ISDIGIT(C) ('0' <= C && C <= '9')
ISALNUM(C) (ISALPHA(C) || ISDIGIT(C))

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
static int CurTok;

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
                IdentifierStr += LastChar;
            } else {
                break;
            }
        }

        if (IdentifierStr == "def")
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;

        return tok_identifier;
    }

    if (ISDIGIT(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (ISDIGIT(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0);

        return tok_number;
    }

    if (LastChar =='#') {
        do {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF)
            return gettok();
    }

    if (LastChar == EOF)
        return tok_eof;

    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

static int getNextToken() {
    return CurTok = gettok();
}

std::unique_ptr<ExprAST> Error(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<ExprAST> ErrorP(const char *Str) {
    Error(Str);
    return nullptr;
}
