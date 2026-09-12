#include "include/ui/utils/ConnectionsTreeModel.h"
#include "include/global/Utils.hpp"

#include <QApplication>
#include <QHash>

ConnectionsTreeModel::ConnectionsTreeModel(QObject *parent)
    : QAbstractItemModel(parent) {}

QModelIndex ConnectionsTreeModel::index(int row, int column, const QModelIndex &parent) const {
    if (column < 0 || column >= ColumnCount) return {};

    if (!parent.isValid()) {
        if (row < 0 || row >= m_groups.size()) return {};
        return createIndex(row, column, m_groups[row].get());
    }

    auto *item = static_cast<ConnectionsTree::TreeItem *>(parent.internalPointer());
    if (!item || !item->isProcess()) return {};

    auto *group = static_cast<ConnectionsTree::ProcessGroupItem *>(item);
    if (row < 0 || row >= group->children.size()) return {};

    return createIndex(row, column, group->children[row].get());
}

QModelIndex ConnectionsTreeModel::parent(const QModelIndex &child) const {
    if (!child.isValid()) return {};

    auto *item = static_cast<ConnectionsTree::TreeItem *>(child.internalPointer());
    if (!item || item->isProcess()) return {};

    auto *leaf = static_cast<ConnectionsTree::ConnectionLeafItem *>(item);
    if (!leaf->parent) return {};

    return createIndex(leaf->parent->row, 0, leaf->parent);
}

int ConnectionsTreeModel::rowCount(const QModelIndex &parent) const {
    if (!parent.isValid()) {
        return static_cast<int>(m_groups.size());
    }

    auto *item = static_cast<ConnectionsTree::TreeItem *>(parent.internalPointer());
    if (!item || !item->isProcess()) return 0;

    auto *group = static_cast<ConnectionsTree::ProcessGroupItem *>(item);
    return static_cast<int>(group->children.size());
}

int ConnectionsTreeModel::columnCount(const QModelIndex &) const {
    return ColumnCount;
}

Qt::ItemFlags ConnectionsTreeModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (index.column() == ColClose) return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant ConnectionsTreeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};

    auto *item = static_cast<ConnectionsTree::TreeItem *>(index.internalPointer());
    if (!item) return {};

    if (item->isProcess()) {
        auto *group = static_cast<ConnectionsTree::ProcessGroupItem *>(item);

        if (role == IsProcessRole) return true;
        if (role == ProcessNameRole) return group->processName;
        if (role == ConnIdsRole) return group->connectionIds();
        if (role == ConnIdRole) return group->connectionIds().join(QLatin1Char(','));

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColTarget:
                return QStringLiteral("%1 (%2)").arg(group->processName).arg(group->children.size());
            case ColSource:
                return QStringLiteral("-");
            case ColProtocol:
                return QStringLiteral("-");
            case ColOutbound:
                return group->sameOutbound ? group->commonOutbound : QStringLiteral("-");
            case ColTraffic:
                return ReadableSize(group->totalUpload) + "↑ " + ReadableSize(group->totalDownload) + "↓";
            case ColSpeed:
                return ReadableSize(group->totalUploadSpeed) + "/s↑ " + ReadableSize(group->totalDownloadSpeed) + "/s↓";
            default:
                return {};
            }
        }

        if (role == Qt::ToolTipRole) {
            if (index.column() == ColClose) {
                return tr("Close all connections for %1").arg(group->processName);
            }
            if (index.column() == ColTarget) {
                return tr("Process: %1\nActive connections: %2\nTotal traffic: %3↑ %4↓\nTotal speed: %5/s↑ %6/s↓")
                    .arg(group->processName)
                    .arg(group->children.size())
                    .arg(ReadableSize(group->totalUpload))
                    .arg(ReadableSize(group->totalDownload))
                    .arg(ReadableSize(group->totalUploadSpeed))
                    .arg(ReadableSize(group->totalDownloadSpeed));
            }
        }

        return {};
    }

    // Leaf connection
    auto *leaf = static_cast<ConnectionsTree::ConnectionLeafItem *>(item);
    const auto &c = leaf->meta;

    if (role == IsProcessRole) return false;
    if (role == ProcessNameRole) return leaf->parent ? leaf->parent->processName : c.process;
    if (role == ConnIdRole) return c.id;
    if (role == ConnIdsRole) return QStringList{c.id};

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTarget: return leaf->destText;
        case ColSource: return c.sourceDisplay;
        case ColProtocol: return leaf->protocolText;
        case ColOutbound: return c.outbound;
        case ColTraffic: return ReadableSize(c.upload) + "↑ " + ReadableSize(c.download) + "↓";
        case ColSpeed: return ReadableSize(c.uploadSpeed) + "/s↑ " + ReadableSize(c.downloadSpeed) + "/s↓";
        default: return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        if (index.column() == ColClose) {
            return tr("Close this connection");
        }
        if (index.column() == ColTarget) {
            return tr("Destination: %1\nProcess: %2\nProtocol: %3\nOutbound: %4")
                .arg(leaf->destText, c.process.isEmpty() ? tr("System") : c.process, leaf->protocolText, c.outbound);
        }
    }

    return {};
}

