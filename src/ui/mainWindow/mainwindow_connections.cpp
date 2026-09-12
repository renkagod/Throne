#include "include/ui/mainwindow.h"
#include "include/api/RPC.h"
#include "include/database/entities/RouteProfile.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/LocalNetwork.hpp"
#include "include/ui/utils/ConnectionsTreeFilterHeader.h"
#include "include/ui/utils/ConnectionsTreeFilterProxyModel.h"
#include "include/ui/utils/ConnectionsTreeModel.h"

#include <QHostAddress>

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>

namespace
{
    QIcon RecolorIcon(const QString& path, const QColor& color)
    {
        QPixmap pixmap(path);
        if (pixmap.isNull()) return QIcon(path);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();
        return QIcon(pixmap);
    }
}

void MainWindow::setupConnectionList()
{
    connectionsTree = ui->connectionsTree;
    connectionsTreeModel = new ConnectionsTreeModel(this);
    connectionsTreeFilterModel = new ConnectionsTreeFilterProxyModel(this);
    connectionsTreeFilterModel->setSourceModel(connectionsTreeModel);
    connectionsTree->setModel(connectionsTreeFilterModel);

    connectionsTreeFilterHeader = new ConnectionsTreeFilterHeader(connectionsTree);
    connectionsTree->setHeader(connectionsTreeFilterHeader);

    auto* header = connectionsTree->header();
    header->setHighlightSections(false);
    header->setSectionResizeMode(ConnectionsTreeModel::ColTarget, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTreeModel::ColSource, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColProtocol, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColOutbound, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColTraffic, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColSpeed, QHeaderView::ResizeToContents);

    header->setResizeContentsPrecision(20);
    connectionsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connectionsTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    connectionsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    connectionsTree->setAlternatingRowColors(true);
    connectionsTree->setWordWrap(false);
    connectionsTree->setAnimated(false);

    connectionsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(connectionsTree, &QWidget::customContextMenuRequested, this, &MainWindow::onTreeConnectionContextMenu);

    connect(connectionsTree, &QAbstractItemView::clicked, this, [this](const QModelIndex& index)
    {
        if (!index.isValid()) return;

        QString text;
        if (index.data(ConnectionsTreeModel::IsProcessRole).toBool() && index.column() == ConnectionsTreeModel::ColTarget) {
            text = index.data(ConnectionsTreeModel::ProcessNameRole).toString();
        } else if (!index.data(ConnectionsTreeModel::IsProcessRole).toBool() && index.column() == ConnectionsTreeModel::ColTarget) {
            text = index.data(ConnectionsTreeModel::CleanDestRole).toString();
            if (text.isEmpty()) text = index.data(Qt::DisplayRole).toString();
        } else {
            text = index.data(Qt::DisplayRole).toString();
        }
        if (text.isEmpty() || text == "-") return;

        QApplication::clipboard()->setText(text);
        const QPoint pos = connectionsTree->viewport()->mapToGlobal(connectionsTree->visualRect(index).center());
        QToolTip::showText(pos, tr("Copied!"), this);
        auto r = ++toolTipID;
        QTimer::singleShot(1500, this, [=, this] {
            if (r != toolTipID) return;
            QToolTip::hideText();
        });
    });

    connect(connectionsTree, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
        const QString proc = index.data(ConnectionsTreeModel::ProcessNameRole).toString();
        if (!proc.isEmpty()) m_collapsedProcesses.insert(proc);
    });
    connect(connectionsTree, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        const QString proc = index.data(ConnectionsTreeModel::ProcessNameRole).toString();
        if (!proc.isEmpty()) m_collapsedProcesses.remove(proc);
    });

    connect(header, &QHeaderView::sectionClicked, this, [this](int index)
    {
        Stats::ConnectionSort sortType;
        switch (index)
        {
        case ConnectionsTreeModel::ColSource:   sortType = Stats::BySource; break;
        case ConnectionsTreeModel::ColProtocol: sortType = Stats::ByProtocol; break;
        case ConnectionsTreeModel::ColOutbound: sortType = Stats::ByOutbound; break;
        case ConnectionsTreeModel::ColTraffic:  sortType = Stats::ByTraffic; break;
        case ConnectionsTreeModel::ColSpeed:    sortType = Stats::BySpeed; break;
        case ConnectionsTreeModel::ColTarget:   sortType = Stats::ByProcess; break;
        default: sortType = Stats::Default; break;
        }

        applyConnectionSort(sortType);
    });

    refreshConnectionCloseIcons();
    restoreConnectionSort();
    setupConnectionSortMenu();
    setupConnectionFilter();
    syncConnectionSourceColumn();
}

