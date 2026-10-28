// Калькулятор + конвертер валют, криптовалют и единиц (Qt Widgets, C++17).
//  - тёмная тема с фиолетовым акцентом, скруглённые клавиши, карточка дисплея;
//  - переключатель «Калькулятор | Конвертер» в шапке, кнопка истории справа;
//  - окно шире, чем выше: появляются научные клавиши.
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QSettings>
#include <QSslSocket>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>

#include "calculator_engine.h"
#include "expr_tracker.h"
#include "units.h"

using E = CalcEngine;

// ============================================================================
//  Клавиша: скруглённый прямоугольник с градиентом, тенью и подсветкой при наведении
// ============================================================================
class CalcButton : public QAbstractButton {
public:
    enum Kind { Digit, Function, Operator, Scientific, Accent };

    CalcButton(const QString& text, Kind kind, QWidget* parent)
        : QAbstractButton(parent), kind_(kind) {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        fade_.setDuration(300);
        fade_.setStartValue(1.0);
        fade_.setEndValue(0.0);
        fade_.setEasingCurve(QEasingCurve::OutQuad);
        connect(&fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            press_ = v.toDouble();
            update();
        });
        connect(this, &QAbstractButton::pressed, this, [this] {
            fade_.stop();
            press_ = 1.0;
            update();
        });
        connect(this, &QAbstractButton::released, this, [this] { fade_.start(); });
    }

    void setHighlighted(bool on) {
        if (on != highlighted_) { highlighted_ = on; update(); }
    }
    void setLeftAligned(bool on) { leftAligned_ = on; }

    // Подсветка при вводе с клавиатуры
    void flash() {
        fade_.stop();
        press_ = 1.0;
        fade_.start();
    }

protected:
    bool event(QEvent* e) override {
        switch (e->type()) {
            case QEvent::HoverEnter: case QEvent::HoverLeave:
            case QEvent::Enter: case QEvent::Leave:
                update();
                break;
            default: break;
        }
        return QAbstractButton::event(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        QColor top, bottom, border, fg;
        switch (kind_) {
            case Digit:
                top = QColor(0x25, 0x2A, 0x36); bottom = QColor(0x1D, 0x21, 0x2B);
                border = QColor(0x2E, 0x34, 0x41); fg = QColor(0xEE, 0xF1, 0xF7); break;
            case Function:
                top = QColor(0x2D, 0x33, 0x40); bottom = QColor(0x26, 0x2B, 0x36);
                border = QColor(0x37, 0x3E, 0x4D); fg = QColor(0xB9, 0xC2, 0xD4); break;
            case Scientific:
                top = QColor(0x1D, 0x21, 0x2A); bottom = QColor(0x18, 0x1C, 0x23);
                border = QColor(0x27, 0x2C, 0x37); fg = QColor(0x9A, 0xA4, 0xB8); break;
            case Operator:
                top = QColor(0x34, 0x30, 0x6B); bottom = QColor(0x2B, 0x28, 0x5D);
                border = QColor(0x44, 0x3F, 0x8A); fg = QColor(0xB8, 0xAD, 0xFF); break;
            case Accent:
                top = QColor(0x8A, 0x7B, 0xFF); bottom = QColor(0x6A, 0x5A, 0xF0);
                border = QColor(0xA2, 0x96, 0xFF); fg = Qt::white; break;
        }
        if (highlighted_) {  // активный оператор / включённая "2nd"
            top = QColor(0x8A, 0x7B, 0xFF); bottom = QColor(0x6A, 0x5A, 0xF0);
            border = QColor(0xA2, 0x96, 0xFF); fg = Qt::white;
        }
        const QString t = text();
        if (kind_ == Function && (t == QLatin1String("AC") || t == QLatin1String("C")))
            fg = QColor(0xFF, 0x7A, 0x85);

        if (underMouse()) { top = top.lighter(112); bottom = bottom.lighter(112); }
        auto mix = [](const QColor& a, const QColor& b, double k) {
            return QColor(int(a.red() + (b.red() - a.red()) * k),
                          int(a.green() + (b.green() - a.green()) * k),
                          int(a.blue() + (b.blue() - a.blue()) * k));
        };
        top = mix(top, Qt::white, 0.16 * press_);
        bottom = mix(bottom, Qt::white, 0.16 * press_);

        // «объём»: тень под клавишей, при нажатии клавиша опускается
        const double w = width(), h = height();
        const double depth = std::max(2.0, h * 0.045);
        const QRectF face(0, press_ * depth * 0.7, w, h - depth);
        const double rad = std::min(face.width(), face.height()) * 0.30;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        p.drawRoundedRect(QRectF(0, depth, w, face.height()), rad, rad);

        QLinearGradient grad(face.topLeft(), face.bottomLeft());
        grad.setColorAt(0, top);
        grad.setColorAt(1, bottom);
        p.setBrush(grad);
        p.setPen(QPen(border, 1));
        p.drawRoundedRect(face.adjusted(0.5, 0.5, -0.5, -0.5), rad, rad);

        double k = 0.40;
        if (kind_ == Function) k = 0.34;
        if (kind_ == Operator || kind_ == Accent) k = 0.46;
        if (kind_ == Scientific) k = 0.32;

        QFont f = font();
        int px = std::max(8, int(face.height() * k));
        f.setPixelSize(px);
        while (px > 8 && QFontMetrics(f).horizontalAdvance(t) > face.width() * 0.82) f.setPixelSize(--px);
        p.setFont(f);
        p.setPen(fg);
        if (leftAligned_) {
            p.drawText(QRectF(face.height() * 0.45, face.top(), face.width(), face.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, t);
        } else {
            p.drawText(face, Qt::AlignCenter, t);
        }
    }

private:
    Kind kind_;
    bool highlighted_ = false;
    bool leftAligned_ = false;
    double press_ = 0.0;
    QVariantAnimation fade_;
};

// ============================================================================
//  Кнопка с нарисованной иконкой: калькулятор / стрелки обмена ⇅
// ============================================================================
class IconButton : public QAbstractButton {
public:
    enum Glyph { Swap, History };

    IconButton(Glyph g, QWidget* parent) : QAbstractButton(parent), glyph_(g) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }
    void setBackground(bool on) { bg_ = on; update(); }
    void setAccent(bool on) { accent_ = on; update(); }

protected:
    bool event(QEvent* e) override {
        switch (e->type()) {
            case QEvent::HoverEnter: case QEvent::HoverLeave:
            case QEvent::Enter: case QEvent::Leave:
                update();
                break;
            default: break;
        }
        return QAbstractButton::event(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();
        const double s = std::min(r.width(), r.height());
        const QPointF c = r.center();
        const QRectF body = r.adjusted(0.5, 0.5, -0.5, -0.5);

        const bool accentFill = bg_ && accent_;
        if (bg_) {
            if (accent_) {
                QLinearGradient g(r.topLeft(), r.bottomLeft());
                g.setColorAt(0, isDown() ? QColor(0x7A, 0x6B, 0xF0) : QColor(0x8A, 0x7B, 0xFF));
                g.setColorAt(1, isDown() ? QColor(0x5A, 0x4B, 0xD8) : QColor(0x6A, 0x5A, 0xF0));
                p.setBrush(g);
                p.setPen(QPen(QColor(0xA2, 0x96, 0xFF), 1));
                p.drawEllipse(body);
            } else {
                p.setBrush(isDown() ? QColor(0x2B, 0x30, 0x3B)
                                    : (underMouse() ? QColor(0x22, 0x27, 0x31) : QColor(0x1B, 0x1F, 0x27)));
                p.setPen(QPen(QColor(0x27, 0x2C, 0x37), 1));
                p.drawRoundedRect(body, s * 0.32, s * 0.32);
            }
        }
        const QColor fg = accentFill ? QColor(Qt::white)
                                     : (accent_ ? QColor(0x9A, 0x8D, 0xFF) : QColor(0xB9, 0xC2, 0xD4));
        p.setPen(QPen(fg, std::max(1.8, s * 0.07), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);

        if (glyph_ == History) {
            const double rad = s * 0.27;
            p.drawEllipse(c, rad, rad);
            p.drawLine(c, QPointF(c.x(), c.y() - rad * 0.62));
            p.drawLine(c, QPointF(c.x() + rad * 0.50, c.y() + rad * 0.28));
        } else {
            const double h = s * 0.30, dx = s * 0.13, a = s * 0.09;
            // стрелка вверх слева
            p.drawLine(QPointF(c.x() - dx, c.y() + h), QPointF(c.x() - dx, c.y() - h));
            p.drawLine(QPointF(c.x() - dx, c.y() - h), QPointF(c.x() - dx - a, c.y() - h + a));
            p.drawLine(QPointF(c.x() - dx, c.y() - h), QPointF(c.x() - dx + a, c.y() - h + a));
            // стрелка вниз справа
            p.drawLine(QPointF(c.x() + dx, c.y() - h), QPointF(c.x() + dx, c.y() + h));
            p.drawLine(QPointF(c.x() + dx, c.y() + h), QPointF(c.x() + dx - a, c.y() + h - a));
            p.drawLine(QPointF(c.x() + dx, c.y() + h), QPointF(c.x() + dx + a, c.y() + h - a));
        }
    }

private:
    Glyph glyph_;
    bool bg_ = false;
    bool accent_ = false;
};

// ============================================================================
//  Переключатель режимов («Калькулятор | Конвертер»)
// ============================================================================
class Segmented : public QWidget {
public:
    std::function<void(int)> onChange;

    Segmented(const QStringList& items, QWidget* parent) : QWidget(parent), items_(items) {
        setCursor(Qt::PointingHandCursor);
    }

    void setCurrent(int i) {
        if (i != cur_) { cur_ = i; update(); }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        const QRectF r = rect();
        p.setPen(QPen(QColor(0x27, 0x2C, 0x37), 1));
        p.setBrush(QColor(0x1B, 0x1F, 0x27));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), r.height() / 2, r.height() / 2);

        const int n = std::max(1, int(items_.size()));
        const double w = r.width() / n;
        QFont f = font();
        f.setPixelSize(std::max(11, int(r.height() * 0.36)));
        p.setFont(f);
        for (int i = 0; i < n; ++i) {
            const QRectF cell(i * w, 0, w, r.height());
            if (i == cur_) {
                const QRectF pill = cell.adjusted(3, 3, -3, -3);
                QLinearGradient g(pill.topLeft(), pill.bottomLeft());
                g.setColorAt(0, QColor(0x8A, 0x7B, 0xFF));
                g.setColorAt(1, QColor(0x6A, 0x5A, 0xF0));
                p.setPen(Qt::NoPen);
                p.setBrush(g);
                p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            }
            p.setPen(i == cur_ ? QColor(Qt::white) : QColor(0x8A, 0x93, 0xA6));
            p.drawText(cell, Qt::AlignCenter, items_[i]);
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        const int n = std::max(1, int(items_.size()));
        const int i = std::clamp(int(e->pos().x() / (double(width()) / n)), 0, n - 1);
        if (i != cur_) {
            cur_ = i;
            update();
            if (onChange) onChange(i);
        }
    }

private:
    QStringList items_;
    int cur_ = 0;
};

// Надпись, по которой можно кликнуть (выбор единицы)
class ClickLabel : public QLabel {
public:
    using QLabel::QLabel;
    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (onClick) onClick();
        QLabel::mousePressEvent(e);
    }
};

