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
           || !m_protocol.isEmpty() || !m_outbound.isEmpty();
}

void ConnectionsTreeFilterProxyModel::setFilters(const QString &source, const QString &dest, const QString &process,
                                                 const QString &protocol, const QString &outbound) {
    if (m_source == source && m_dest == dest && m_process == process
        && m_protocol == protocol && m_outbound == outbound) return;
    m_source = source;
    m_dest = dest;
    m_process = process;
    m_protocol = protocol;
    m_outbound = outbound;
    invalidateRowsFilter();
}

bool ConnectionsTreeFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (!hasActiveFilter()) return true;

    auto *model = treeModel();
    if (model == nullptr) return true;

    if (!sourceParent.isValid()) {
        const auto *group = model->groupAt(sourceRow);
        if (group == nullptr) return false;

        const bool procFilterActive = !m_process.isEmpty();
        const bool procMatches = procFilterActive && group->processName.contains(m_process, Qt::CaseInsensitive);

        const bool hasOtherFilters = !m_source.isEmpty() || !m_dest.isEmpty() || !m_protocol.isEmpty() || !m_outbound.isEmpty();

        // If other filters are active or process doesn't match directly, check children
        if (hasOtherFilters || !procMatches) {
            bool anyChildMatches = false;
            for (const auto &child : group->children) {
                const auto &meta = child->meta;
                if (!m_process.isEmpty() && !procMatches
                    && !meta.process.contains(m_process, Qt::CaseInsensitive)) continue;
                if (!m_source.isEmpty() && !meta.sourceDisplay.contains(m_source, Qt::CaseInsensitive)) continue;
                if (!m_dest.isEmpty() && !child->destText.contains(m_dest, Qt::CaseInsensitive)) continue;
                if (!m_protocol.isEmpty() && !child->protocolText.contains(m_protocol, Qt::CaseInsensitive)) continue;
                if (!m_outbound.isEmpty() && !meta.outbound.contains(m_outbound, Qt::CaseInsensitive)) continue;
                anyChildMatches = true;
                break;
            }
            if (!anyChildMatches) return false;
        }

        return true;
    } else {
        const auto *group = model->groupAt(sourceParent.row());
        if (group == nullptr || sourceRow < 0 || sourceRow >= group->children.size()) return false;

        const auto &child = group->children[sourceRow];
        const auto &meta = child->meta;

        if (!m_process.isEmpty() && !group->processName.contains(m_process, Qt::CaseInsensitive)
            && !meta.process.contains(m_process, Qt::CaseInsensitive)) return false;
        if (!m_source.isEmpty() && !meta.sourceDisplay.contains(m_source, Qt::CaseInsensitive)) return false;
        if (!m_dest.isEmpty() && !child->destText.contains(m_dest, Qt::CaseInsensitive)) return false;
        if (!m_protocol.isEmpty() && !child->protocolText.contains(m_protocol, Qt::CaseInsensitive)) return false;
        if (!m_outbound.isEmpty() && !meta.outbound.contains(m_outbound, Qt::CaseInsensitive)) return false;

        return true;
    }
}