void MainWindow::restoreConnectionSort()
{
    const auto* settings = Configs::dataManager->settingsRepo.get();
    int stored = settings->connection_sort;
    if (stored < Stats::Default || stored > Stats::BySource) return;
    // The Source header is unreachable while its column is hidden, so that sort would be stuck for good.
    if (stored == Stats::BySource && !LocalNetwork::LanInboundEnabled()) stored = Stats::Default;
    // Runs before setup_rpc() spawns the lister thread, so writing the pair unguarded is safe.
    Stats::connection_lister->restoreSort(static_cast<Stats::ConnectionSort>(stored), settings->connection_sort_asc);
}

void MainWindow::applyConnectionSort(Stats::ConnectionSort sort)
{
    Stats::connection_lister->setSort(sort);
    auto* settings = Configs::dataManager->settingsRepo.get();
    settings->connection_sort = Stats::connection_lister->getSort();
    settings->connection_sort_asc = Stats::connection_lister->isSortAscending();
    settings->Save();
    Stats::connection_lister->ForceUpdate();
}

void MainWindow::setupConnectionFilter()
{
    connectionFilterButton = new QToolButton(this);
    connectionFilterButton->setIcon(QIcon(":/icon/filter.png"));
    connectionFilterButton->setToolTip(tr("Enable Filter"));
    connectionFilterButton->setCheckable(true);
    connect(connectionFilterButton, &QToolButton::toggled, this, [this](bool visible) {
        if (connectionsTreeFilterHeader) connectionsTreeFilterHeader->setFiltersVisible(visible);
    });
    if (connectionsTreeFilterHeader) {
        connect(connectionsTreeFilterHeader, &ConnectionsTreeFilterHeader::closeRequested, connectionFilterButton,
                [this] { connectionFilterButton->setChecked(false); });
    }

    connectionCloseAllButton = new QToolButton(this);
    connectionCloseAllButton->setIcon(connectionCloseIcon);
    connectionCloseAllButton->setToolTip(tr("Close every connection listed below"));
    connect(connectionCloseAllButton, &QToolButton::clicked, this, [this] { closeConnections(listedConnectionIds()); });

    auto* corner = new QWidget(this);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(2);
    cornerLayout->addWidget(connectionFilterButton);
    cornerLayout->addWidget(connectionCloseAllButton);
    ui->stats_widget->setCornerWidget(corner, Qt::TopRightCorner);

    auto syncCorner = [=,this] { corner->setVisible(ui->stats_widget->currentWidget() == ui->connections_tab); };
    connect(ui->stats_widget, &QTabWidget::currentChanged, this, [syncCorner](int) { syncCorner(); });
    syncCorner();

    connectionFilterDebounce = new QTimer(this);
    connectionFilterDebounce->setSingleShot(true);
    connectionFilterDebounce->setInterval(50);
    connect(connectionFilterDebounce, &QTimer::timeout, this, [this] { applyConnectionFilters(); });
    if (connectionsTreeFilterHeader) {
        connect(connectionsTreeFilterHeader, &ConnectionsTreeFilterHeader::filtersChanged, this, [this] { connectionFilterDebounce->start(); });
    }
}

void MainWindow::applyConnectionFilters()
{
    if (connectionsTreeFilterHeader != nullptr && connectionsTreeFilterModel != nullptr) {
        const auto filters = connectionsTreeFilterHeader->filters();
        connectionsTreeFilterModel->setFilters(filters.source, filters.target,
                                               filters.protocol, filters.outbound);
    }
}