// ============================================================================
//  Курсы валют: официальный курс НБУ (bank.gov.ua), обновление каждые 10 минут.
//  Берутся ВСЕ валюты НБУ; курсы хранятся в QSettings, поэтому без сети
//  используются последние известные. Курс любой пары = курс_from / курс_to (через гривну).
// ============================================================================
struct CurrencyInfo {
    QString cc;
    QString name;
    double rate = 0;  // сколько гривен стоит 1 единица валюты
};

class RateTracker : public QObject {
public:
    QVector<CurrencyInfo> list;
    QString nbuDate;                  // дата курса по данным НБУ
    QString status;                   // "обновлено 14:05" или текст ошибки
    bool failed = false;
    std::function<void()> onUpdate;

    explicit RateTracker(QObject* parent) : QObject(parent) {
        QSettings s;
        nbuDate = s.value("date").toString();
        const QJsonArray arr = QJsonDocument::fromJson(s.value("all").toString().toUtf8()).array();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            CurrencyInfo ci;
            ci.cc = o.value(QStringLiteral("cc")).toString();
            ci.name = o.value(QStringLiteral("n")).toString();
            ci.rate = o.value(QStringLiteral("r")).toDouble();
            if (ci.rate > 0) list.push_back(ci);
        }
        timer_.setInterval(10 * 60 * 1000);
        connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
        timer_.start();
        refresh();
    }

    double rateOf(const QString& cc) const {
        for (const CurrencyInfo& c : list)
            if (c.cc == cc) return c.rate;
        return 0;
    }
    double usd() const { return rateOf(QStringLiteral("USD")); }
    double eur() const { return rateOf(QStringLiteral("EUR")); }
    bool ready() const { return usd() > 0 && eur() > 0; }

    void refresh() {
        QNetworkRequest req(QUrl(QStringLiteral("https://bank.gov.ua/NBUStatService/v1/statdirectory/exchange?json")));
        req.setHeader(QNetworkRequest::UserAgentHeader, "IOSCalculator/1.0");
        nam_.setTransferTimeout(15000);  // не ждать ответ дольше 15 секунд
        QNetworkReply* reply = nam_.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                QString why = QStringLiteral("нет связи");
                if (!QSslSocket::supportsSsl())
                    why = QStringLiteral("нет поддержки HTTPS в Qt");
                else
                    why += QStringLiteral(": ") + reply->errorString();
                qWarning() << "Курсы НБУ:" << why;
                return fail(why);
            }

            QVector<CurrencyInfo> fresh;
            CurrencyInfo uah;
            uah.cc = QStringLiteral("UAH");
            uah.name = QStringLiteral("Українська гривня");
            uah.rate = 1.0;
            fresh.push_back(uah);

            QString date;
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
            for (const QJsonValue& v : arr) {
                const QJsonObject o = v.toObject();
                CurrencyInfo ci;
                ci.cc = o.value(QStringLiteral("cc")).toString();
                ci.name = o.value(QStringLiteral("txt")).toString();
                ci.rate = o.value(QStringLiteral("rate")).toDouble();
                if (ci.cc.isEmpty() || ci.cc == QLatin1String("UAH") || ci.rate <= 0) continue;
                if (date.isEmpty()) date = o.value(QStringLiteral("exchangedate")).toString();
                fresh.push_back(ci);
            }
            double nu = 0, ne = 0;
            for (const CurrencyInfo& c : fresh) {
                if (c.cc == QLatin1String("USD")) nu = c.rate;
                if (c.cc == QLatin1String("EUR")) ne = c.rate;
            }
            if (nu <= 0 || ne <= 0) return fail(QStringLiteral("ошибка данных"));

            std::sort(fresh.begin(), fresh.end(), [](const CurrencyInfo& a, const CurrencyInfo& b) {
                const int pa = priority(a.cc), pb = priority(b.cc);
                return pa != pb ? pa < pb : a.cc < b.cc;
            });
            list = fresh;
            nbuDate = date;
            failed = false;
            status = QStringLiteral("обновлено ") + QTime::currentTime().toString("HH:mm");

            QJsonArray out;
            for (const CurrencyInfo& c : list) {
                QJsonObject o;
                o.insert(QStringLiteral("cc"), c.cc);
                o.insert(QStringLiteral("n"), c.name);
                o.insert(QStringLiteral("r"), c.rate);
                out.push_back(o);
            }
            QSettings s;
            s.setValue("all", QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
            s.setValue("date", nbuDate);
            if (onUpdate) onUpdate();
        });
    }

