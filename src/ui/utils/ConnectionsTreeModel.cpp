#include "include/ui/utils/ConnectionsTreeModel.h"
#include "include/global/Utils.hpp"

#include <algorithm>
#include <QApplication>
#include <QHash>

ConnectionsTreeModel::ConnectionsTreeModel(QObject *parent)
    : QAbstractItemModel(parent) {}

QModelIndex ConnectionsTreeModel::index(int row, int column, const QModelIndex &parent) const {
    if (column < 0 || column >= ColumnCount) return {};
    if (parent.isValid() && parent.column() != 0) return {};

    if (!parent.isValid()) {
        if (row < 0 || row >= static_cast<int>(m_groups.size())) return {};
        return createIndex(row, column, m_groups[row].get());
    }

    auto *item = static_cast<ConnectionsTree::TreeItem *>(parent.internalPointer());
    if (!item || !item->isProcess()) return {};

    auto *group = static_cast<ConnectionsTree::ProcessGroupItem *>(item);
    if (row < 0 || row >= static_cast<int>(group->children.size())) return {};

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
    if (parent.isValid() && parent.column() != 0) return 0;

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
        if (role == ConnIdsRole || role == ConnIdRole) return group->connectionIds();

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColTarget:
                return QStringLiteral("%1 (%2)").arg(group->processName).arg(group->totalConnections);
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
            if (index.column() == ColTarget) {
                return tr("Process: %1\nActive connections: %2\nTotal traffic: %3↑ %4↓\nTotal speed: %5/s↑ %6/s↓")
                    .arg(group->processName)
                    .arg(group->totalConnections)
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
    if (role == CleanDestRole) return leaf->destText;
    if (role == ConnIdRole) return leaf->connectionIds.isEmpty() ? QString() : leaf->connectionIds.first();
    if (role == ConnIdsRole) return leaf->connectionIds;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTarget:
            return (leaf->count > 1) ? QStringLiteral("%1 (%2)").arg(leaf->destText).arg(leaf->count) : leaf->destText;
        case ColSource:
            return (leaf->count > 1) ? QStringLiteral("-") : leaf->sourceDisplay;
        case ColProtocol:
            return leaf->protocolText;
        case ColOutbound:
            return leaf->outbound;
        case ColTraffic:
            return ReadableSize(leaf->upload) + "↑ " + ReadableSize(leaf->download) + "↓";
        case ColSpeed:
            return ReadableSize(leaf->uploadSpeed) + "/s↑ " + ReadableSize(leaf->downloadSpeed) + "/s↓";
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        if (index.column() == ColTarget) {
            if (leaf->count > 1) {
                return tr("Destination: %1\nConnections: %2\nProcess: %3\nProtocol: %4\nOutbound: %5\nTotal traffic: %6↑ %7↓\nTotal speed: %8/s↑ %9/s↓")
                    .arg(leaf->destText)
                    .arg(leaf->count)
                    .arg(c.process.isEmpty() ? tr("System") : c.process)
                    .arg(leaf->protocolText)
                    .arg(leaf->outbound)
                    .arg(ReadableSize(leaf->upload))
                    .arg(ReadableSize(leaf->download))
                    .arg(ReadableSize(leaf->uploadSpeed))
                    .arg(ReadableSize(leaf->downloadSpeed));
            }
            return tr("Destination: %1\nProcess: %2\nProtocol: %3\nOutbound: %4")
                .arg(leaf->destText, c.process.isEmpty() ? tr("System") : c.process, leaf->protocolText, leaf->outbound);
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
    std::vector<QHash<QString, int>> groupLeafMaps;

    for (const auto &c : connections) {
        QString proc = c.process.trimmed();
        if (proc.isEmpty()) {
            proc = tr("System");
        }

        auto it = groupMap.find(proc);
        ConnectionsTree::ProcessGroupItem *group = nullptr;
        int groupRow = -1;
        if (it == groupMap.end()) {
            auto newGroup = std::make_unique<ConnectionsTree::ProcessGroupItem>();
            newGroup->processName = proc;
            newGroup->row = static_cast<int>(m_groups.size());
            newGroup->commonOutbound = c.outbound;
            group = newGroup.get();
            groupRow = newGroup->row;
            groupMap.insert(proc, groupRow);
            m_groups.push_back(std::move(newGroup));
            groupLeafMaps.emplace_back();
        } else {
            groupRow = it.value();
            group = m_groups[groupRow].get();
            if (group->sameOutbound && group->commonOutbound != c.outbound) {
                group->sameOutbound = false;
            }
        }

        group->totalConnections++;
        group->totalUpload += c.upload;
        group->totalDownload += c.download;
        group->totalUploadSpeed += c.uploadSpeed;
        group->totalDownloadSpeed += c.downloadSpeed;

        const QString dest = DisplayDest(c.dest, c.domain);
        const QString proto = c.protocol.isEmpty() ? c.network : c.network + " (" + c.protocol + ")";
        const QString leafKey = dest + "\t" + proto + "\t" + c.outbound;

        auto &leafMap = groupLeafMaps[groupRow];
        auto leafIt = leafMap.find(leafKey);
        if (leafIt == leafMap.end()) {
            auto leaf = std::make_unique<ConnectionsTree::ConnectionLeafItem>();
            leaf->parent = group;
            leaf->rowInParent = static_cast<int>(group->children.size());
            leaf->meta = c;
            leaf->destText = dest;
            leaf->protocolText = proto;
            leaf->outbound = c.outbound;
            leaf->sourceDisplay = c.sourceDisplay;
            leaf->count = 1;
            if (!c.id.isEmpty()) leaf->connectionIds.append(c.id);
            leaf->upload = c.upload;
            leaf->download = c.download;
            leaf->uploadSpeed = c.uploadSpeed;
            leaf->downloadSpeed = c.downloadSpeed;

            leafMap.insert(leafKey, static_cast<int>(group->children.size()));
            group->children.push_back(std::move(leaf));
        } else {
            auto *existingLeaf = group->children[leafIt.value()].get();
            existingLeaf->count++;
            if (!c.id.isEmpty()) existingLeaf->connectionIds.append(c.id);
            existingLeaf->upload += c.upload;
            existingLeaf->download += c.download;
            existingLeaf->uploadSpeed += c.uploadSpeed;
            existingLeaf->downloadSpeed += c.downloadSpeed;
            if (existingLeaf->sourceDisplay != c.sourceDisplay) {
                existingLeaf->sourceDisplay = QStringLiteral("-");
            }
        }
    }

    if (Stats::connection_lister != nullptr) {
        const auto sortMode = Stats::connection_lister->getSort();
        const bool asc = Stats::connection_lister->isSortAscending();

        auto compareGroups = [sortMode, asc](const std::unique_ptr<ConnectionsTree::ProcessGroupItem> &a,
                                             const std::unique_ptr<ConnectionsTree::ProcessGroupItem> &b) -> bool {
            switch (sortMode) {
            case Stats::ByTraffic: {
                const auto ta = a->totalUpload + a->totalDownload;
                const auto tb = b->totalUpload + b->totalDownload;
                if (ta == tb) return a->processName < b->processName;
                return asc ? (ta < tb) : (ta > tb);
            }
            case Stats::ByDownload: {
                if (a->totalDownload == b->totalDownload) return a->processName < b->processName;
                return asc ? (a->totalDownload < b->totalDownload) : (a->totalDownload > b->totalDownload);
            }
            case Stats::ByUpload: {
                if (a->totalUpload == b->totalUpload) return a->processName < b->processName;
                return asc ? (a->totalUpload < b->totalUpload) : (a->totalUpload > b->totalUpload);
            }
            case Stats::BySpeed: {
                const auto sa = a->totalUploadSpeed + a->totalDownloadSpeed;
                const auto sb = b->totalUploadSpeed + b->totalDownloadSpeed;
                if (sa == sb) return a->processName < b->processName;
                return asc ? (sa < sb) : (sa > sb);
            }
            case Stats::ByDownloadSpeed: {
                if (a->totalDownloadSpeed == b->totalDownloadSpeed) return a->processName < b->processName;
                return asc ? (a->totalDownloadSpeed < b->totalDownloadSpeed) : (a->totalDownloadSpeed > b->totalDownloadSpeed);
            }
            case Stats::ByUploadSpeed: {
                if (a->totalUploadSpeed == b->totalUploadSpeed) return a->processName < b->processName;
                return asc ? (a->totalUploadSpeed < b->totalUploadSpeed) : (a->totalUploadSpeed > b->totalUploadSpeed);
            }
            case Stats::ByProcess: {
                if (a->processName == b->processName) return false;
                return asc ? (a->processName > b->processName) : (a->processName < b->processName);
            }
            default:
                return false;
            }
        };

        if (sortMode != Stats::Default) {
            std::stable_sort(m_groups.begin(), m_groups.end(), compareGroups);
        }

        for (auto &group : m_groups) {
            std::stable_sort(group->children.begin(), group->children.end(),
                [sortMode, asc](const std::unique_ptr<ConnectionsTree::ConnectionLeafItem> &a,
                                const std::unique_ptr<ConnectionsTree::ConnectionLeafItem> &b) -> bool {
                    switch (sortMode) {
                    case Stats::ByTraffic: {
                        const auto ta = a->upload + a->download;
                        const auto tb = b->upload + b->download;
                        if (ta == tb) return a->destText < b->destText;
                        return asc ? (ta < tb) : (ta > tb);
                    }
                    case Stats::ByDownload: {
                        if (a->download == b->download) return a->destText < b->destText;
                        return asc ? (a->download < b->download) : (a->download > b->download);
                    }
                    case Stats::ByUpload: {
                        if (a->upload == b->upload) return a->destText < b->destText;
                        return asc ? (a->upload < b->upload) : (a->upload > b->upload);
                    }
                    case Stats::BySpeed: {
                        const auto sa = a->uploadSpeed + a->downloadSpeed;
                        const auto sb = b->uploadSpeed + b->downloadSpeed;
                        if (sa == sb) return a->destText < b->destText;
                        return asc ? (sa < sb) : (sa > sb);
                    }
                    case Stats::ByDownloadSpeed: {
                        if (a->downloadSpeed == b->downloadSpeed) return a->destText < b->destText;
                        return asc ? (a->downloadSpeed < b->downloadSpeed) : (a->downloadSpeed > b->downloadSpeed);
                    }
                    case Stats::ByUploadSpeed: {
                        if (a->uploadSpeed == b->uploadSpeed) return a->destText < b->destText;
                        return asc ? (a->uploadSpeed < b->uploadSpeed) : (a->uploadSpeed > b->uploadSpeed);
                    }
                    case Stats::ByProcess:
                    default: {
                        if (a->destText == b->destText) return false;
                        return asc ? (a->destText < b->destText) : (a->destText > b->destText);
                    }
                    }
                });
            for (size_t c = 0; c < group->children.size(); ++c) {
                group->children[c]->rowInParent = static_cast<int>(c);
            }
        }
    }

    for (size_t i = 0; i < m_groups.size(); ++i) {
        m_groups[i]->row = static_cast<int>(i);
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
    return leaf->connectionIds;
}

const ConnectionsTree::ProcessGroupItem *ConnectionsTreeModel::groupAt(int row) const {
    if (row < 0 || row >= m_groups.size()) return nullptr;
    return m_groups[row].get();
}

int ConnectionsTreeModel::groupCount() const {
    return static_cast<int>(m_groups.size());
}
