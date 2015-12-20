#include "kaleidoscope.hpp"

#include <llvm/Analysis/Passes.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#include <cctype>
#include <cstdio>
#include <map>
#include <memory>

#include "MCJITHelper.hpp"

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

static llvm::IRBuilder<> Builder(llvm::getGlobalContext());
static std::map<std::string, llvm::Value*> NamedValues;
static std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
static MCJITHelper *JITHelper;

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

        LHS = llvm::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
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

    return llvm::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition()
{
    getNextToken();
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));

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
        auto Proto = llvm::make_unique<PrototypeAST>("", std::vector<std::string>());
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

static void HandleDefinition()
{
    if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Parsed a function definition\n");
            FnIR->dump();
        }
    } else {
        getNextToken();
    }
}

static void HandleExtern()
{
    if (auto ProtoAST = ParseExtern()) {
        if (auto FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Parsed an extern\n");
            FnIR->dump();
        }
    } else {
        getNextToken();
    }
}

static void HandleTopLevelExpression()
{
    if (auto FnAST = ParseTopLevelExpr()) {
        if (auto *FnIR = FnAST->codegen()) {
            FnIR->dump();
            
            void *FPtr = JITHelper->getPointerToFunction(FnIR);

            double (*FP)() = (double (*)())(intptr_t)FPtr;
            fprintf(stderr, "Evaluated to %f\n", FP());
        }
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

llvm::Value *NumberExprAST::codegen()
{
    return llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen()
{
    llvm::Value *V = NamedValues[Name];

    if (!V)
        ErrorV("Unknown variable name");
    return V;
}

llvm::Value *BinaryExprAST::codegen()
{
    llvm::Value *L = LHS->codegen();
    llvm::Value *R = RHS->codegen();

    if (!L || !R)
        return nullptr;

    switch (Op) {
    case '+':
        return Builder.CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder.CreateFSub(L, R, "subtmp");
    case '*':
        return Builder.CreateFMul(L, R, "multmp");
    case '<':
        L = Builder.CreateFCmpULT(L, R, "cmptmp");
        return Builder.CreateUIToFP(L,
                                    llvm::Type::getDoubleTy(llvm::getGlobalContext()),
                                    "booltmp");
    default:
        return ErrorV("invalid binary operator");
    }
}

llvm::Value *CallExprAST::codegen()
{
    llvm::Function *CalleeF = JITHelper->getFunction(Callee);
    if (!CalleeF)
        return ErrorV("Unknown function referenced");

    if (CalleeF->arg_size() != Args.size())
        return ErrorV("Incorrect # arguments passed");

    std::vector<llvm::Value*> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }

    return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Function *PrototypeAST::codegen()
{
    std::vector<llvm::Type*> Doubles(Args.size(),
                                     llvm::Type::getDoubleTy(llvm::getGlobalContext()));
    llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(llvm::getGlobalContext()), Doubles, false);

    std::string FnName = MakeLegalFunctionName(Name);

    llvm::Module *M = JITHelper->getModuleForNewFunction();

    llvm::Function *F = llvm::Function::Create(FT,
                                               llvm::Function::ExternalLinkage,
                                               FnName,
                                               M);

    // If F conflicted, there was already something named 'Name'.  If it has a
    // body, don't allow redefinition or reextern.
    if (F->getName() != FnName) {
        // Delete the one we just made and get the existing one.
        F->eraseFromParent();
        F = JITHelper->getFunction(Name);
        // If F already has a body, reject this.
        if (!F->empty()) {
            ErrorF("redefinition of function");
            return 0;
        }

        // If F took a different number of args, reject.
        if (F->arg_size() != Args.size()) {
            ErrorF("redefinition of function with different # args");
            return 0;
        }
    }
    
    unsigned idx = 0;
    for (auto &Arg: F->args()) {
        Arg.setName(Args[idx++]);

        NamedValues[Args[idx]] = &Arg;
    }

    return F;
}

llvm::Function *FunctionAST::codegen()
{
    llvm::Function *TheFunction = Proto->codegen();

    if (! TheFunction)
        return nullptr;

    llvm::BasicBlock *BB = llvm::BasicBlock::Create(llvm::getGlobalContext(),
                                                    "entry",
                                                    TheFunction);
    Builder.SetInsertPoint(BB);

    NamedValues.clear();

    for (auto &Arg: TheFunction->args())
        NamedValues[Arg.getName()] = &Arg;

    if (llvm::Value *RetVal = Body->codegen()) {
        Builder.CreateRet(RetVal);
        verifyFunction(*TheFunction);
        return TheFunction;
    }

    // error
    TheFunction->eraseFromParent();
    return nullptr;
}

int main()
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    llvm::LLVMContext &Context = llvm::getGlobalContext();
    JITHelper = new MCJITHelper(Context);
    
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40; // hightest

    fprintf(stderr, "ready> ");
    getNextToken();

    MainLoop();

    return 0;
}