private:
    static int priority(const QString& cc) {  // популярные валюты — в начале списка
        static const QStringList first = {"UAH", "USD", "EUR", "PLN", "GBP", "CHF", "CAD", "CZK", "JPY", "CNY", "TRY"};
        const int i = first.indexOf(cc);
        return i < 0 ? 1000 : i;
    }

    void fail(const QString& why) {
        failed = true;
        status = why;
        if (onUpdate) onUpdate();
    }

    QNetworkAccessManager nam_;
    QTimer timer_;
};

// ============================================================================
//  Криптовалюты: BTC, ETH, BNB, SOL, TRX, HYPE, GRAM (бывший Toncoin) с CoinGecko
//  (публичный API без ключа), цены в USD, обновление каждые 5 минут.
//  Последние данные хранятся в QSettings.
//  Цена в других валютах считается через USD (гривна и евро: курс НБУ).
// ============================================================================
struct CryptoInfo {
    QString symbol;      // BTC
    QString name;        // Bitcoin
    double usd = 0;      // цена в долларах
    double change = 0;   // изменение за 24 часа, %
};

class CryptoTracker : public QObject {
public:
    QVector<CryptoInfo> list;
    QString status;  // "обновлено 14:05" или текст ошибки
    bool failed = false;
    std::function<void()> onUpdate;

    explicit CryptoTracker(QObject* parent) : QObject(parent) {
        QSettings s;
        const QJsonArray arr = QJsonDocument::fromJson(s.value("crypto").toString().toUtf8()).array();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            CryptoInfo ci;
            ci.symbol = o.value(QStringLiteral("s")).toString();
            ci.name = o.value(QStringLiteral("n")).toString();
            ci.usd = o.value(QStringLiteral("p")).toDouble();
            ci.change = o.value(QStringLiteral("c")).toDouble();
            if (ci.usd > 0 && isTracked(ci.symbol)) list.push_back(ci);
        }
        timer_.setInterval(5 * 60 * 1000);  // каждые 5 минут
        connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
        timer_.start();
        refresh();
    }

    double usdOf(const QString& symbol) const {
        for (const CryptoInfo& c : list)
            if (c.symbol == symbol) return c.usd;
        return 0;
    }

    void refresh() {
        QNetworkRequest req(QUrl(QStringLiteral(
            "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&sparkline=false"
            "&ids=bitcoin,ethereum,binancecoin,solana,tron,hyperliquid,the-open-network,gram")));
        req.setHeader(QNetworkRequest::UserAgentHeader, "IOSCalculator/1.0");
        req.setRawHeader("Accept", "application/json");
        nam_.setTransferTimeout(15000);
        QNetworkReply* reply = nam_.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                QString why = QStringLiteral("нет связи");
                if (!QSslSocket::supportsSsl())
                    why = QStringLiteral("нет поддержки HTTPS в Qt");
                else
                    why += QStringLiteral(": ") + reply->errorString();
                qWarning() << "Крипто (CoinGecko):" << why;
                return fail(why);
            }

            QVector<CryptoInfo> fresh;
            QVector<double> caps;  // капитализация — чтобы при совпадении тикеров выбрать настоящую монету
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
            for (const QJsonValue& v : arr) {
                const QJsonObject o = v.toObject();
                CryptoInfo ci;
                ci.symbol = o.value(QStringLiteral("symbol")).toString().toUpper();
                ci.name = o.value(QStringLiteral("name")).toString();
                ci.usd = o.value(QStringLiteral("current_price")).toDouble();
                ci.change = o.value(QStringLiteral("price_change_percentage_24h")).toDouble();
                if (o.value(QStringLiteral("id")).toString() == QLatin1String("the-open-network")) {
                    ci.symbol = QStringLiteral("GRAM");  // Toncoin переименован в Gram (июнь 2026)
                    ci.name = QStringLiteral("Gram");
                }
                const double cap = o.value(QStringLiteral("market_cap")).toDouble();
                if (!isTracked(ci.symbol) || ci.usd <= 0) continue;

                int found = -1;
                for (int i = 0; i < fresh.size(); ++i)
                    if (fresh[i].symbol == ci.symbol) found = i;
                if (found < 0) {
                    fresh.push_back(ci);
                    caps.push_back(cap);
                } else if (cap > caps[found]) {
                    fresh[found] = ci;
                    caps[found] = cap;
                }
            }
            std::sort(fresh.begin(), fresh.end(), [](const CryptoInfo& a, const CryptoInfo& b) {
                return order(a.symbol) < order(b.symbol);
            });
            if (fresh.isEmpty()) return fail(QStringLiteral("ошибка данных"));

            list = fresh;
            failed = false;
            status = QStringLiteral("обновлено ") + QTime::currentTime().toString("HH:mm");

            QJsonArray out;
            for (const CryptoInfo& c : list) {
                QJsonObject o;
                o.insert(QStringLiteral("s"), c.symbol);
                o.insert(QStringLiteral("n"), c.name);
                o.insert(QStringLiteral("p"), c.usd);
                o.insert(QStringLiteral("c"), c.change);
                out.push_back(o);
            }
            QSettings s;
            s.setValue("crypto", QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
            if (onUpdate) onUpdate();
        });
    }

private:
    static const QStringList& tracked() {
        static const QStringList t = {"BTC", "ETH", "BNB", "SOL", "TRX", "HYPE", "GRAM"};
        return t;
    }
    static bool isTracked(const QString& symbol) { return tracked().contains(symbol); }
    static int order(const QString& symbol) { return tracked().indexOf(symbol); }

    void fail(const QString& why) {
        failed = true;
        status = why;
        if (onUpdate) onUpdate();
    }

    QNetworkAccessManager nam_;
    QTimer timer_;
};

// ============================================================================
//  Главное окно
// ============================================================================
enum class A {
    Digit, Decimal, Clear, Negate, Percent, Binary, Equals, LParen, RParen,
    MClear, MAdd, MSub, MRecall, Second, Unary, Const, EE, Angle
};

struct Act {
    A a = A::Digit;
    int arg = 0;
};

struct Key {
    CalcButton* btn = nullptr;
    QString label, label2;
    Act act, act2;
    bool hasSecond = false;
    bool sci = false;
    int r = 0, c = 0, span = 1;
};

struct UnitItem {
    QString id;
    QString name;
    QString info;  // доп. строка в списке (для крипто: цена и изменение за 24 ч)
};

// Выбор одной строки конвертера: категория + единица. Верхняя и нижняя строки выбираются независимо.
struct Sel {
    int cat = 0;  // 0 — валюты, 1 — крипто, дальше — категории из units.h
    QString id;
};

// Запись истории: вычисление («2 + 3 × 4» = «14») или конвертация («100 USD» = «4 125,00 UAH»)
struct HistoryItem {
    bool conv = false;
    QString expr;
    QString result;
    double value = 0;   // результат (калькулятор) или введённое число (конвертер)
    int cat = 0;        // для конвертации: категория верхней строки
    int catTo = 0;      // категория нижней строки (может отличаться: SOL → USD)
    QString from, to;
    qint64 ts = 0;
};

