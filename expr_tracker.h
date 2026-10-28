// Запись выражений для истории калькулятора: "2 + 3 × 4" = "14".
// Чистый C++17, без Qt. Класс следит за нажатиями и собирает текст выражения.
#pragma once

#include <string>

#include "calculator_engine.h"

struct HistoryEntry {
    std::string expr;
    std::string result;
    double value = 0;
};

enum class Ev { Other, Digit, Decimal, EE, Backspace, Negate, Percent, Const, MRecall,
                Unary, Binary, LParen, RParen, Equals, Clear };

class ExprTracker {
public:
    // Состояние ДО выполнения действия
    struct Pre {
        bool wasC = false;
        bool wasAfterOp = false;
        int depth = 0;
        std::string operand;  // текст текущего операнда
    };

    Pre before(const CalcEngine& e) const {
        Pre p;
        p.wasC = e.showClear();
        p.wasAfterOp = e.afterOperator();
        p.depth = e.parenDepth();
        p.operand = operandText_.empty() ? e.display() : operandText_;
        return p;
    }

    // Вызывать ПОСЛЕ действия. Возвращает true, если нужно добавить запись в историю.
    bool after(Ev ev, int arg, const Pre& pre, const CalcEngine& e, HistoryEntry& out) {
        if (e.inError()) { reset(); return false; }
        switch (ev) {
            case Ev::Digit: case Ev::Decimal: case Ev::EE: case Ev::Backspace:
                lastExpr_.clear();
                operandText_.clear();
                exprClosed_ = false;
                return false;
            case Ev::Negate:
                lastExpr_.clear();
                if (!operandText_.empty()) operandText_ = "\u2212(" + operandText_ + ")";
                return false;
            case Ev::Percent:
                lastExpr_.clear();
                operandText_ = pre.operand + "%";
                return false;
            case Ev::Const:
                lastExpr_.clear();
                operandText_ = (arg == CalcEngine::Pi) ? "\u03C0" : (arg == CalcEngine::E ? "e" : "rand");
                return false;
            case Ev::MRecall:
                lastExpr_.clear();
                operandText_ = "mr";
                return false;
            case Ev::Unary: {
                const std::string t = fnText(arg, pre.operand);
                if (!expr_.empty() || exprClosed_ || pre.depth > 0) {  // функция внутри выражения
                    operandText_ = t;
                    return false;
                }
                operandText_.clear();  // самостоятельная операция: sin(30) = 0,5
                lastExpr_ = t;
                out.expr = t;
                out.result = e.display();
                out.value = e.value();
                return true;
            }
            case Ev::Binary: {
                if (pre.wasAfterOp && lastOpLen_ > 0 && expr_.size() >= lastOpLen_)
                    expr_.erase(expr_.size() - lastOpLen_);  // оператор заменили другим
                else if (!exprClosed_)
                    expr_ += pre.operand;
                lastExpr_.clear();
                lastSym_ = opSymbol(arg);
                const std::string tok = " " + lastSym_ + " ";
                expr_ += tok;
                lastOpLen_ = tok.size();
                exprClosed_ = false;
                operandText_.clear();
                return false;
            }
            case Ev::LParen:
                lastExpr_.clear();
                expr_ += "(";
                lastOpLen_ = 0;
                exprClosed_ = false;
                operandText_.clear();
                return false;
            case Ev::RParen:
                if (e.parenDepth() < pre.depth) {
                    expr_ += (exprClosed_ ? std::string() : pre.operand) + ")";
                    exprClosed_ = true;
                    operandText_.clear();
                }
                return false;
            case Ev::Equals: {
                bool produced = false;
                if (!expr_.empty() || exprClosed_) {
                    std::string full = expr_ + (exprClosed_ ? std::string() : pre.operand);
                    for (int i = 0; i < pre.depth; ++i) full += ")";  // незакрытые скобки
                    out.expr = full;
                    out.result = e.display();
                    out.value = e.value();
                    produced = true;
                    repeatTail_ = (!exprClosed_ && !lastSym_.empty())
                                      ? " " + lastSym_ + " " + pre.operand : std::string();
                } else if (!repeatTail_.empty()) {  // повторное «=»
                    out.expr = pre.operand + repeatTail_;
                    out.result = e.display();
                    out.value = e.value();
                    produced = true;
                }
                if (produced) lastExpr_ = out.expr;
                expr_.clear();
                operandText_.clear();
                exprClosed_ = false;
                lastOpLen_ = 0;
                return produced;
            }
            case Ev::Clear:
                lastExpr_.clear();
                if (pre.wasC) operandText_.clear();  // «C» стирает только ввод
                else reset();                        // «AC» — всё
                return false;
            case Ev::Other:
                return false;
        }
        return false;
    }

    // Строка над результатом: «2 + 3 ×» пока набираем, «2 + 3 × 4 =» после вычисления
    std::string header() const {
        if (!expr_.empty()) return expr_;
        return lastExpr_.empty() ? std::string() : lastExpr_ + " =";
    }

    void reset() {
        lastExpr_.clear();
        expr_.clear();
        operandText_.clear();
        repeatTail_.clear();
        lastSym_.clear();
        lastOpLen_ = 0;
        exprClosed_ = false;
    }

private:
    std::string expr_, operandText_, repeatTail_, lastSym_, lastExpr_;
    size_t lastOpLen_ = 0;
    bool exprClosed_ = false;  // последним закрыта скобка: операнд уже внутри expr_

    static std::string opSymbol(int op) {
        switch (op) {
            case CalcEngine::Add: return "+";
            case CalcEngine::Sub: return "\u2212";
            case CalcEngine::Mul: return "\u00D7";
            case CalcEngine::Div: return "\u00F7";
            case CalcEngine::Pow: return "^";
            case CalcEngine::Root: return "\u02B8\u221A";
            case CalcEngine::LogY: return "log\u1D67";
            default: return "?";
        }
    }

    static std::string fnText(int f, const std::string& o) {
        auto call = [&](const std::string& name) { return name + "(" + o + ")"; };
        switch (f) {
            case CalcEngine::Square: return o + "\u00B2";
            case CalcEngine::Cube:   return o + "\u00B3";
            case CalcEngine::Inv:    return "1/(" + o + ")";
            case CalcEngine::Sqrt:   return call("\u221A");
            case CalcEngine::Cbrt:   return call("\u221B");
            case CalcEngine::Ln:     return call("ln");
            case CalcEngine::Log10:  return call("log\u2081\u2080");
            case CalcEngine::Log2:   return call("log\u2082");
            case CalcEngine::ExpE:   return "e^(" + o + ")";
            case CalcEngine::Exp10:  return "10^(" + o + ")";
            case CalcEngine::Exp2:   return "2^(" + o + ")";
            case CalcEngine::Fact:   return o + "!";
            case CalcEngine::Sin:    return call("sin");
            case CalcEngine::Cos:    return call("cos");
            case CalcEngine::Tan:    return call("tan");
            case CalcEngine::Asin:   return call("sin\u207B\u00B9");
            case CalcEngine::Acos:   return call("cos\u207B\u00B9");
            case CalcEngine::Atan:   return call("tan\u207B\u00B9");
            case CalcEngine::Sinh:   return call("sinh");
            case CalcEngine::Cosh:   return call("cosh");
            case CalcEngine::Tanh:   return call("tanh");
            case CalcEngine::Asinh:  return call("sinh\u207B\u00B9");
            case CalcEngine::Acosh:  return call("cosh\u207B\u00B9");
            case CalcEngine::Atanh:  return call("tanh\u207B\u00B9");
            default: return o;
        }
    }
};
