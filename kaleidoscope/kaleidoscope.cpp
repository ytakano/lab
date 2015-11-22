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

static int getNextToken()
{
    return CurTok = gettok();
}

std::unique_ptr<ExprAST> Error(const char *Str)
{
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<ExprAST> ErrorP(const char *Str)
{
    Error(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = llvm::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr()
{
    getNextToken();
    auto V = ParseExpression();
    if (!V)
        return nullptr;

    if (CurTok != ')')
        return Error("expected ')'");

    getNextToken();
    return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr()
{
    std::string IdName = IdentifierStr;

    getNextToken();

    if (CurTok != '(') // 変数
        return llvm::make_unique<VariableExprAST>(IdName);

    // 関数呼び出し
    getNextToken();
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (1) {
            if (auto Arg = ParseExpression()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }

            if (CurTok == ')')
                break;

            if (CurTok != ',')
                return Error("Expected ')' or ',' in argument list");

            getNextToken();
        }
    }

    getNextToken();

    return llvm::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
    default:
        return Error("unknown token when expecting an expression");
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberExpr();
    case '(':
        return ParseParenExpr();
    }
}
