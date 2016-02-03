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

    // control
    tok_if   = -6,
    tok_then = -7,
    tok_else = -8,
    tok_for  = -9,
    tok_in   = -10,
    
    // operators
    tok_binary = -11,
    tok_unary  = -12,
};

static std::string IdentifierStr;
static double NumVal;
static int CurTok;
static std::map<char, int> BinopPrecedence;

static std::unique_ptr<ExprAST> ParsePrimary();
static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<ExprAST> ParseIfExpr();
static std::unique_ptr<ExprAST> ParseForExpr();
static std::unique_ptr<ExprAST> ParseUnary();

static llvm::IRBuilder<> Builder(llvm::getGlobalContext());
static std::map<std::string, llvm::AllocaInst*> NamedValues;
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
        if (IdentifierStr == "if")
            return tok_if;
        if (IdentifierStr == "then")
            return tok_then;
        if (IdentifierStr == "else")
            return tok_else;
        if (IdentifierStr == "for")
            return tok_for;
        if (IdentifierStr == "in")
            return tok_in;
        if (IdentifierStr == "binary")
            return tok_binary;
        if (IdentifierStr == "unary")
            return tok_unary;

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

static llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                                const std::string &VarName)
{
    llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
    return TmpB.CreateAlloca(llvm::Type::getDoubleTy(llvm::getGlobalContext()), 0, VarName.c_str());
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

        auto RHS = ParseUnary();
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
    auto LHS = ParseUnary();
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
    case tok_if:
        return ParseIfExpr();
    case tok_for:
        return ParseForExpr();
    }
}

static std::unique_ptr<PrototypeAST> ParsePrototype()
{
    std::string FnName;
    
    unsigned Kind = 0;
    unsigned BinaryPrecedence = 30;
    
    switch (CurTok) {
    default:
        return ErrorP("Expected function name in prototype");
    case tok_identifier:
        FnName = IdentifierStr;
        Kind = 0;
        getNextToken();
        break;
    case tok_unary:
        getNextToken();
        if (! isascii(CurTok))
            return ErrorP("Expected unary operator");
        FnName = "unary";
        FnName += (char)CurTok;
        Kind = 1;
        getNextToken();
        break;
    case tok_binary:
        getNextToken();
        if (! isascii(CurTok)) {
            return ErrorP("Expected binary operator");
        }
        FnName = "binary";
        FnName += (char)CurTok;
        Kind = 2;
        
        getNextToken();
        
        if (CurTok == tok_number) {
            if (NumVal < 1 || NumVal > 100)
                return ErrorP("Invalid precedence: must be 1..100");
            BinaryPrecedence = (unsigned)NumVal;
            getNextToken();
        }
        
        break;
    }
    
    if (CurTok != '(')
        return ErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return ErrorP("Expected ')' in prototype");

    getNextToken(); // eat ')'
    
    if (Kind && ArgNames.size() != Kind)
        return ErrorP("Invalid number of operands for operator");

    return llvm::make_unique<PrototypeAST>(FnName, std::move(ArgNames), Kind != 0, BinaryPrecedence);
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

static std::unique_ptr<ExprAST> ParseIfExpr()
{
    getNextToken(); // eat the if.
    
    // condition
    auto Cond = ParseExpression();
    if (!Cond)
        return nullptr;

    if (CurTok != tok_then)
        return Error("expected then");
    getNextToken(); // eat the then
    
    auto Then = ParseExpression();
    if (!Then)
        return nullptr;
    
    if (CurTok != tok_else)
        return Error("expected else");
    getNextToken(); // eat the else

    auto Else = ParseExpression();
    if (!Else)
        return nullptr;
    
    return llvm::make_unique<IfExprAST>(std::move(Cond), std::move(Then), std::move(Else));
}

static std::unique_ptr<ExprAST> ParseForExpr()
{
    getNextToken(); // eat for
    
    if (CurTok != tok_identifier)
        return Error("expected identifier after for");
    
    std::string IdName = IdentifierStr;
    getNextToken(); // eat identifier
    
    if (CurTok != '=')
        return Error("expected '=' after for");
    getNextToken(); // eat '='
    
    
    auto Start = ParseExpression();
    if (!Start)
        return nullptr;
    if (CurTok != ',')
        return Error("expected ',' after for start value");
    getNextToken();
    
    auto End = ParseExpression();
    if (!End)
        return nullptr;
    
    // the step value is optional
    std::unique_ptr<ExprAST> Step;
    if (CurTok == ',') {
        getNextToken();
        Step = ParseExpression();
        if (!Step)
            return nullptr;
    }
    
    if (CurTok != tok_in)
        return Error("expected 'in' after for");
    getNextToken(); // eat 'in'
    
    auto Body = ParseExpression();
    if (!Body)
        return nullptr;
    
    return llvm::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                         std::move(Step), std::move(Body));
}