class Calculator : public QWidget {
public:
    Calculator() : rates_(this), crypto_(this) {
        setWindowTitle(QStringLiteral("Калькулятор"));
        setMinimumSize(300, 480);
        setFocusPolicy(Qt::StrongFocus);
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, Qt::black);
        setPalette(pal);

        // --- обычный дисплей ---
        display_ = new QLabel(QStringLiteral("0"), this);
        display_->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        display_->setStyleSheet(QStringLiteral("color: #F4F6FA; background: transparent;"));
        QFont df = display_->font();
        df.setWeight(QFont::Light);
        display_->setFont(df);

        // --- переключатель режимов (калькулятор | конвертер) ---
        seg_ = new Segmented({QStringLiteral("Калькулятор"), QStringLiteral("Конвертер")}, this);
        seg_->onChange = [this](int i) { setConverter(i == 1); };

        exprLabel_ = new QLabel(this);
        exprLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        exprLabel_->setStyleSheet(QStringLiteral("color: #8A93A6; background: transparent;"));

        // --- кнопка истории (слева вверху) ---
        historyBtn_ = new IconButton(IconButton::History, this);
        historyBtn_->setBackground(true);
        connect(historyBtn_, &QAbstractButton::clicked, this, [this] { openHistory(); });

        // --- дисплей конвертера: две строки ---
        topVal_ = new QLabel(this);
        bottomVal_ = new QLabel(this);
        for (QLabel* l : {topVal_, bottomVal_}) {
            l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            QFont f = l->font();
            f.setWeight(QFont::Light);
            l->setFont(f);
        }
        topVal_->setStyleSheet(QStringLiteral("color: #F4F6FA; background: transparent;"));
        bottomVal_->setStyleSheet(QStringLiteral("color: #F4F6FA; background: transparent;"));

        topUnit_ = new ClickLabel(this);
        bottomUnit_ = new ClickLabel(this);
        for (ClickLabel* l : {topUnit_, bottomUnit_}) {
            l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            l->setCursor(Qt::PointingHandCursor);
            l->setStyleSheet(QStringLiteral("color: #9A8DFF; background: transparent;"));
        }
        topUnit_->onClick = [this] { openPicker(true); };
        bottomUnit_->onClick = [this] { openPicker(false); };

        swapBtn_ = new IconButton(IconButton::Swap, this);
        swapBtn_->setBackground(true);
        swapBtn_->setAccent(true);
        connect(swapBtn_, &QAbstractButton::clicked, this, [this] {
            commitConversion();
            std::swap(from_, to_);
            refresh();
            scheduleConvCommit();
        });

        defaultPair(0, from_, to_);  // по умолчанию USD → UAH

        rates_.onUpdate = [this] { refresh(); };
        crypto_.onUpdate = [this] { refresh(); };

        loadHistory();
        convTimer_.setSingleShot(true);
        connect(&convTimer_, &QTimer::timeout, this, [this] { commitConversion(); });

        for (QWidget* w : converterWidgets()) w->hide();

        buildKeys();
        refresh();
        resize(390, 780);
    }

protected:
    void resizeEvent(QResizeEvent*) override { layoutKeys(); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient bg(0, 0, 0, height());
        bg.setColorAt(0, QColor(0x0D, 0x0F, 0x13));
        bg.setColorAt(1, QColor(0x17, 0x1B, 0x22));
        p.fillRect(rect(), bg);

        auto card = [&](const QRectF& r, bool active) {
            QLinearGradient g(r.topLeft(), r.bottomLeft());
            g.setColorAt(0, QColor(0x1D, 0x21, 0x2A));
            g.setColorAt(1, QColor(0x17, 0x1B, 0x22));
            p.setBrush(g);
            p.setPen(QPen(active ? QColor(0x6A, 0x5A, 0xF0) : QColor(0x27, 0x2C, 0x37), active ? 1.5 : 1));
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 22, 22);
        };
        if (!converter_) {
            card(cardRect_, false);
        } else {
            card(cardTop_, true);
            card(cardBottom_, false);
        }
    }

    void keyPressEvent(QKeyEvent* e) override {
        const int k = e->key();
        if (k >= Qt::Key_0 && k <= Qt::Key_9 && !(e->modifiers() & Qt::ControlModifier))
            return trigger({A::Digit, k - Qt::Key_0});
        switch (k) {
            case Qt::Key_Comma: case Qt::Key_Period: return trigger({A::Decimal, 0});
            case Qt::Key_Plus:     return trigger({A::Binary, E::Add});
            case Qt::Key_Minus:    return trigger({A::Binary, E::Sub});
            case Qt::Key_Asterisk: case Qt::Key_X: return trigger({A::Binary, E::Mul});
            case Qt::Key_Slash:    return trigger({A::Binary, E::Div});
            case Qt::Key_AsciiCircum: return trigger({A::Binary, E::Pow});
            case Qt::Key_Percent:  return trigger({A::Percent, 0});
            case Qt::Key_ParenLeft:  return trigger({A::LParen, 0});
            case Qt::Key_ParenRight: return trigger({A::RParen, 0});
            case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Equal:
                return trigger({A::Equals, 0});
            case Qt::Key_Escape: case Qt::Key_C:
                if (e->modifiers() & Qt::ControlModifier) {  // Ctrl+C — копировать результат
                    QString t = converter_ ? bottomVal_->text() : display_->text();
                    t.remove(QChar(0x00A0));
                    QGuiApplication::clipboard()->setText(t);
                    return;
                }
                return trigger({A::Clear, 0});
            case Qt::Key_Backspace: case Qt::Key_Delete:
                doBackspace();
                return;
            default: QWidget::keyPressEvent(e);
        }
    }

