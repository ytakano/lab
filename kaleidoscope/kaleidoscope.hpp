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
    PrototypeAST(const std::string &n, std::vector<std::string> a)
        : Name(n), Args(std::move(a)) { }

    llvm::Function *codegen();
    const std::string &getName() const { return Name; }

private:
    std::string Name;
    std::vector<std::string> Args;
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
    virtual Value *codegen();
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