static std::unique_ptr<ExprAST> ParseUnary()
{
    if (! isascii(CurTok) || CurTok == '(' || CurTok == ',')
        return ParsePrimary();
    
    int Opc = CurTok;
    getNextToken();
    
    if (auto Operand = ParseUnary())
        return llvm::make_unique<UnaryExprAST>(Opc, std::move(Operand));

    return nullptr;
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
    llvm::AllocaInst *V = NamedValues[Name];
    if (!V)
        ErrorV("Unknown variable name");
    
    return Builder.CreateLoad(V, Name.c_str());
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
        break;
    }
    
    std::string FnName = MakeLegalFunctionName(std::string("binary") + Op);
    llvm::Function *F = JITHelper->getFunction(FnName);
    assert(F && "binary operator not found!");
    
    llvm::Value *Ops[2] = {L, R};
    return Builder.CreateCall(F, Ops, "binop");
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
    }

    return F;
}

llvm::Function *FunctionAST::codegen()
{
    llvm::Function *TheFunction = Proto->codegen();
    if (! TheFunction)
        return nullptr;
    
    if (Proto->isBinaryOp())
        BinopPrecedence[Proto->getOperatorName()] = Proto->getBinaryPrecedence();

    llvm::BasicBlock *BB = llvm::BasicBlock::Create(llvm::getGlobalContext(),
                                                    "entry",
                                                    TheFunction);
    Builder.SetInsertPoint(BB);

    NamedValues.clear();

    for (auto &Arg: TheFunction->args()) {
        llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
        
        Builder.CreateStore(&Arg, Alloca);
        
        NamedValues[Arg.getName()] = Alloca;
    }

    if (llvm::Value *RetVal = Body->codegen()) {
        Builder.CreateRet(RetVal);
        verifyFunction(*TheFunction);
        return TheFunction;
    }

    // error
    TheFunction->eraseFromParent();
    return nullptr;
}

llvm::Value *ForExprAST::codegen()
{   
    // loop header
    llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();
    
    llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    
    // start code
    llvm::Value *StartVal = Start->codegen();
    if (StartVal == 0)
        return 0;

    Builder.CreateStore(StartVal, Alloca);        
    
    llvm::BasicBlock *LoopBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "loop", TheFunction);
    
    // terminate with branch
    Builder.CreateBr(LoopBB);    
    
    // LoopBB
    Builder.SetInsertPoint(LoopBB);
    
    // shadowing
    llvm::AllocaInst *OldVal = NamedValues[VarName];
    NamedValues[VarName] = Alloca;
    
    if (!Body->codegen())
        return nullptr; 
    
    // step value
    llvm::Value *StepVal = nullptr;
    if (Step) {
        StepVal = Step->codegen();
        if (!StepVal)
            return nullptr;
    } else {
        StepVal = llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(1.0));
    }
    
    // compute end condtion
    llvm::Value *EndCond = End->codegen();
    if (!EndCond)
        return nullptr;

    llvm::Value *CurVar  = Builder.CreateLoad(Alloca, VarName.c_str());
    llvm::Value *NextVar = Builder.CreateFAdd(CurVar, StepVal, "nextvar");
    Builder.CreateStore(NextVar, Alloca);
    
    EndCond = Builder.CreateFCmpONE(EndCond, llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(0.0)), "loopcond");
    
    // after loop
    llvm::BasicBlock *AfterBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "afterloop", TheFunction);
    
    Builder.CreateCondBr(EndCond, LoopBB, AfterBB);
    
    Builder.SetInsertPoint(AfterBB);
    
    if (OldVal)
        NamedValues[VarName] = OldVal;
    else
        NamedValues.erase(VarName);
    
    return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(llvm::getGlobalContext()));
}

llvm::Value *IfExprAST::codegen()
{
    llvm::Value *CondV = Cond->codegen();
    if (!CondV)
        return nullptr;

    CondV = Builder.CreateFCmpONE(CondV, llvm::ConstantFP::get(llvm::getGlobalContext(),
                                  llvm::APFloat(0.0)), "ifcond");
    
    llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();
    
    llvm::BasicBlock *ThenBB  = llvm::BasicBlock::Create(llvm::getGlobalContext(), "then", TheFunction);
    llvm::BasicBlock *ElseBB  = llvm::BasicBlock::Create(llvm::getGlobalContext(), "else");
    llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "ifcont");
    
    Builder.CreateCondBr(CondV, ThenBB, ElseBB);
    
    // then
    Builder.SetInsertPoint(ThenBB);
    
    llvm::Value *ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;
    
    Builder.CreateBr(MergeBB);
    ThenBB = Builder.GetInsertBlock();
    
    // else
    TheFunction->getBasicBlockList().push_back(ElseBB);
    Builder.SetInsertPoint(ElseBB);
    
    llvm::Value *ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;
    
    Builder.CreateBr(MergeBB);
    ElseBB = Builder.GetInsertBlock();
    
    // merge
    TheFunction->getBasicBlockList().push_back(MergeBB);
    Builder.SetInsertPoint(MergeBB);
    
    llvm::PHINode *PN = Builder.CreatePHI(llvm::Type::getDoubleTy(llvm::getGlobalContext()), 2, "iftmp");
    
    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    
    return PN;
}

llvm::Value *UnaryExprAST::codegen()
{
    llvm::Value *OperandV = Operand->codegen();
    if (!OperandV)
        return nullptr;
    auto FnName = MakeLegalFunctionName(std::string("unary") + Opcode);
    llvm::Function *F = JITHelper->getFunction(FnName);
    if (!F)
        return ErrorV("Unknown unary operator");
    
    return Builder.CreateCall(F, OperandV, "unop");
}

extern "C" double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

extern "C" double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
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
