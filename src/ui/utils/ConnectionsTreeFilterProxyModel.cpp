#include "include/ui/utils/ConnectionsTreeFilterProxyModel.h"
#include "include/ui/utils/ConnectionsTreeModel.h"

ConnectionsTreeFilterProxyModel::ConnectionsTreeFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

ConnectionsTreeModel *ConnectionsTreeFilterProxyModel::treeModel() const {
    return qobject_cast<ConnectionsTreeModel *>(sourceModel());
}

bool ConnectionsTreeFilterProxyModel::hasActiveFilter() const {
    return !m_source.isEmpty() || !m_dest.isEmpty() || !m_process.isEmpty()
           || !m_protocol.isEmpty() || !m_outbound.isEmpty() || !m_target.isEmpty();
}

void ConnectionsTreeFilterProxyModel::setFilters(const QString &source, const QString &dest,
                                                 const QString &process, const QString &protocol,
                                                 const QString &outbound, const QString &target) {
    if (m_source == source && m_dest == dest && m_process == process
        && m_protocol == protocol && m_outbound == outbound && m_target == target) return;
    m_source = source;
    m_dest = dest;
    m_process = process;
    m_protocol = protocol;
    m_outbound = outbound;
    m_target = target;
    invalidateRowsFilter();
}

bool ConnectionsTreeFilterProxyModel::leafMatches(const ConnectionsTree::ProcessGroupItem *group,
                                                  const ConnectionsTree::ConnectionLeafItem *child) const {
    if (child == nullptr) return false;
    const auto &meta = child->meta;

    if (!m_source.isEmpty() && !meta.sourceDisplay.contains(m_source, Qt::CaseInsensitive))
        return false;

    if (!m_protocol.isEmpty() && !child->protocolText.contains(m_protocol, Qt::CaseInsensitive))
        return false;

    if (!m_outbound.isEmpty() && !meta.outbound.contains(m_outbound, Qt::CaseInsensitive))
        return false;

    if (!m_process.isEmpty()) {
        const QString &procName = group ? group->processName : meta.process;
        const bool matches = procName.contains(m_process, Qt::CaseInsensitive)
                          || meta.process.contains(m_process, Qt::CaseInsensitive);
        if (!matches) return false;
    }

    if (!m_dest.isEmpty()) {
        const bool matches = child->destText.contains(m_dest, Qt::CaseInsensitive)
                          || meta.dest.contains(m_dest, Qt::CaseInsensitive)
                          || meta.domain.contains(m_dest, Qt::CaseInsensitive);
        if (!matches) return false;
    }

    if (!m_target.isEmpty()) {
        const QString &procName = group ? group->processName : meta.process;
        const bool procMatches = procName.contains(m_target, Qt::CaseInsensitive)
                              || meta.process.contains(m_target, Qt::CaseInsensitive);
        const bool destMatches = child->destText.contains(m_target, Qt::CaseInsensitive)
                              || meta.dest.contains(m_target, Qt::CaseInsensitive)
                              || meta.domain.contains(m_target, Qt::CaseInsensitive);
        if (!procMatches && !destMatches) return false;
    }

    return true;
}

bool ConnectionsTreeFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (!hasActiveFilter()) return true;

    auto *model = treeModel();
    if (model == nullptr) return true;

    if (!sourceParent.isValid()) {
        const auto *group = model->groupAt(sourceRow);
        if (group == nullptr) return false;

        // Group is accepted if ANY child matches all active filters
        for (const auto &child : group->children) {
            if (leafMatches(group, child.get())) return true;
        }
        return false;
    } else {
        const auto *group = model->groupAt(sourceParent.row());
        if (group == nullptr || sourceRow < 0 || sourceRow >= static_cast<int>(group->children.size()))
            return false;

        return leafMatches(group, group->children[sourceRow].get());
    }
}