void MainWindow::syncConnectionSourceColumn()
{
    const bool show = LocalNetwork::LanInboundEnabled();

    if (connectionsTree != nullptr && connectionsTreeModel != nullptr) {
        if (connectionsTree->isColumnHidden(ConnectionsTreeModel::ColSource) != !show) {
            connectionsTree->setColumnHidden(ConnectionsTreeModel::ColSource, !show);
            if (connectionsTreeFilterHeader != nullptr) {
                connectionsTreeFilterHeader->adjustPositions();
                if (!show) {
                    connectionsTreeFilterHeader->clearFilterFor(ConnectionsTreeModel::ColSource);
                    if (Stats::connection_lister->getSort() == Stats::BySource) applyConnectionSort(Stats::Default);
                }
            }
        }
    }

    applyConnectionFilters();
}

void MainWindow::setupConnectionSortMenu()
{
    auto installSortMenu = [this](QHeaderView* header, int trafficCol, int speedCol) {
        if (header == nullptr) return;
        header->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(header, &QWidget::customContextMenuRequested, this, [=,this](const QPoint& pos)
        {
            const int columnIndex = header->logicalIndexAt(pos);
            const bool isTraffic = columnIndex == trafficCol;
            const bool isSpeed = columnIndex == speedCol;
            if (!isTraffic && !isSpeed) return;

            struct SortOption { Stats::ConnectionSort value; QString label; };
            const QList<SortOption> options = isTraffic
                ? QList<SortOption>{
                    { Stats::ByTraffic, tr("Total") },
                    { Stats::ByDownload, tr("Downloaded") },
                    { Stats::ByUpload, tr("Uploaded") } }
                : QList<SortOption>{
                    { Stats::BySpeed, tr("Total") },
                    { Stats::ByDownloadSpeed, tr("Download Speed") },
                    { Stats::ByUploadSpeed, tr("Upload Speed") } };

            QMenu menu(this);
            auto* sortByLabel = menu.addAction(tr("Sort By:"));
            sortByLabel->setEnabled(false);

            const auto current = Stats::connection_lister->getSort();
            for (const auto& opt : options)
            {
                auto* act = menu.addAction(opt.label);
                act->setData(static_cast<int>(opt.value));
                act->setCheckable(true);
                act->setChecked(current == opt.value);
            }

            auto* chosen = menu.exec(header->mapToGlobal(pos));
            if (chosen == nullptr || !chosen->data().isValid()) return;

            applyConnectionSort(static_cast<Stats::ConnectionSort>(chosen->data().toInt()));
        });
    };

    if (connectionsTree != nullptr) {
        installSortMenu(connectionsTree->header(),
                        ConnectionsTreeModel::ColTraffic, ConnectionsTreeModel::ColSpeed);
    }
}

void MainWindow::refreshConnectionCloseIcons()
{
    connectionCloseIcon = RecolorIcon(":/icon/material/cancel.png", palette().color(QPalette::ButtonText));
    if (connectionCloseAllButton != nullptr) connectionCloseAllButton->setIcon(connectionCloseIcon);
    if (connectionsTree != nullptr) connectionsTree->viewport()->update();
}

QStringList MainWindow::listedConnectionIds() const
{
    QStringList ids;
    if (connectionsTreeFilterModel == nullptr) return ids;
    const int rows = connectionsTreeFilterModel->rowCount();
    for (int r = 0; r < rows; ++r) {
        const QModelIndex groupIdx = connectionsTreeFilterModel->index(r, 0);
        const int childCount = connectionsTreeFilterModel->rowCount(groupIdx);
        for (int c = 0; c < childCount; ++c) {
            const auto id = connectionsTreeFilterModel->index(c, 0, groupIdx)
                                .data(ConnectionsTreeModel::ConnIdRole).toString();
            if (!id.isEmpty()) ids << id;
        }
    }
    return ids;
}