private:
    CalcEngine eng_;
    RateTracker rates_;
    CryptoTracker crypto_;

    QLabel* display_ = nullptr;
    Segmented* seg_ = nullptr;
    QLabel* exprLabel_ = nullptr;
    QRectF cardRect_, cardTop_, cardBottom_;  // карточки дисплея

    // режим конвертера
    bool converter_ = false;
    Sel from_, to_;               // верхняя и нижняя строки выбираются независимо
    QLabel* topVal_ = nullptr;
    QLabel* bottomVal_ = nullptr;
    ClickLabel* topUnit_ = nullptr;
    ClickLabel* bottomUnit_ = nullptr;
    IconButton* swapBtn_ = nullptr;
    int convFont_ = 40;

    // история
    QVector<HistoryItem> history_;
    IconButton* historyBtn_ = nullptr;
    QTimer convTimer_;       // записывает конвертацию, когда ввод остановился
    ExprTracker tracker_;    // собирает текст выражения для истории

    QVector<Key> keys_;
    bool second_ = false;
    int maxFont_ = 80;

    QVector<QWidget*> converterWidgets() const {
        return {topVal_, bottomVal_, topUnit_, bottomUnit_, swapBtn_};
    }

    // ---------- построение клавиатуры ----------
    int add(const QString& label, CalcButton::Kind kind, Act act, int r, int c,
            int span = 1, bool sci = false, const QString& label2 = QString(),
            Act act2 = Act(), bool hasSecond = false) {
        Key key;
        key.btn = new CalcButton(label, kind, this);
        key.label = label;
        key.label2 = label2;
        key.act = act;
        key.act2 = act2;
        key.hasSecond = hasSecond;
        key.sci = sci;
        key.r = r; key.c = c; key.span = span;
        const int idx = keys_.size();
        keys_.push_back(key);
        connect(key.btn, &QAbstractButton::clicked, this, [this, idx] { press(idx); });
        return idx;
    }

    void sci(const QString& label, int r, int c, Act act,
             const QString& label2 = QString(), Act act2 = Act()) {
        add(label, CalcButton::Scientific, act, r, c, 1, true, label2, act2, !label2.isEmpty());
    }

    void buildKeys() {
        using K = CalcButton;
        const QString dec = QString::fromUtf8(E::kDecimal);

        // Основная клавиатура (позиции в портретной сетке 5×4)
        add("AC", K::Function, {A::Clear}, 0, 0);
        add(QString::fromUtf8("+/\u2212"), K::Function, {A::Negate}, 0, 1);
        add("%", K::Function, {A::Percent}, 0, 2);
        add(QString::fromUtf8("\u00F7"), K::Operator, {A::Binary, E::Div}, 0, 3);

        add("7", K::Digit, {A::Digit, 7}, 1, 0);
        add("8", K::Digit, {A::Digit, 8}, 1, 1);
        add("9", K::Digit, {A::Digit, 9}, 1, 2);
        add(QString::fromUtf8("\u00D7"), K::Operator, {A::Binary, E::Mul}, 1, 3);

        add("4", K::Digit, {A::Digit, 4}, 2, 0);
        add("5", K::Digit, {A::Digit, 5}, 2, 1);
        add("6", K::Digit, {A::Digit, 6}, 2, 2);
        add(QString::fromUtf8("\u2212"), K::Operator, {A::Binary, E::Sub}, 2, 3);

        add("1", K::Digit, {A::Digit, 1}, 3, 0);
        add("2", K::Digit, {A::Digit, 2}, 3, 1);
        add("3", K::Digit, {A::Digit, 3}, 3, 2);
        add("+", K::Operator, {A::Binary, E::Add}, 3, 3);

        const int zero = add("0", K::Digit, {A::Digit, 0}, 4, 0, 2);
        keys_[zero].btn->setLeftAligned(true);
        add(dec, K::Digit, {A::Decimal}, 4, 2);
        add("=", K::Accent, {A::Equals}, 4, 3);

        // Научные клавиши (ландшафт, 5 строк × 6 столбцов слева)
        sci("(", 0, 0, {A::LParen});
        sci(")", 0, 1, {A::RParen});
        sci("mc", 0, 2, {A::MClear});
        sci("m+", 0, 3, {A::MAdd});
        sci(QString::fromUtf8("m\u2212"), 0, 4, {A::MSub});
        sci("mr", 0, 5, {A::MRecall});

        sci("2nd", 1, 0, {A::Second});
        sci(QString::fromUtf8("x\u00B2"), 1, 1, {A::Unary, E::Square});
        sci(QString::fromUtf8("x\u00B3"), 1, 2, {A::Unary, E::Cube});
        sci(QString::fromUtf8("x\u02B8"), 1, 3, {A::Binary, E::Pow});
        sci(QString::fromUtf8("e\u02E3"), 1, 4, {A::Unary, E::ExpE},
            QString::fromUtf8("y\u02E3"), {A::Binary, E::Pow});
        sci(QString::fromUtf8("10\u02E3"), 1, 5, {A::Unary, E::Exp10},
            QString::fromUtf8("2\u02E3"), {A::Unary, E::Exp2});

        sci(QString::fromUtf8("\u00B9\u2044\u2093"), 2, 0, {A::Unary, E::Inv});
        sci(QString::fromUtf8("\u00B2\u221Ax"), 2, 1, {A::Unary, E::Sqrt});
        sci(QString::fromUtf8("\u00B3\u221Ax"), 2, 2, {A::Unary, E::Cbrt});
        sci(QString::fromUtf8("\u02B8\u221Ax"), 2, 3, {A::Binary, E::Root});
        sci("ln", 2, 4, {A::Unary, E::Ln},
            QString::fromUtf8("log\u1D67"), {A::Binary, E::LogY});
        sci(QString::fromUtf8("log\u2081\u2080"), 2, 5, {A::Unary, E::Log10},
            QString::fromUtf8("log\u2082"), {A::Unary, E::Log2});

        const QString inv = QString::fromUtf8("\u207B\u00B9");
        sci("x!", 3, 0, {A::Unary, E::Fact});
        sci("sin", 3, 1, {A::Unary, E::Sin}, "sin" + inv, {A::Unary, E::Asin});
        sci("cos", 3, 2, {A::Unary, E::Cos}, "cos" + inv, {A::Unary, E::Acos});
        sci("tan", 3, 3, {A::Unary, E::Tan}, "tan" + inv, {A::Unary, E::Atan});
        sci("e", 3, 4, {A::Const, E::E});
        sci("EE", 3, 5, {A::EE});

        sci("Rad", 4, 0, {A::Angle});
        sci("sinh", 4, 1, {A::Unary, E::Sinh}, "sinh" + inv, {A::Unary, E::Asinh});
        sci("cosh", 4, 2, {A::Unary, E::Cosh}, "cosh" + inv, {A::Unary, E::Acosh});
        sci("tanh", 4, 3, {A::Unary, E::Tanh}, "tanh" + inv, {A::Unary, E::Atanh});
        sci(QString::fromUtf8("\u03C0"), 4, 4, {A::Const, E::Pi});
        sci("Rand", 4, 5, {A::Const, E::Rand});
    }

    // ---------- раскладка ----------
    void layoutKeys() {
        const int W = width(), H = height();
        const bool land = W > H;
        const int minSide = std::min(W, H);
        const int m = std::max(14, int(minSide * (land ? 0.035 : 0.045)));
        const int g = std::max(8, int(minSide * (land ? 0.022 : 0.026)));

        // --- шапка: переключатель режимов и кнопка истории ---
        const int hh = land ? 34 : 40;
        const int y0 = land ? 8 : 14;
        seg_->setGeometry(m, y0, std::min(W - 2 * m - hh - 10, 300), hh);
        historyBtn_->setGeometry(W - m - hh, y0, hh, hh);

        // --- клавиатура ---
        int top;
        if (!land) {
            const int bw = (W - 2 * m - 3 * g) / 4;
            const int bh = std::min(int(bw * 0.84), (int(H * 0.60) - 4 * g) / 5);
            top = H - m - (5 * bh + 4 * g);
            for (Key& k : keys_) {
                k.btn->setVisible(!k.sci);
                if (k.sci) continue;
                const int w = k.span * bw + (k.span - 1) * g;
                k.btn->setGeometry(m + k.c * (bw + g), top + k.r * (bh + g), w, bh);
            }
        } else {
            const int bw = (W - 2 * m - 9 * g) / 10;
            const int bh = std::min((int(H * 0.62) - 4 * g) / 5, int(bw * 0.78));
            top = H - m - (5 * bh + 4 * g);
            for (Key& k : keys_) {
                k.btn->setVisible(true);
                const int col = k.sci ? k.c : k.c + 6;
                const int w = k.span * bw + (k.span - 1) * g;
                k.btn->setGeometry(m + col * (bw + g), top + k.r * (bh + g), w, bh);
            }
        }

        // --- карточка дисплея ---
        const int cardY = y0 + hh + 12;
        const int cardH = std::max(60, top - 14 - cardY);
        const int cw = W - 2 * m;
        const int pad = 16;
        cardRect_ = QRectF(m, cardY, cw, cardH);

        const int exprH = std::clamp(cardH / 5, 18, 26);
        QFont ef = exprLabel_->font();
        ef.setPixelSize(std::clamp(exprH - 6, 12, 17));
        exprLabel_->setFont(ef);
        exprLabel_->setGeometry(m + pad, cardY + 8, cw - 2 * pad, exprH);
        const int dispY = cardY + 8 + exprH;
        const int dispH = std::max(30, cardH - (dispY - cardY) - 8);
        display_->setGeometry(m + pad, dispY, cw - 2 * pad, dispH);
        maxFont_ = std::clamp(int(dispH * 0.78), 28, std::max(28, int(minSide * 0.2)));
        fitDisplayFont();

        // --- режим конвертера: две карточки (ввод и результат) ---
        const int gap = 10;
        const int h2 = std::max(30, (cardH - gap) / 2);
        cardTop_ = QRectF(m, cardY, cw, h2);
        cardBottom_ = QRectF(m, cardY + h2 + gap, cw, h2);
        const int sw = land ? 28 : 36;
        const int uw = std::clamp(cw / 4, 78, 130);
        const int vx = m + pad + sw;
        const int vw = std::max(40, m + cw - pad - uw - 8 - vx);
        topVal_->setGeometry(vx, cardY, vw, h2);
        topUnit_->setGeometry(m + cw - pad - uw, cardY, uw, h2);
        bottomVal_->setGeometry(vx, cardY + h2 + gap, vw, h2);
        bottomUnit_->setGeometry(m + cw - pad - uw, cardY + h2 + gap, uw, h2);
        swapBtn_->setGeometry(m + pad - 6, cardY + h2 + gap / 2 - sw / 2, sw, sw);
        convFont_ = std::clamp(int(h2 * 0.5), 20, std::max(20, int(minSide * 0.12)));
        QFont uf = topUnit_->font();
        uf.setPixelSize(std::clamp(int(h2 * 0.26), 13, 22));
        topUnit_->setFont(uf);
        bottomUnit_->setFont(uf);

        updateConverter();
        setExpr(QString::fromUtf8(tracker_.header().c_str()));
        update();
    }

    void setExpr(const QString& t) {
        const QFontMetrics fm(exprLabel_->font());
        exprLabel_->setText(fm.elidedText(t, Qt::ElideLeft, std::max(20, exprLabel_->width() - 4)));
    }

    static void fitLabel(QLabel* l, int maxPx, int minPx) {
        QFont f = l->font();
        const QString t = l->text();
        const int avail = std::max(20, l->width() - 4);
        int px = maxPx;
        for (; px > minPx; px -= 2) {
            f.setPixelSize(px);
            if (QFontMetrics(f).horizontalAdvance(t) <= avail) break;
        }
        f.setPixelSize(px);
        l->setFont(f);
    }

    void fitDisplayFont() { fitLabel(display_, maxFont_, 22); }

    // ---------- режим «Конвертер» ----------
    int catCount() const { return 2 + int(units::categories().size()); }

    QString catTitle(int c) const {
        if (c == 0) return QStringLiteral("Валюта");
        if (c == 1) return QStringLiteral("Крипто");
        return QString::fromUtf8(units::categories()[c - 2].title);
    }

    // «деньги»: обычные валюты и крипто можно переводить друг в друга через доллар
    static bool isMoney(int cat) { return cat == 0 || cat == 1; }
    static bool compatible(int a, int b) { return a == b || (isMoney(a) && isMoney(b)); }

    // пара по умолчанию для категории
    void defaultPair(int cat, Sel& a, Sel& b) const {
        a.cat = b.cat = cat;
        if (cat == 0) {
            a.id = QStringLiteral("USD");
            b.id = QStringLiteral("UAH");
        } else if (cat == 1) {
            a.id = QStringLiteral("BTC");
            b.id = QStringLiteral("ETH");
        } else {
            const auto& us = units::categories()[cat - 2].units;
            a.id = QString::fromUtf8(us[0].id);
            b.id = QString::fromUtf8(us[1].id);
        }
    }

    // цена одной единицы в долларах: монета (CoinGecko) или обычная валюта (курс НБУ)
    double usdPriceOf(const Sel& s) const {
        if (s.cat == 1) return crypto_.usdOf(s.id);
        const double uahPerUsd = rates_.usd();
        const double uahPerUnit = rates_.rateOf(s.id);
        return (uahPerUsd > 0 && uahPerUnit > 0) ? uahPerUnit / uahPerUsd : 0.0;
    }

    static QString cryptoInfoText(const CryptoInfo& c) {
        const std::string p = c.usd >= 1 ? E::formatFixed(c.usd, 2) : E::fmtNumber(c.usd);
        char b[32];
        std::snprintf(b, sizeof b, "%.2f", std::fabs(c.change));
        const QString pct = QString::fromLatin1(b).replace('.', ',');
        return QStringLiteral("$") + QString::fromUtf8(p.c_str()) + QStringLiteral("  ") +
               (c.change >= 0 ? QStringLiteral("\u25B2") : QStringLiteral("\u25BC")) + pct + QStringLiteral("%");
    }

    QVector<UnitItem> unitItems(int cat) const {
        QVector<UnitItem> out;
        if (cat == 0) {
            for (const CurrencyInfo& c : rates_.list) out.push_back(UnitItem{c.cc, c.name, QString()});
            if (out.isEmpty()) out.push_back(UnitItem{QStringLiteral("UAH"), QStringLiteral("Українська гривня"), QString()});
        } else if (cat == 1) {
            for (const CryptoInfo& c : crypto_.list)
                out.push_back(UnitItem{c.symbol, c.name, cryptoInfoText(c)});
        } else {
            for (const units::Unit& u : units::categories()[cat - 2].units)
                out.push_back(UnitItem{QString::fromUtf8(u.id), QString::fromUtf8(u.name), QString()});
        }
        return out;
    }

    bool convertValue(double v, double& out) const {
        // единицы одной категории (длина, масса, ...)
        if (from_.cat >= 2 && from_.cat == to_.cat)
            return units::convert(units::categories()[from_.cat - 2], from_.id.toStdString(),
                                  to_.id.toStdString(), v, out);
        // валюта и крипто — в любых сочетаниях, через доллар
        if (isMoney(from_.cat) && isMoney(to_.cat)) {
            const double pf = usdPriceOf(from_), pt = usdPriceOf(to_);
            if (pf <= 0 || pt <= 0) return false;
            out = v * pf / pt;
            return true;
        }
        return false;
    }

    void setConverter(bool on) {
        if (converter_ && !on) commitConversion();  // записываем конвертацию перед выходом
        converter_ = on;
        seg_->setCurrent(on ? 1 : 0);
        display_->setVisible(!on);
        exprLabel_->setVisible(!on);
        for (QWidget* w : converterWidgets()) w->setVisible(on);
        layoutKeys();
        refresh();
    }

    void updateConverter() {
        if (!converter_) return;
        const QString chevron = QStringLiteral(" \u25BE");
        topUnit_->setText(from_.id + chevron);
        bottomUnit_->setText(to_.id + chevron);
        topVal_->setText(QString::fromUtf8(eng_.display().c_str()));

        QString bottom = QStringLiteral("\u2014");
        double out = 0;
        if (!eng_.inError() && convertValue(eng_.value(), out)) {
            const bool fiatTarget = (to_.cat == 0);  // обычные деньги — два знака после запятой
            const std::string s = fiatTarget ? E::formatFixed(out, 2) : E::fmtNumber(out);
            bottom = QString::fromUtf8(s.c_str());
        }
        bottomVal_->setText(bottom);
        fitLabel(topVal_, convFont_, 16);
        fitLabel(bottomVal_, convFont_, 16);
    }

    // Окно выбора единиц: вкладки-категории, список, поиск
    void openPicker(bool editFrom) {
        commitConversion();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Единицы измерения"));
        dlg.resize(std::min(std::max(width(), 340), 460), std::max(480, std::min(height(), 720)));
        dlg.setStyleSheet(QStringLiteral(
            "QDialog { background: #14171D; }"
            "QLabel { color: #8A93A6; }"
            "QListWidget { background: #14171D; color: white; border: none; font-size: 16px; outline: 0; }"
            "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #232833; }"
            "QListWidget::item:selected { background: #232833; }"
            "QLineEdit { background: #232833; color: white; border: none; border-radius: 16px;"
            "            padding: 8px 14px; font-size: 15px; }"
            "QPushButton { background: transparent; color: #8676FF; border: none; border-radius: 16px;"
            "              padding: 6px 14px; font-size: 15px; }"
            "QPushButton:checked { background: #6A5AF0; color: white; }"
            "QScrollArea { background: transparent; border: none; }"));

        auto* lay = new QVBoxLayout(&dlg);
        auto* scroll = new QScrollArea(&dlg);
        scroll->setWidgetResizable(true);
        scroll->setFixedHeight(56);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* tabsW = new QWidget;
        auto* tabsL = new QHBoxLayout(tabsW);
        tabsL->setContentsMargins(0, 0, 0, 0);
        auto* list = new QListWidget(&dlg);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)  // прокрутка пальцем
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
        QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
