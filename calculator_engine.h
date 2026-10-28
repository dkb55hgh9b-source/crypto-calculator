// Движок калькулятора в стиле iPhone. Чистый C++17, без зависимостей от GUI.
// Поддерживает: приоритет операций (2+3×4=14), скобки, повтор "=", проценты в стиле iOS,
// память (mc/m+/m−/mr), научные функции, Rad/Deg, EE, Backspace.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

class CalcEngine {
public:
    enum Op { Add = 0, Sub, Mul, Div, Pow, Root, LogY, LParen };
    enum Fn {
        Square, Cube, Inv, Sqrt, Cbrt, Ln, Log10, Log2, ExpE, Exp10, Exp2, Fact,
        Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Asinh, Acosh, Atanh
    };
    enum Const { Pi, E, Rand };

    static constexpr const char* kDecimal = ",";           // десятичный разделитель (ru)
    static constexpr const char* kThousands = "\xC2\xA0";  // неразрывный пробел
    static constexpr int kMaxDigits = 9;

    CalcEngine() : rng_(std::random_device{}()) { allClear(); }

    // ---------- ввод числа ----------
    void digit(int d) {
        if (error_) allClear();
        if (!typing_) startEntry("0");
        auto epos = entry_.find('e');
        if (epos != std::string::npos) {  // ввод порядка (после EE)
            std::string ex = entry_.substr(epos + 1);
            bool neg = !ex.empty() && ex[0] == '-';
            std::string dg = neg ? ex.substr(1) : ex;
            if (dg == "0") dg = std::to_string(d);
            else if (dg.size() < 3) dg += static_cast<char>('0' + d);
            entry_ = entry_.substr(0, epos + 1) + (neg ? "-" : "") + dg;
            return;
        }
        int count = 0;
        for (char c : entry_) if (std::isdigit(static_cast<unsigned char>(c))) ++count;
        if (entry_ == "0") entry_ = std::to_string(d);
        else if (entry_ == "-0") entry_ = "-" + std::to_string(d);
        else if (count < kMaxDigits) entry_ += static_cast<char>('0' + d);
    }

    void decimal() {
        if (error_) allClear();
        if (!typing_) startEntry("0");
        if (entry_.find('.') == std::string::npos && entry_.find('e') == std::string::npos)
            entry_ += '.';
    }

    void negate() {
        if (error_) return;
        if (typing_) {
            auto e = entry_.find('e');
            if (e != std::string::npos) {
                if (entry_[e + 1] == '-') entry_.erase(e + 1, 1);
                else entry_.insert(e + 1, "-");
            } else if (entry_[0] == '-') {
                entry_.erase(0, 1);
            } else {
                entry_.insert(0, "-");
            }
        } else if (afterOp_) {
            startEntry("-0");
        } else {
            value_ = -value_;
        }
    }

    void backspace() {
        if (error_ || !typing_) return;
        entry_.pop_back();
        if (!entry_.empty() && entry_.back() == 'e') entry_.pop_back();
        else if (entry_.size() >= 2 && entry_.compare(entry_.size() - 2, 2, "e-") == 0)
            entry_.erase(entry_.size() - 2);
        if (entry_.empty() || entry_ == "-") entry_ = "0";
    }

    void ee() {
        if (error_) return;
        if (!typing_) {
            char b[40];
            std::snprintf(b, sizeof b, "%.9g", value_);
            if (std::string(b).find('e') != std::string::npos) return;
            startEntry(b);
        }
        if (entry_.find('e') == std::string::npos) entry_ += "e0";
    }

    // ---------- функции ----------
    void percent() {
        if (error_) return;
        double x = current();
        if (!ops_.empty() && !vals_.empty() && (ops_.back() == Add || ops_.back() == Sub))
            x = vals_.back() * x / 100.0;
        else
            x /= 100.0;
        setResult(x);
    }

    void unary(Fn f) {
        if (error_) return;
        double r;
        if (!compute(f, current(), r)) return fail();
        setResult(r);
    }

    void constant(Const c) {
        if (error_) allClear();
        switch (c) {
            case Pi: setResult(3.14159265358979323846); break;
            case E: setResult(2.71828182845904523536); break;
            case Rand: setResult(std::uniform_real_distribution<double>(0.0, 1.0)(rng_)); break;
        }
    }

    // ---------- доступ к значению (для конвертера валют) ----------
    double value() const { return error_ ? 0.0 : current(); }
    void setValue(double v) {
        if (error_) allClear();
        setResult(v);
    }

    // ---------- бинарные операции, скобки, "=" ----------
    void binary(Op o) {
        if (error_) return;
        if (afterOp_ && !ops_.empty() && ops_.back() != LParen) {  // замена оператора
            ops_.pop_back();
            value_ = vals_.back();
            vals_.pop_back();
        }
        double x = current();
        const int p = prec(o);
        while (!ops_.empty() && ops_.back() != LParen &&
               (prec(ops_.back()) > p || (prec(ops_.back()) == p && o != Pow))) {
            if (!reduceTop(x)) return fail();
        }
        vals_.push_back(x);
        ops_.push_back(o);
        value_ = x;
        typing_ = false;
        afterOp_ = true;
    }

