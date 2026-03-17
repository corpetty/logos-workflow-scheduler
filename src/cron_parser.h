#ifndef CRON_PARSER_H
#define CRON_PARSER_H

#include <QString>
#include <QDateTime>

/**
 * @brief Parses 5-field cron expressions and checks if they match the current time
 *
 * Format: minute hour dayOfMonth month dayOfWeek
 * Supports: *, exact values, ranges (N-M), lists (N,M,O), steps (asterisk/N)
 *
 * Ported from bridge/scheduler.js
 */
class CronParser
{
public:
    /**
     * @brief Check if a cron expression matches the current local time
     */
    static bool matchesNow(const QString& cronExpression);

    /**
     * @brief Check if a cron expression matches a specific time
     */
    static bool matches(const QString& cronExpression, const QDateTime& dateTime);

    /**
     * @brief Validate a cron expression
     * @return Empty string if valid, error message if invalid
     */
    static QString validate(const QString& cronExpression);

private:
    static bool fieldMatches(const QString& field, int value, int minVal, int maxVal);
};

#endif // CRON_PARSER_H