void MainWindow::closeConnections(const QStringList& ids)
{
    if (ids.isEmpty()) return;
    runOnNewThread([ids] {
        bool rpcOK = false;
        const auto err = API::defaultClient->CloseConnections(&rpcOK, ids);
        if (!rpcOK || !err.isEmpty())
        {
            MW_show_log(tr("Failed to close connections: %1").arg(err.isEmpty() ? tr("IPC error") : err));
            return;
        }
        Stats::connection_lister->ForceUpdate();
    });
}

void MainWindow::UpdateConnectionList(const QList<Stats::ConnectionMetadata>& connections)
{
    if (connectionsTreeModel != nullptr && connectionsTree != nullptr) {
        const int scrollPos = connectionsTree->verticalScrollBar()->value();

        QString selectedConnId;
        QString selectedProcess;
        bool selectedWasProcess = false;
        const QModelIndex currentProxyIdx = connectionsTree->currentIndex();
        if (currentProxyIdx.isValid()) {
            selectedWasProcess = currentProxyIdx.data(ConnectionsTreeModel::IsProcessRole).toBool();
            if (selectedWasProcess) {
                selectedProcess = currentProxyIdx.data(ConnectionsTreeModel::ProcessNameRole).toString();
            } else {
                selectedConnId = currentProxyIdx.data(ConnectionsTreeModel::ConnIdRole).toString();
            }
        }

        connectionsTreeModel->setConnections(connections);

        const bool oldBlocked = connectionsTree->blockSignals(true);
        const int groupCount = connectionsTreeFilterModel ? connectionsTreeFilterModel->rowCount()
                                                          : connectionsTreeModel->rowCount();
        for (int i = 0; i < groupCount; ++i) {
            const QModelIndex idx = connectionsTreeFilterModel ? connectionsTreeFilterModel->index(i, 0)
                                                               : connectionsTreeModel->index(i, 0);
            const QString proc = idx.data(ConnectionsTreeModel::ProcessNameRole).toString();
            const bool shouldExpand = !m_collapsedProcesses.contains(proc);
            connectionsTree->setExpanded(idx, shouldExpand);
        }
        connectionsTree->blockSignals(oldBlocked);

        if (selectedWasProcess && !selectedProcess.isEmpty()) {
            for (int i = 0; i < groupCount; ++i) {
                const QModelIndex idx = connectionsTreeFilterModel ? connectionsTreeFilterModel->index(i, 0)
                                                                   : connectionsTreeModel->index(i, 0);
                if (idx.data(ConnectionsTreeModel::ProcessNameRole).toString() == selectedProcess) {
                    connectionsTree->setCurrentIndex(idx);
                    break;
                }
            }
        } else if (!selectedConnId.isEmpty()) {
            bool found = false;
            for (int i = 0; i < groupCount && !found; ++i) {
                const QModelIndex groupIdx = connectionsTreeFilterModel ? connectionsTreeFilterModel->index(i, 0)
                                                                        : connectionsTreeModel->index(i, 0);
                const int childCount = connectionsTreeFilterModel ? connectionsTreeFilterModel->rowCount(groupIdx)
                                                                  : connectionsTreeModel->rowCount(groupIdx);
                for (int c = 0; c < childCount; ++c) {
                    const QModelIndex childIdx = connectionsTreeFilterModel ? connectionsTreeFilterModel->index(c, 0, groupIdx)
                                                                            : connectionsTreeModel->index(c, 0, groupIdx);
                    if (childIdx.data(ConnectionsTreeModel::ConnIdRole).toString() == selectedConnId) {
                        connectionsTree->setCurrentIndex(childIdx);
                        found = true;
                        break;
                    }
                }
            }
        }

        connectionsTree->verticalScrollBar()->setValue(scrollPos);
    }
}