    void openParen() {
        if (error_) allClear();
        ops_.push_back(LParen);
        ++parenDepth_;
        value_ = 0;
        typing_ = false;
        afterOp_ = true;
    }

    void closeParen() {
        if (error_ || parenDepth_ == 0) return;
        double x = current();
        while (!ops_.empty() && ops_.back() != LParen)
            if (!reduceTop(x)) return fail();
        if (!ops_.empty()) ops_.pop_back();
        --parenDepth_;
        setResult(x);
    }

    void equals() {
        if (error_) return;
        double x = current();
        if (ops_.empty()) {
            if (hasLast_) {  // повтор последней операции: 2+3=== → 5, 8, 11
                double r;
                if (!apply(lastOp_, x, lastOperand_, r)) return fail();
                x = r;
            }
            setResult(x);
            return;
        }
        bool first = true;
        while (!ops_.empty()) {
            if (ops_.back() == LParen) { ops_.pop_back(); continue; }
            if (first) { lastOp_ = ops_.back(); lastOperand_ = x; hasLast_ = true; first = false; }
            if (!reduceTop(x)) return fail();
        }
        parenDepth_ = 0;
        setResult(x);
    }

    // ---------- очистка ----------
    void clear() { showClear() ? clearEntry() : allClear(); }

    void clearEntry() {
        error_ = false;
        startEntry("0");
    }

    void allClear() {
        error_ = false;
        vals_.clear();
        ops_.clear();
        parenDepth_ = 0;
        hasLast_ = false;
        value_ = 0;
        startEntry("0");
    }

    // ---------- память / режим углов ----------
    void memClear() { memory_ = 0; }
    void memAdd() { if (!error_) { memory_ += current(); endEntry(); } }
    void memSub() { if (!error_) { memory_ -= current(); endEntry(); } }
    void memRecall() { if (error_) allClear(); setResult(memory_); }
    bool hasMemory() const { return memory_ != 0.0; }
    void toggleAngle() { deg_ = !deg_; }
    bool isDeg() const { return deg_; }

    // ---------- состояние для UI ----------
    bool showClear() const { return !error_ && typing_ && entry_ != "0" && entry_ != "-0"; }
    int activeOp() const { return (afterOp_ && !ops_.empty() && ops_.back() <= Div) ? ops_.back() : -1; }
    int parenDepth() const { return parenDepth_; }

    std::string display() const {
        if (error_) return "Ошибка";
        if (typing_) return formatEntry(entry_);
        return formatNumber(value_);
    }

    bool inError() const { return error_; }
    // true, если последним нажат бинарный оператор (его можно заменить другим)
    bool afterOperator() const { return afterOp_ && !ops_.empty() && ops_.back() != LParen; }

    // Форматирование для режима «Конвертер»
    static std::string fmtNumber(double v) { return formatNumber(v); }
    static std::string formatFixed(double v, int decimals) {  // 4 125,10
        if (!std::isfinite(v)) return "\xE2\x80\x94";
        if (std::fabs(v) < 0.5 * std::pow(10.0, -decimals)) v = 0;
        char b[64];
        std::snprintf(b, sizeof b, "%.*f", decimals, v);
        std::string s = b;
        const bool neg = !s.empty() && s[0] == '-';
        if (neg) s.erase(0, 1);
        return (neg ? "-" : "") + groupMantissa(s);
    }

private:
    // --- состояние ---
    std::string entry_ = "0";
    bool typing_ = true;     // идёт набор числа
    bool afterOp_ = false;   // последним нажат оператор/скобка
    bool error_ = false;
    double value_ = 0;       // отображаемое значение, когда не набираем
    std::vector<double> vals_;
    std::vector<Op> ops_;
    int parenDepth_ = 0;
    bool hasLast_ = false;
    Op lastOp_ = Add;
    double lastOperand_ = 0;
    double memory_ = 0;
    bool deg_ = true;        // iPhone по умолчанию в градусах (кнопка показывает "Rad")
    std::mt19937 rng_;

    double current() const { return typing_ ? std::strtod(entry_.c_str(), nullptr) : value_; }

    void startEntry(const std::string& s) { entry_ = s; typing_ = true; afterOp_ = false; }

    void endEntry() { value_ = current(); typing_ = false; }

    void setResult(double v) {
        if (!std::isfinite(v)) return fail();
        value_ = clean(v);
        typing_ = false;
        afterOp_ = false;
    }

    void fail() {
        error_ = true;
        typing_ = false;
        vals_.clear();
        ops_.clear();
        parenDepth_ = 0;
    }

    static int prec(Op o) {
        switch (o) {
            case Add: case Sub: return 1;
            case Mul: case Div: return 2;
            default: return 3;
        }
    }