#endif
        auto* search = new QLineEdit(&dlg);
        search->setPlaceholderText(QStringLiteral("Поиск по единицам"));
        search->setClearButtonEnabled(true);

        int viewCat = (editFrom ? from_ : to_).cat;
        int chosenCat = -1;
        QString chosenId;
        QVector<QPushButton*> tabs;

        auto fill = [&] {
            list->clear();
            const QString q = search->text().trimmed();
            const QString current = (editFrom ? from_ : to_).id;
            for (const UnitItem& u : unitItems(viewCat)) {
                if (!q.isEmpty() && !u.id.contains(q, Qt::CaseInsensitive) &&
                    !u.name.contains(q, Qt::CaseInsensitive))
                    continue;
                const bool sel = (viewCat == (editFrom ? from_ : to_).cat && u.id == current);
                const QString text = (sel ? QStringLiteral("\u2713  ") : QStringLiteral("      ")) +
                                     u.name + QStringLiteral("\n      ") + u.id +
                                     (u.info.isEmpty() ? QString() : QStringLiteral("   \u00B7   ") + u.info);
                auto* it = new QListWidgetItem(text, list);
                it->setData(Qt::UserRole, u.id);
                if (sel) it->setForeground(QColor(0x9A, 0x8D, 0xFF));
            }
        };

        for (int c = 0; c < catCount(); ++c) {
            auto* b = new QPushButton(catTitle(c), tabsW);
            b->setCheckable(true);
            b->setChecked(c == viewCat);
            b->setCursor(Qt::PointingHandCursor);
            tabs.push_back(b);
            tabsL->addWidget(b);
            connect(b, &QPushButton::clicked, &dlg, [&, c] {
                viewCat = c;
                for (int i = 0; i < tabs.size(); ++i) tabs[i]->setChecked(i == c);
                fill();
            });
        }
        tabsL->addStretch();
        scroll->setWidget(tabsW);

        connect(search, &QLineEdit::textChanged, &dlg, [&] { fill(); });
        connect(list, &QListWidget::itemClicked, &dlg, [&](QListWidgetItem* it) {
            chosenId = it->data(Qt::UserRole).toString();
            chosenCat = viewCat;
            dlg.accept();
        });

        lay->addWidget(scroll);
        lay->addWidget(list, 1);
        lay->addWidget(search);
        fill();

        if (dlg.exec() == QDialog::Accepted && chosenCat >= 0) {
            Sel chosen;
            chosen.cat = chosenCat;
            chosen.id = chosenId;
            Sel& self = editFrom ? from_ : to_;
            Sel& other = editFrom ? to_ : from_;
            self = chosen;  // выбранная строка меняется, вторая остаётся как была
            if (!compatible(chosen.cat, other.cat)) {  // например, валюта → длина: подбираем пару
                Sel d1, d2;
                defaultPair(chosen.cat, d1, d2);
                other = (d1.id == chosen.id) ? d2 : d1;
            }
        }
        refresh();
        scheduleConvCommit();
    }

    // ---------- действия ----------
    void press(int idx) {
        const Key& k = keys_[idx];
        dispatch((second_ && k.hasSecond) ? k.act2 : k.act);
        refresh();
        scheduleConvCommit();
    }

    // Ввод с клавиатуры: найти соответствующую кнопку, подсветить и выполнить
    void trigger(Act a) {
        for (const Key& k : keys_) {
            if (k.act.a == a.a && k.act.arg == a.arg) {
                if (k.btn->isVisible()) k.btn->flash();
                break;
            }
        }
        dispatch(a);
        refresh();
        scheduleConvCommit();
    }

    static Ev toEv(A a) {
        switch (a) {
            case A::Digit:   return Ev::Digit;
            case A::Decimal: return Ev::Decimal;
            case A::EE:      return Ev::EE;
            case A::Negate:  return Ev::Negate;
            case A::Percent: return Ev::Percent;
            case A::Const:   return Ev::Const;
            case A::MRecall: return Ev::MRecall;
            case A::Unary:   return Ev::Unary;
            case A::Binary:  return Ev::Binary;
            case A::LParen:  return Ev::LParen;
            case A::RParen:  return Ev::RParen;
            case A::Equals:  return Ev::Equals;
            case A::Clear:   return Ev::Clear;
            default:         return Ev::Other;
        }
    }

    // Выполнить действие и записать его в историю (если вычисление завершено)
    void dispatch(Act a) {
        const ExprTracker::Pre pre = tracker_.before(eng_);
        runEngine(a);
        HistoryEntry he;
        if (tracker_.after(toEv(a.a), a.arg, pre, eng_, he)) {
            HistoryItem h;
            h.expr = QString::fromUtf8(he.expr.c_str());
            h.result = QString::fromUtf8(he.result.c_str());
            h.value = he.value;
            pushHistory(h);
        }
    }

    void doBackspace() {
        const ExprTracker::Pre pre = tracker_.before(eng_);
        eng_.backspace();
        HistoryEntry he;
        tracker_.after(Ev::Backspace, 0, pre, eng_, he);
        refresh();
    }

    void runEngine(Act a) {
        switch (a.a) {
            case A::Digit:    eng_.digit(a.arg); break;
            case A::Decimal:  eng_.decimal(); break;
            case A::Clear:    eng_.clear(); break;
            case A::Negate:   eng_.negate(); break;
            case A::Percent:  eng_.percent(); break;
            case A::Binary:   eng_.binary(E::Op(a.arg)); break;
            case A::Equals:   eng_.equals(); break;
            case A::LParen:   eng_.openParen(); break;
            case A::RParen:   eng_.closeParen(); break;
            case A::MClear:   eng_.memClear(); break;
            case A::MAdd:     eng_.memAdd(); break;
            case A::MSub:     eng_.memSub(); break;
            case A::MRecall:  eng_.memRecall(); break;
            case A::Second:   second_ = !second_; break;
            case A::Unary:    eng_.unary(E::Fn(a.arg)); break;
            case A::Const:    eng_.constant(E::Const(a.arg)); break;
            case A::EE:       eng_.ee(); break;
            case A::Angle:    eng_.toggleAngle(); break;
        }
    }

    // ---------- история ----------
    void pushHistory(HistoryItem h) {
        h.ts = QDateTime::currentSecsSinceEpoch();
        history_.prepend(h);
        while (history_.size() > 200) history_.removeLast();
        saveHistory();
    }

    void saveHistory() const {
        QJsonArray arr;
        for (const HistoryItem& h : history_) {
            QJsonObject o;
            o.insert(QStringLiteral("k"), h.conv);
            o.insert(QStringLiteral("e"), h.expr);
            o.insert(QStringLiteral("r"), h.result);
            o.insert(QStringLiteral("v"), h.value);
            o.insert(QStringLiteral("c"), h.cat);
            o.insert(QStringLiteral("c2"), h.catTo);
            o.insert(QStringLiteral("f"), h.from);
            o.insert(QStringLiteral("t"), h.to);
            o.insert(QStringLiteral("ts"), double(h.ts));
            arr.push_back(o);
        }
        QSettings s;
        s.setValue("history", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }

    void loadHistory() {
        QSettings s;
        const QJsonArray arr = QJsonDocument::fromJson(s.value("history").toString().toUtf8()).array();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            HistoryItem h;
            h.conv = o.value(QStringLiteral("k")).toBool();
            h.expr = o.value(QStringLiteral("e")).toString();
            h.result = o.value(QStringLiteral("r")).toString();
            h.value = o.value(QStringLiteral("v")).toDouble();
            h.cat = o.value(QStringLiteral("c")).toInt();
            h.catTo = o.contains(QStringLiteral("c2")) ? o.value(QStringLiteral("c2")).toInt() : h.cat;
            h.from = o.value(QStringLiteral("f")).toString();
            h.to = o.value(QStringLiteral("t")).toString();
            h.ts = qint64(o.value(QStringLiteral("ts")).toDouble());
            if (!h.expr.isEmpty()) history_.push_back(h);
        }
    }

    // Конвертации записываются, когда ввод остановился (через 2,5 с), при смене единиц и выходе из режима
    void scheduleConvCommit() {
        if (converter_) convTimer_.start(2500);
    }

    void commitConversion() {
        convTimer_.stop();
        if (!converter_ || eng_.inError()) return;
        const double v = eng_.value();
        double out = 0;
        if (v == 0 || !convertValue(v, out)) return;

        HistoryItem h;
        h.conv = true;
        h.expr = QString::fromUtf8(eng_.display().c_str()) + QLatin1Char(' ') + from_.id;
        h.result = bottomVal_->text() + QLatin1Char(' ') + to_.id;
        h.value = v;
        h.cat = from_.cat;
        h.catTo = to_.cat;
        h.from = from_.id;
        h.to = to_.id;
        if (!history_.isEmpty() && history_.first().conv && history_.first().expr == h.expr &&
            history_.first().result == h.result)
            return;  // такая запись уже есть
        pushHistory(h);
    }

    void restoreHistory(const HistoryItem& h) {
        tracker_.reset();
        if (h.conv) {
            if (!converter_) setConverter(true);
            if (h.cat >= 0 && h.cat < catCount() && h.catTo >= 0 && h.catTo < catCount()) {
                from_.cat = h.cat;
                from_.id = h.from;
                to_.cat = h.catTo;
                to_.id = h.to;
            }
        } else if (converter_) {
            setConverter(false);
        }
        eng_.setValue(h.value);
        refresh();
    }

    void openHistory() {
        commitConversion();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("История"));
        dlg.resize(std::min(std::max(width(), 340), 460), std::max(480, std::min(height(), 720)));
        dlg.setStyleSheet(QStringLiteral(
            "QDialog { background: #14171D; }"
            "QLabel { color: #8A93A6; font-size: 15px; background: transparent; }"
            "QListWidget { background: #14171D; color: white; border: none; outline: 0; }"
            "QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #232833; }"
            "QListWidget::item:selected { background: #232833; }"
            "QPushButton { background: transparent; color: #8676FF; border: none; padding: 6px 14px;"
            "              font-size: 15px; }"));

        auto* lay = new QVBoxLayout(&dlg);
        auto* head = new QHBoxLayout;
        auto* clearBtn = new QPushButton(QStringLiteral("Очистить"), &dlg);
        clearBtn->setCursor(Qt::PointingHandCursor);
        head->addStretch();
        head->addWidget(clearBtn);
        auto* list = new QListWidget(&dlg);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)  // прокрутка пальцем
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
#endif
        auto* empty = new QLabel(QStringLiteral("История пуста"), &dlg);
        empty->setAlignment(Qt::AlignCenter);
        lay->addLayout(head);
        lay->addWidget(list, 1);
        lay->addWidget(empty, 1);

        int chosen = -1;
        auto fill = [&] {
            list->clear();
            for (int i = 0; i < history_.size(); ++i) {
                const HistoryItem& h = history_[i];
                const QString when = QDateTime::fromSecsSinceEpoch(h.ts).toString(QStringLiteral("dd.MM HH:mm"));
                const QString html =
                    QStringLiteral("<div style='color:#8A93A6; font-size:13px;'>%1 &nbsp;&middot;&nbsp; %2</div>"
                                   "<div style='color:white; font-size:22px;'>= %3</div>")
                        .arg(h.expr.toHtmlEscaped(), when, h.result.toHtmlEscaped());
                auto* it = new QListWidgetItem(list);
                auto* lbl = new QLabel(html);
                lbl->setTextFormat(Qt::RichText);
                lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
                it->setSizeHint(lbl->sizeHint() + QSize(0, 8));
                it->setData(Qt::UserRole, i);
                list->setItemWidget(it, lbl);
            }
            list->setVisible(!history_.isEmpty());
            empty->setVisible(history_.isEmpty());
            clearBtn->setEnabled(!history_.isEmpty());
        };
        connect(list, &QListWidget::itemClicked, &dlg, [&](QListWidgetItem* it) {
            chosen = it->data(Qt::UserRole).toInt();
            dlg.accept();
        });
        connect(clearBtn, &QPushButton::clicked, &dlg, [&] {
            history_.clear();
            saveHistory();
            fill();
        });
        fill();
        dlg.exec();

        if (chosen >= 0 && chosen < history_.size()) {
            const HistoryItem h = history_[chosen];  // копия: restore может изменить историю
            restoreHistory(h);
        }
    }

    void refresh() {
        display_->setText(QString::fromUtf8(eng_.display().c_str()));
        fitDisplayFont();

        const int active = eng_.activeOp();
        for (Key& k : keys_) {
            const Act& a = (second_ && k.hasSecond) ? k.act2 : k.act;
            bool hl = false;
            QString text = (second_ && k.hasSecond) ? k.label2 : k.label;

            if (k.act.a == A::Clear) text = eng_.showClear() ? "C" : "AC";
            if (k.act.a == A::Angle) text = eng_.isDeg() ? "Rad" : "Deg";
            if (k.act.a == A::LParen && eng_.parenDepth() > 0)
                text = "(" + QString::number(eng_.parenDepth());
            if (k.act.a == A::Second) hl = second_;
            if (a.a == A::Binary && a.arg <= E::Div && !k.sci) hl = (a.arg == active);

            if (k.btn->text() != text) k.btn->setText(text);
            k.btn->setHighlighted(hl);
        }

        setExpr(QString::fromUtf8(tracker_.header().c_str()));
        updateConverter();
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("IOSCalculator");
    QCoreApplication::setApplicationName("IOSCalculator");

    QFont f;
    f.setFamilies({"SF Pro Display", "Helvetica Neue", "Segoe UI", "Roboto", "Noto Sans", "Arial"});
    app.setFont(f);

    Calculator w;
    w.show();
    return app.exec();
}
