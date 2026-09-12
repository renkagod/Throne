#pragma once

#include <QAbstractItemModel>
#include <QList>
#include <QString>
#include <memory>
#include <vector>

#include "include/stats/connections/connectionLister.hpp"

namespace ConnectionsTree {

struct TreeItem {
    virtual ~TreeItem() = default;
    virtual bool isProcess() const = 0;
};

struct ProcessGroupItem;

struct ConnectionLeafItem : public TreeItem {
    bool isProcess() const override { return false; }
    ProcessGroupItem *parent = nullptr;
    Stats::ConnectionMetadata meta;
    QString destText;
    QString protocolText;
    int rowInParent = 0;
};

struct ProcessGroupItem : public TreeItem {
    bool isProcess() const override { return true; }
    QString processName;
    std::vector<std::unique_ptr<ConnectionLeafItem>> children;
    long long totalUpload = 0;
    long long totalDownload = 0;
    long long totalUploadSpeed = 0;
    long long totalDownloadSpeed = 0;
    QString commonOutbound;
    bool sameOutbound = true;
    int row = 0;

    QStringList connectionIds() const {
        QStringList ids;
        ids.reserve(children.size());
        for (const auto &child : children) {
            if (!child->meta.id.isEmpty()) ids << child->meta.id;
        }
        return ids;
    }
};

} // namespace ConnectionsTree

class ConnectionsTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles {
        ConnIdRole = Qt::UserRole,
        ConnIdsRole,
        IsProcessRole,
        ProcessNameRole,
    };

    enum Column {
        ColTarget = 0,    // Process name (count) at root, Destination (Domain) at leaf
        ColSource,        // Source IP / Local (can be hidden if LAN disabled)
        ColProtocol,      // Protocol (network / protocol)
        ColOutbound,      // Outbound tag
        ColTraffic,       // Total traffic
        ColSpeed,         // Total speed
        ColClose,         // Close button
        ColumnCount
    };

    explicit ConnectionsTreeModel(QObject *parent = nullptr);
    ~ConnectionsTreeModel() override = default;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setConnections(const QList<Stats::ConnectionMetadata> &connections);

    bool isProcessIndex(const QModelIndex &index) const;
    QString processNameAt(const QModelIndex &index) const;
    const Stats::ConnectionMetadata *metaAt(const QModelIndex &index) const;
    QStringList connectionIdsAt(const QModelIndex &index) const;

    const ConnectionsTree::ProcessGroupItem *groupAt(int row) const;
    int groupCount() const;

private:
    std::vector<std::unique_ptr<ConnectionsTree::ProcessGroupItem>> m_groups;
};