    bool reduceTop(double& x) {
        Op o = ops_.back(); ops_.pop_back();
        double a = vals_.back(); vals_.pop_back();
        double r;
        if (!apply(o, a, x, r)) return false;
        x = r;
        return true;
    }

    static bool apply(Op o, double a, double b, double& r) {
        switch (o) {
            case Add: r = a + b; break;
            case Sub: r = a - b; break;
            case Mul: r = a * b; break;
            case Div: if (b == 0) return false; r = a / b; break;
            case Pow: r = std::pow(a, b); break;
            case Root:
                if (b == 0) return false;
                if (a < 0 && std::fmod(b, 2.0) != 0.0) return false;
                r = std::pow(a, 1.0 / b);
                break;
            case LogY:
                if (a <= 0 || b <= 0 || b == 1) return false;
                r = std::log(a) / std::log(b);
                break;
            default: return false;
        }
        return std::isfinite(r);
    }

    bool compute(Fn f, double x, double& r) const {
        const double pi = 3.14159265358979323846;
        auto toRad = [&](double v) { return deg_ ? v * pi / 180.0 : v; };
        auto fromRad = [&](double v) { return deg_ ? v * 180.0 / pi : v; };
        bool trig = false;
        switch (f) {
            case Square: r = x * x; break;
            case Cube: r = x * x * x; break;
            case Inv: if (x == 0) return false; r = 1.0 / x; break;
            case Sqrt: if (x < 0) return false; r = std::sqrt(x); break;
            case Cbrt: r = std::cbrt(x); break;
            case Ln: if (x <= 0) return false; r = std::log(x); break;
            case Log10: if (x <= 0) return false; r = std::log10(x); break;
            case Log2: if (x <= 0) return false; r = std::log2(x); break;
            case ExpE: r = std::exp(x); break;
            case Exp10: r = std::pow(10.0, x); break;
            case Exp2: r = std::pow(2.0, x); break;
            case Fact:
                if (x < 0 || (x == std::floor(x) && x > 170)) return false;
                r = std::tgamma(x + 1.0);
                if (x == std::floor(x)) r = std::round(r);
                break;
            case Sin: r = std::sin(toRad(x)); trig = true; break;
            case Cos: r = std::cos(toRad(x)); trig = true; break;
            case Tan:
                if (deg_ && std::fmod(std::fabs(x), 180.0) == 90.0) return false;
                r = std::tan(toRad(x)); trig = true;
                break;
            case Asin: if (std::fabs(x) > 1) return false; r = fromRad(std::asin(x)); break;
            case Acos: if (std::fabs(x) > 1) return false; r = fromRad(std::acos(x)); break;
            case Atan: r = fromRad(std::atan(x)); break;
            case Sinh: r = std::sinh(x); break;
            case Cosh: r = std::cosh(x); break;
            case Tanh: r = std::tanh(x); break;
            case Asinh: r = std::asinh(x); break;
            case Acosh: if (x < 1) return false; r = std::acosh(x); break;
            case Atanh: if (std::fabs(x) >= 1) return false; r = std::atanh(x); break;
        }
        if (trig && std::fabs(r) < 1e-15) r = 0;
        return std::isfinite(r);
    }

    // убирает шум вида 0.30000000000000004
    static double clean(double v) {
        char b[40];
        std::snprintf(b, sizeof b, "%.14g", v);
        return std::strtod(b, nullptr);
    }

    // ---------- форматирование ----------
    static std::string groupMantissa(const std::string& m) {  // без знака, "1234.5"
        auto dot = m.find('.');
        std::string ip = m.substr(0, dot);
        std::string out;
        for (size_t i = 0; i < ip.size(); ++i) {
            if (i > 0 && (ip.size() - i) % 3 == 0) out += kThousands;
            out += ip[i];
        }
        if (dot != std::string::npos) out += kDecimal + m.substr(dot + 1);
        return out;
    }

    static std::string formatEntry(const std::string& e) {
        std::string s = e;
        bool neg = !s.empty() && s[0] == '-';
        if (neg) s.erase(0, 1);
        std::string ex;
        auto ep = s.find('e');
        if (ep != std::string::npos) { ex = s.substr(ep); s.erase(ep); }
        return (neg ? "-" : "") + groupMantissa(s) + ex;
    }

    static std::string formatNumber(double v) {
        if (v == 0) return "0";
        char b[40];
        if (v == std::floor(v) && std::fabs(v) < 1e12)  // целые числа показываем полностью
            std::snprintf(b, sizeof b, "%.0f", v);
        else
            std::snprintf(b, sizeof b, "%.9g", v);
        std::string s = b;
        bool neg = s[0] == '-';
        if (neg) s.erase(0, 1);
        std::string ex;
        auto ep = s.find('e');
        if (ep != std::string::npos) {
            ex = "e" + std::to_string(std::atoi(s.c_str() + ep + 1));
            s.erase(ep);
        }
        return (neg ? "-" : "") + groupMantissa(s) + ex;
    }
};
