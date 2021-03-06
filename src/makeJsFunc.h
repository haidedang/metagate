#ifndef MAKEJSFUNCPARAMETERS_H
#define MAKEJSFUNCPARAMETERS_H

#include <QString>
#include <QJsonDocument>

#include <string>

#include "TypedException.h"
#include "check.h"
#include "Log.h"

template<typename T>
struct Opt {
    T value;
    bool isSet = false;

    Opt() = default;

    explicit Opt(const T &value)
        : value(value)
        , isSet(true)
    {}

    Opt<T>& operator=(const T &val) {
        value = val;
        isSet = true;
        return *this;
    }

    const T& get() const {
        CHECK(isSet, "Not set");
        return value;
    }

    const T& getWithoutCheck() const {
        return value;
    }
};

inline QString toJsString(const QString &arg) {
    QString copy = arg;
    copy.replace("\\", "\\\\");
    copy.replace('\"', "\\\"");
    copy.replace("\n", "\\n");
    copy.replace("\r", "");
    return "\"" + copy + "\"";
}

inline QString toJsString(const QJsonDocument &arg) {
    QString json = arg.toJson(QJsonDocument::Compact);
    return toJsString(json);
}

inline QString toJsString(const std::string &arg) {
    return toJsString(QString::fromStdString(arg));
}

inline QString toJsString(const int &arg) {
    return QString::fromStdString(std::to_string(arg));
}

inline QString toJsString(const long int &arg) {
    return QString::fromStdString(std::to_string(arg));
}

inline QString toJsString(const long long int &arg) {
    return QString::fromStdString(std::to_string(arg));
}

inline QString toJsString(bool arg) {
    if (arg) {
        return "true";
    } else {
        return "false";
    }
}

inline QString toJsString(const size_t &arg) {
    return QString::fromStdString(std::to_string(arg));
}

template<typename Arg>
inline QString append(const Arg &arg) {
    static_assert(!std::is_same<typename std::decay<decltype(arg)>::type, char const*>::value, "const char* not allowed");
    return toJsString(arg);
}

template<typename Arg, typename... Args>
inline QString append(const Arg &arg, Args&& ...args) {
    return append(arg) + ", " + append(std::forward<Args>(args)...);
}

template<typename Arg>
inline QString appendOpt(bool withoutCheck, const Arg &argOpt) {
    const auto &arg = withoutCheck ? argOpt.getWithoutCheck() : argOpt.get();
    static_assert(!std::is_same<typename std::decay<decltype(arg)>::type, char const*>::value, "const char* not allowed");
    return toJsString(arg);
}

template<typename Arg, typename... Args>
inline QString appendOpt(bool withoutCheck, const Arg &arg, Args&& ...args) {
    return appendOpt(withoutCheck, arg) + ", " + appendOpt(withoutCheck, std::forward<Args>(args)...);
}

template<bool isLastArg, typename... Args>
inline QString makeJsFunc2(const QString &function, const QString &lastArg, const TypedException &exception, Args&& ...args) {
    const bool withoutCheck = exception.numError != TypeErrors::NOT_ERROR;
    QString jScript = function + "(";
    jScript += appendOpt<Args...>(withoutCheck, std::forward<Args>(args)...) + ", ";
    jScript += append(exception.numError, exception.description);
    if (isLastArg) {
        jScript += ", " + toJsString(lastArg);
    }
    jScript += ");";
    return jScript;
}

template<bool isLastArg, typename... Args>
inline QString makeJsFunc3(const QString &function, const QString &lastArg, const TypedException &exception, Args&& ...args) {
    QString jScript = function + "(";
    jScript += append<Args..., int, const std::string&>(std::forward<Args>(args)..., (int)exception.numError, exception.description);
    if (isLastArg) {
        jScript += ", " + toJsString(lastArg);
    }
    jScript += ");";
    return jScript;
}

#endif // MAKEJSFUNCPARAMETERS_H