QString MainWindow::routeRuleAppendBlocker() const
{
    const auto& dm = Configs::dataManager;
    const auto currentRoute = dm->routesRepo->GetRouteProfile(dm->settingsRepo->current_route_id);
    if (!currentRoute) return tr("No active routing profile found.");
    if (currentRoute->preventModifications) return tr("The current routing profile is locked against modifications.");
    if (currentRoute->isRaw) return tr("The current routing profile is raw JSON.");
    if (currentRoute->isRemote && currentRoute->autoUpdate) return tr("The current routing profile auto-updates from a URL.");
    return {};
}

bool MainWindow::addRuleToCurrentRoute(const QString& rawRule, Configs::simpleAction action)
{
    auto fail = [this](const QString& msg) {
        MW_show_log(msg);
        return false;
    };

    if (const auto blocker = routeRuleAppendBlocker(); !blocker.isEmpty()) return fail(blocker);

    const auto& dm = Configs::dataManager;
    const auto currentRoute = dm->routesRepo->GetRouteProfile(dm->settingsRepo->current_route_id);
    if (!currentRoute) return fail(tr("No active routing profile found."));

    if (!currentRoute->AppendSimpleRule(rawRule, action))
        return fail(tr("Failed to add routing rule: %1").arg(rawRule));

    if (!dm->routesRepo->Save(currentRoute))
        return fail(tr("Failed to save routing rule: %1").arg(rawRule));

    MW_show_log(tr("Appended %1 to the %2 rules of \"%3\"")
                    .arg(rawRule, Configs::simpleActionToString(action), currentRoute->name));
    noteRestartNeeded(tr("Routing"));
    return true;
}

