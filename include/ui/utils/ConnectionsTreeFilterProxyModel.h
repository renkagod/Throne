#pragma once

#include <QSortFilterProxyModel>
#include <QString>

class ConnectionsTreeModel;

namespace ConnectionsTree {
struct ProcessGroupItem;
struct ConnectionLeafItem;
}

class ConnectionsTreeFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ConnectionsTreeFilterProxyModel(QObject *parent = nullptr);

    void setFilters(const QString &source, const QString &dest, const QString &process,
                    const QString &protocol, const QString &outbound, const QString &target = QString());

    bool hasActiveFilter() const;

    ConnectionsTreeModel *treeModel() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool leafMatches(const ConnectionsTree::ProcessGroupItem *group,
                     const ConnectionsTree::ConnectionLeafItem *child) const;

    QString m_source;
    QString m_dest;
    QString m_process;
    QString m_protocol;
    QString m_outbound;
    QString m_target;
};