QVariant ConnectionsTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};

    if (role == Qt::DisplayRole) {
        switch (section) {
        case ColTarget: return tr("Process / Destination");
        case ColSource: return tr("Source");
        case ColProtocol: return tr("Protocol");
        case ColOutbound: return tr("Outbound");
        case ColTraffic: return tr("Traffic");
        case ColSpeed: return tr("Speed");
        default: return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (section) {
        case ColTarget: return tr("Click To Sort By Process");
        case ColSource: return tr("Click To Sort By Source");
        case ColProtocol: return tr("Click To Sort By Protocol");
        case ColOutbound: return tr("Click To Sort By Outbound");
        case ColTraffic: return tr("Click to sort by traffic; right-click to choose total/down/up");
        case ColSpeed: return tr("Click to sort by speed; right-click to choose total/down/up");
        default: return {};
        }
    }

    return {};
}

void ConnectionsTreeModel::setConnections(const QList<Stats::ConnectionMetadata> &connections) {
    beginResetModel();
    m_groups.clear();

    QHash<QString, int> groupMap;

    for (const auto &c : connections) {
        QString proc = c.process.trimmed();
        if (proc.isEmpty()) {
            proc = tr("System");
        }

        auto it = groupMap.find(proc);
        ConnectionsTree::ProcessGroupItem *group = nullptr;
        if (it == groupMap.end()) {
            auto newGroup = std::make_unique<ConnectionsTree::ProcessGroupItem>();
            newGroup->processName = proc;
            newGroup->row = static_cast<int>(m_groups.size());
            newGroup->commonOutbound = c.outbound;
            group = newGroup.get();
            groupMap.insert(proc, newGroup->row);
            m_groups.push_back(std::move(newGroup));
        } else {
            group = m_groups[it.value()].get();
            if (group->sameOutbound && group->commonOutbound != c.outbound) {
                group->sameOutbound = false;
            }
        }

        auto leaf = std::make_unique<ConnectionsTree::ConnectionLeafItem>();
        leaf->parent = group;
        leaf->rowInParent = static_cast<int>(group->children.size());
        leaf->meta = c;
        leaf->destText = DisplayDest(c.dest, c.domain);
        leaf->protocolText = c.protocol.isEmpty() ? c.network : c.network + " (" + c.protocol + ")";

        group->totalUpload += c.upload;
        group->totalDownload += c.download;
        group->totalUploadSpeed += c.uploadSpeed;
        group->totalDownloadSpeed += c.downloadSpeed;

        group->children.push_back(std::move(leaf));
    }

    endResetModel();
}

bool ConnectionsTreeModel::isProcessIndex(const QModelIndex &index) const {
    if (!index.isValid()) return false;
    auto *item = static_cast<ConnectionsTree::TreeItem *>(index.internalPointer());
    return item && item->isProcess();
}

QString ConnectionsTreeModel::processNameAt(const QModelIndex &index) const {
    if (!index.isValid()) return {};
    auto *item = static_cast<ConnectionsTree::TreeItem *>(index.internalPointer());
    if (!item) return {};
    if (item->isProcess()) return static_cast<ConnectionsTree::ProcessGroupItem *>(item)->processName;
    auto *leaf = static_cast<ConnectionsTree::ConnectionLeafItem *>(item);
    return leaf->parent ? leaf->parent->processName : leaf->meta.process;
}

const Stats::ConnectionMetadata *ConnectionsTreeModel::metaAt(const QModelIndex &index) const {
    if (!index.isValid()) return nullptr;
    auto *item = static_cast<ConnectionsTree::TreeItem *>(index.internalPointer());
    if (!item || item->isProcess()) return nullptr;
    return &static_cast<ConnectionsTree::ConnectionLeafItem *>(item)->meta;
}

QStringList ConnectionsTreeModel::connectionIdsAt(const QModelIndex &index) const {
    if (!index.isValid()) return {};
    auto *item = static_cast<ConnectionsTree::TreeItem *>(index.internalPointer());
    if (!item) return {};
    if (item->isProcess()) return static_cast<ConnectionsTree::ProcessGroupItem *>(item)->connectionIds();
    auto *leaf = static_cast<ConnectionsTree::ConnectionLeafItem *>(item);
    return {leaf->meta.id};
}

const ConnectionsTree::ProcessGroupItem *ConnectionsTreeModel::groupAt(int row) const {
    if (row < 0 || row >= m_groups.size()) return nullptr;
    return m_groups[row].get();
}

int ConnectionsTreeModel::groupCount() const {
    return static_cast<int>(m_groups.size());
}
