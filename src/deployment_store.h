#ifndef DEPLOYMENT_STORE_H
#define DEPLOYMENT_STORE_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QString>

/**
 * @brief Persists deployed workflows to disk
 *
 * Stores workflow JSON files in ~/.local/share/logos/deployed-workflows/
 * so they survive process restarts. The scheduler loads all deployed
 * workflows on startup and resumes their timer schedules.
 */
class DeploymentStore : public QObject
{
    Q_OBJECT

public:
    explicit DeploymentStore(QObject* parent = nullptr);

    void save(const QString& workflowId, const QJsonObject& workflow);
    QJsonObject load(const QString& workflowId) const;
    void remove(const QString& workflowId);
    QMap<QString, QJsonObject> loadAll() const;

private:
    QString storePath() const;
    QString filePath(const QString& workflowId) const;
};

#endif // DEPLOYMENT_STORE_H
