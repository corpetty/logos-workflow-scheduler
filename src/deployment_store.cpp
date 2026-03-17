#include "deployment_store.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDebug>

DeploymentStore::DeploymentStore(QObject* parent)
    : QObject(parent)
{
    QDir().mkpath(storePath());
}

QString DeploymentStore::storePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/deployed-workflows";
}

QString DeploymentStore::filePath(const QString& workflowId) const
{
    return storePath() + "/" + workflowId + ".json";
}

void DeploymentStore::save(const QString& workflowId, const QJsonObject& workflow)
{
    QFile file(filePath(workflowId));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(workflow).toJson(QJsonDocument::Indented));
        qDebug() << "[deployment_store] Saved:" << workflowId;
    } else {
        qWarning() << "[deployment_store] Failed to save:" << workflowId;
    }
}

QJsonObject DeploymentStore::load(const QString& workflowId) const
{
    QFile file(filePath(workflowId));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        return doc.object();
    }
    return {};
}

void DeploymentStore::remove(const QString& workflowId)
{
    QFile::remove(filePath(workflowId));
    qDebug() << "[deployment_store] Removed:" << workflowId;
}

QMap<QString, QJsonObject> DeploymentStore::loadAll() const
{
    QMap<QString, QJsonObject> result;
    QDir dir(storePath());
    QStringList filters;
    filters << "*.json";

    for (const auto& entry : dir.entryInfoList(filters, QDir::Files)) {
        QString id = entry.baseName();
        QJsonObject workflow = load(id);
        if (!workflow.isEmpty()) {
            result[id] = workflow;
        }
    }
    return result;
}
