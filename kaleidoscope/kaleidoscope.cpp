#include "kaleidoscope.hpp"

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#include <cctype>
#include <cstdio>
#include <map>
#include <memory>

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
static std::map<char, int> BinopPrecedence;

static std::unique_ptr<ExprAST> ParsePrimary();
static std::unique_ptr<ExprAST> ParseExpression();

static std::unique_ptr<Module> *TheModule;
static IRBuilder<> Builder(getGlobalContext());
static std::map<std::string, Value*> NameValues;

static int gettok()
{
    static int LastChar = ' ';

    // skip any whitespace.
    while (isspace(LastChar))
        LastChar = getchar();

    if (isalpha(LastChar)) {
        IdentifierStr = LastChar;
        for (;;) {
            LastChar = getchar();
            if (isalnum(LastChar)) {
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

    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

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

Value *ErrorV(const char *Str)
{
    Error(Str);
    return nullptr;
}

std::unique_ptr<ExprAST> Error(const char *Str)
{
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> ErrorP(const char *Str)
{
    Error(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
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
static int GetTokPredecence()
{
    if (!isascii(CurTok))
        return -1;

    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0) return -1;

    return TokPrec;
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS)
{
    for (;;) {
        int TokPrec = GetTokPredecence();
        if (TokPrec < ExprPrec)
            return LHS;

        int BinOp = CurTok;
        getNextToken();

        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        int NextPrec = GetTokPredecence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }

        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
                                              std::move(RHS));
    }
}

static std::unique_ptr<ExprAST> ParseExpression()
{
    auto LHS = ParsePrimary();
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr()
{
    std::string IdName = IdentifierStr;

    getNextToken();

    if (CurTok != '(') // 変数
        return std::make_unique<VariableExprAST>(IdName);

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

    return std::make_unique<CallExprAST>(IdName, std::move(Args));
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

static std::unique_ptr<PrototypeAST> ParsePrototype()
{
    if (CurTok != tok_identifier)
        return ErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    getNextToken();

    if (CurTok != '(')
        return ErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return ErrorP("Expected ')' in prototype");

    getNextToken();

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition()
{
    getNextToken();
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

    return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern()
{
    getNextToken();
    return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr()
{
    if (auto E = ParseExpression()) {
        auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

static void HandleDefinition()
{
    if (ParseDefinition()) {
        fprintf(stderr, "Parsed a function definition\n");
    } else {
        getNextToken();
    }
}

static void HandleExtern()
{
    if (ParseExtern()) {
        fprintf(stderr, "Parsed an extern\n");
    } else {
        getNextToken();
    }
}

static void HandleTopLevelExpression()
{
    if (ParseTopLevelExpr()) {
        fprintf(stderr, "Parsed a top-level expr\n");
    } else {
        getNextToken();
    }
}

static void MainLoop()
{
    for (;;) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
        case tok_eof:
            return;
        case ';':
            getNextToken();
            break;
        case tok_def:
            HandleDefinition();
            break;
        case tok_extern:
            HandleExtern();
            break;
        default:
            HandleTopLevelExpression();
            break;
        }
    }
}

int main()
{
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;

    fprintf(stderr, "ready> ");
    getNextToken();

    MainLoop();

    return 0;
}
