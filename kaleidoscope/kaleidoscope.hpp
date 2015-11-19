#ifndef KALEIDOSCOPE_HPP
#define KALEIDOSCOPE_HPP

#include <string>

class ExprAST {
public:
    virtual ~ExprAST() { }
};

class NumberExprAST : public ExprAST {
public:
    NumberExprAST(double v) : Val(v) { }

private:
    double Val;
};

class VariableExprAST : public ExprAST {
public:
    VariableExprAST(const std::string &n) : Name(n) { }

private:
    std::string Name;
};

class BinaryExprAST : public ExprAST {
public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> L,
                  std::unique_ptr<ExprAST> R)
        : Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) { }

private:
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;
};

class CallExprAST : public ExprAST {
public:
    CallExprAST(const std::string &c,
                std::vector<std::unique_ptr<ExprAST>> a)
        : Callee(c), Args(std::move(a)) { }

private:
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;
};

class PrototyeAST {
public:
    PrototyeAST(const std::string &n, std::vector<std::string> a)
        : Name(n), Args(std::move(a)) { }

private:
    std::string Name;
    std::vector<std::string> Args;
};

class FunctionAST {
public:
    FunctionAST(std::unique_ptr<PrototyeAST> p,
                std::unique_ptr<ExprAST> b)
        : Proto(std::move(p)), Body(std::move(b)) { }

private:
    std::unique_ptr<PrototyeAST> Proto;
    std::unique_ptr<ExprAST> Body;
};

#endif // KALEIDOSCOPE_HPP