void MainWindow::onTreeConnectionContextMenu(const QPoint& pos)
{
    if (connectionsTree == nullptr || connectionsTreeModel == nullptr) return;

    const QModelIndex proxyIndex = connectionsTree->indexAt(pos);
    if (!proxyIndex.isValid()) {
        QMenu menu(this);
        auto* expAll = menu.addAction(tr("Expand All"));
        connect(expAll, &QAction::triggered, this, [this] {
            m_collapsedProcesses.clear();
            connectionsTree->expandAll();
        });
        auto* collAll = menu.addAction(tr("Collapse All"));
        connect(collAll, &QAction::triggered, this, [this] {
            const int count = connectionsTreeFilterModel ? connectionsTreeFilterModel->rowCount() : 0;
            for (int i = 0; i < count; ++i) {
                const QModelIndex idx = connectionsTreeFilterModel->index(i, 0);
                const QString proc = idx.data(ConnectionsTreeModel::ProcessNameRole).toString();
                if (!proc.isEmpty()) m_collapsedProcesses.insert(proc);
            }
            connectionsTree->collapseAll();
        });
        menu.exec(connectionsTree->viewport()->mapToGlobal(pos));
        return;
    }

    const QModelIndex sourceIndex = connectionsTreeFilterModel
                                        ? connectionsTreeFilterModel->mapToSource(proxyIndex)
                                        : proxyIndex;
    if (!sourceIndex.isValid()) return;

    connectionsTree->setCurrentIndex(proxyIndex);

    const QPoint globalPos = connectionsTree->viewport()->mapToGlobal(pos);

    auto showTip = [this](const QString& text) {
        QToolTip::showText(QCursor::pos(), text, this);
        auto r = ++toolTipID;
        QTimer::singleShot(2000, this, [=, this] {
            if (r == toolTipID) QToolTip::hideText();
        });
    };

    struct RouteAction { Configs::simpleAction action; QString label; };
    const RouteAction routeActions[] = {
        { Configs::bypass, tr("Direct") },
        { Configs::proxy,  tr("Proxy") },
        { Configs::block,  tr("Block") },
    };

    const QString blocker = routeRuleAppendBlocker();

    QMenu menu(this);

    auto addRouteSubmenu = [&](const QString& title, const QString& rule) {
        auto* sub = menu.addMenu(title);
        if (!blocker.isEmpty())
        {
            sub->setEnabled(false);
            sub->menuAction()->setToolTip(blocker);
            return;
        }
        for (const auto& ra : routeActions)
        {
            auto* act = sub->addAction(ra.label);
            connect(act, &QAction::triggered, this, [this, rule, ra, showTip] {
                if (addRuleToCurrentRoute(rule, ra.action))
                    showTip(tr("Appended to the %1 rules:\n%2").arg(ra.label, rule));
            });
        }
    };

    menu.setToolTipsVisible(true);

    if (connectionsTreeModel->isProcessIndex(sourceIndex)) {
        const QString process = connectionsTreeModel->processNameAt(sourceIndex).trimmed();
        if (!process.isEmpty() && process != tr("System")) {
            addRouteSubmenu(tr("Append process \"%1\" to").arg(process), "processName:" + process);
        }

        if (!process.isEmpty()) {
            auto* copyProc = menu.addAction(tr("Copy Process Name"));
            connect(copyProc, &QAction::triggered, this, [this, process, showTip] {
                QApplication::clipboard()->setText(process);
                showTip(tr("Copied: %1").arg(process));
            });
        }

        const auto ids = connectionsTreeModel->connectionIdsAt(sourceIndex);
        if (!ids.isEmpty()) {
            menu.addSeparator();
            auto* closeAct = menu.addAction(tr("Close all connections for \"%1\" (%2)").arg(process).arg(ids.size()));
            connect(closeAct, &QAction::triggered, this, [this, ids] { closeConnections(ids); });
        }
    } else {
        const auto* meta = connectionsTreeModel->metaAt(sourceIndex);
        if (meta != nullptr) {
            const QString domain = meta->domain.trimmed();
            const QString process = meta->process.trimmed();
            const QString host = domain.isEmpty() ? Stats::EndpointHost(meta->dest.trimmed()) : domain;

            const bool isDomain = QHostAddress(host).isNull();
            const QString addressRule = isDomain ? ("suffix:" + host) : ("ip:" + host);
            const QString processRule = "processName:" + process;

            if (!host.isEmpty()) addRouteSubmenu(tr("Append \"%1\" to").arg(host), addressRule);
            if (!process.isEmpty()) addRouteSubmenu(tr("Append process \"%1\" to").arg(process), processRule);

            menu.addSeparator();
            if (!host.isEmpty()) {
                auto* copyHost = menu.addAction(tr("Copy Destination (%1)").arg(host));
                connect(copyHost, &QAction::triggered, this, [this, host, showTip] {
                    QApplication::clipboard()->setText(host);
                    showTip(tr("Copied: %1").arg(host));
                });
            }
            if (!process.isEmpty()) {
                auto* copyProc = menu.addAction(tr("Copy Process Name (%1)").arg(process));
                connect(copyProc, &QAction::triggered, this, [this, process, showTip] {
                    QApplication::clipboard()->setText(process);
                    showTip(tr("Copied: %1").arg(process));
                });
            }

            menu.addSeparator();
            const auto ids = connectionsTreeModel->connectionIdsAt(sourceIndex);
            if (!ids.isEmpty()) {
                auto* closeAct = ids.size() > 1
                    ? menu.addAction(tr("Close all connections (%1)").arg(ids.size()))
                    : menu.addAction(tr("Close connection"));
                connect(closeAct, &QAction::triggered, this, [this, ids] { closeConnections(ids); });
            }
        }
    }

    menu.addSeparator();
    auto* expAll = menu.addAction(tr("Expand All"));
    connect(expAll, &QAction::triggered, this, [this] {
        m_collapsedProcesses.clear();
        connectionsTree->expandAll();
    });
    auto* collAll = menu.addAction(tr("Collapse All"));
    connect(collAll, &QAction::triggered, this, [this] {
        const int count = connectionsTreeFilterModel ? connectionsTreeFilterModel->rowCount() : 0;
        for (int i = 0; i < count; ++i) {
            const QModelIndex idx = connectionsTreeFilterModel->index(i, 0);
            const QString proc = idx.data(ConnectionsTreeModel::ProcessNameRole).toString();
            if (!proc.isEmpty()) m_collapsedProcesses.insert(proc);
        }
        connectionsTree->collapseAll();
    });

    menu.exec(globalPos);
}
