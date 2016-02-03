#ifndef KALEIDOSCOPE_HPP
#define KALEIDOSCOPE_HPP

#include <stdint.h>

#include <llvm/IR/Value.h>

#include <string>
#include <vector>

class ExprAST {
public:
    virtual ~ExprAST() { }
    virtual llvm::Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
public:
    NumberExprAST(double v) : Val(v) { }
    virtual llvm::Value *codegen() override;

private:
    double Val;
};

class VariableExprAST : public ExprAST {
public:
    VariableExprAST(const std::string &n) : Name(n) { }
    virtual llvm::Value *codegen() override;
    
    const std::string& getName() { return Name; }

private:
    std::string Name;
};

class BinaryExprAST : public ExprAST {
public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> L,
                  std::unique_ptr<ExprAST> R)
        : Op(op), LHS(std::move(L)), RHS(std::move(R)) { }

    virtual llvm::Value *codegen() override;

private:
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;
};

class CallExprAST : public ExprAST {
public:
    CallExprAST(const std::string &c,
                std::vector<std::unique_ptr<ExprAST>> a)
        : Callee(c), Args(std::move(a)) { }

    virtual llvm::Value *codegen() override;

private:
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;
};

class PrototypeAST {
public:
    PrototypeAST(const std::string &n, std::vector<std::string> a,
                 bool IsOperator = false, unsigned Prec = 0)
        : Name(n), Args(std::move(a)), IsOperator(IsOperator), Precedence(Prec) { }

    bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
    bool isBinaryOp() const { return IsOperator && Args.size() == 2; }
    
    char getOperatorName() const {
        assert(isUnaryOp() || isBinaryOp());
        return Name[Name.size() - 1];
    }
    
    unsigned getBinaryPrecedence() { return Precedence; }

    llvm::Function *codegen();
    const std::string &getName() const { return Name; }

private:
    std::string Name;
    std::vector<std::string> Args;
    bool IsOperator;
    unsigned Precedence;
};

class FunctionAST {
public:
    FunctionAST(std::unique_ptr<PrototypeAST> p,
                std::unique_ptr<ExprAST> b)
        : Proto(std::move(p)), Body(std::move(b)) { }

    llvm::Function *codegen();

private:
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;
};

class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond, Then, Else;

public:
    IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
              std::unique_ptr<ExprAST> Else)
        : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
    virtual llvm::Value *codegen();
};

class ForExprAST : public ExprAST {
    std::string VarName;
    std::unique_ptr<ExprAST> Start, End, Step, Body;

public:
    ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
               std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
               std::unique_ptr<ExprAST> Body)
        : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
          Step(std::move(Step)), Body(std::move(Body)) {}
    virtual llvm::Value *codegen();
};

class UnaryExprAST : public ExprAST {
    char Opcode;
    std::unique_ptr<ExprAST> Operand;
    
public:
    UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
        : Opcode(Opcode), Operand(std::move(Operand)) {}
    virtual llvm::Value *codegen();
};

class VarExprAST : public ExprAST {
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    std::unique_ptr<ExprAST> Body;
    
public:
    VarExprAST(std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
               std::unique_ptr<ExprAST> Body)
    : VarNames(std::move(VarNames)), Body(std::move(Body)) { }
    
    virtual llvm::Value *codegen();
};

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

llvm::Value *ErrorV(const char *Str)
{
    Error(Str);
    return nullptr;
}

FunctionAST *ErrorF(const char *Str) {
  Error(Str);
  return 0;
}

#endif // KALEIDOSCOPE_HPP
