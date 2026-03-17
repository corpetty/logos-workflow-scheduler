#include "cron_parser.h"
#include <QStringList>
#include <QRegularExpression>
#include <QDebug>

bool CronParser::matchesNow(const QString& cronExpression)
{
    return matches(cronExpression, QDateTime::currentDateTime());
}

bool CronParser::matches(const QString& cronExpression, const QDateTime& dateTime)
{
    QStringList fields = cronExpression.trimmed().split(QRegularExpression("\\s+"));
    if (fields.size() != 5) return false;

    int minute     = dateTime.time().minute();
    int hour       = dateTime.time().hour();
    int dayOfMonth = dateTime.date().day();
    int month      = dateTime.date().month();
    int dayOfWeek  = dateTime.date().dayOfWeek() % 7;  // 0=Sunday

    return fieldMatches(fields[0], minute, 0, 59)
        && fieldMatches(fields[1], hour, 0, 23)
        && fieldMatches(fields[2], dayOfMonth, 1, 31)
        && fieldMatches(fields[3], month, 1, 12)
        && fieldMatches(fields[4], dayOfWeek, 0, 6);
}

bool CronParser::fieldMatches(const QString& field, int value, int minVal, int maxVal)
{
    if (field == "*") return true;

    // Handle lists: "1,5,10"
    QStringList parts = field.split(",", Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        QString p = part.trimmed();

        // Handle steps: "*/5" or "1-10/2"
        int step = 1;
        int slashIdx = p.indexOf('/');
        if (slashIdx >= 0) {
            step = p.mid(slashIdx + 1).toInt();
            p = p.left(slashIdx);
            if (step <= 0) step = 1;
        }

        // Handle ranges: "1-5"
        int dashIdx = p.indexOf('-');
        if (dashIdx >= 0) {
            int rangeStart = p.left(dashIdx).toInt();
            int rangeEnd = p.mid(dashIdx + 1).toInt();
            if (value >= rangeStart && value <= rangeEnd) {
                if ((value - rangeStart) % step == 0) return true;
            }
            continue;
        }

        // Wildcard with step: "*/5"
        if (p == "*") {
            if ((value - minVal) % step == 0) return true;
            continue;
        }

        // Exact value
        bool ok;
        int exact = p.toInt(&ok);
        if (ok && exact == value) return true;
    }

    return false;
}

QString CronParser::validate(const QString& cronExpression)
{
    QStringList fields = cronExpression.trimmed().split(QRegularExpression("\\s+"));
    if (fields.size() != 5) {
        return QString("Expected 5 fields, got %1").arg(fields.size());
    }
    // Basic validation — more thorough checking could be added
    return QString();
}
