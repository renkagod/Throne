#include "include/ui/mainwindow.h"
#include "include/api/RPC.h"
#include "include/database/entities/RouteProfile.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/LocalNetwork.hpp"
#include "include/ui/utils/ConnectionCloseDelegate.h"
#include "include/ui/utils/ConnectionsFilterHeader.h"
#include "include/ui/utils/ConnectionsFilterProxyModel.h"
#include "include/ui/utils/ConnectionsTableModel.h"
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
#include <QTableView>
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

    QIcon MakeTreeIcon(const QColor& color)
    {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(color, 1.5));
        p.drawRect(1, 2, 4, 3);
        p.drawLine(3, 5, 3, 13);
        p.drawLine(3, 8, 7, 8);
        p.drawRect(7, 7, 7, 2);
        p.drawLine(3, 13, 7, 13);
        p.drawRect(7, 12, 7, 2);
        p.end();
        return QIcon(pixmap);
    }

    QIcon MakeTableIcon(const QColor& color)
    {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(color, 1.5));
        p.drawRect(1, 1, 13, 13);
        p.drawLine(1, 5, 14, 5);
        p.drawLine(5, 5, 5, 14);
        p.drawLine(10, 5, 10, 14);
        p.end();
        return QIcon(pixmap);
    }
}

void MainWindow::setupConnectionList()
{
    connectionsModel = new ConnectionsTableModel(this);
    connectionsFilterModel = new ConnectionsFilterProxyModel(this);
    connectionsFilterModel->setSourceModel(connectionsModel);
    ui->connections->setModel(connectionsFilterModel);

    // Order matters: setModel() after this would re-init the sections and drop the resize modes below.
    connectionFilterHeader = new ConnectionsFilterHeader(ui->connections);
    ui->connections->setHorizontalHeader(connectionFilterHeader);

    connectionCloseDelegate = new ConnectionCloseDelegate(this);
    ui->connections->setItemDelegateForColumn(ConnectionsTableModel::ColClose, connectionCloseDelegate);
    connect(connectionCloseDelegate, &ConnectionCloseDelegate::closeRequested, this,
            [this](const QString& id) { closeConnections({id}); });
    connect(connectionCloseDelegate, &ConnectionCloseDelegate::closeMultipleRequested, this,
            [this](const QStringList& ids) { closeConnections(ids); });

    auto* header = ui->connections->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(ConnectionsTableModel::ColSource, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColDest, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTableModel::ColProcess, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTableModel::ColProtocol, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColOutbound, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColTraffic, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColSpeed, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColClose, QHeaderView::Fixed);
    ui->connections->setColumnWidth(ConnectionsTableModel::ColClose, ConnectionCloseDelegate::ColumnWidth);
    ui->connections->verticalHeader()->hide();

    header->setResizeContentsPrecision(20);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->connections->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->connections->setAlternatingRowColors(true);
    ui->connections->setWordWrap(false);

    setupConnectionTree();

    refreshConnectionCloseIcons();
    restoreConnectionSort();
    setupConnectionSortMenu();
    setupConnectionFilter();

    ui->connections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->connections, &QWidget::customContextMenuRequested, this, &MainWindow::onConnectionContextMenu);

    connect(ui->connections, &QAbstractItemView::clicked, this, [this](const QModelIndex& index)
    {
        if (!index.isValid() || index.column() == ConnectionsTableModel::ColClose) return;
        const auto text = index.data(Qt::DisplayRole).toString();
        if (text.isEmpty()) return;

        QApplication::clipboard()->setText(text);
        const QPoint pos = ui->connections->viewport()->mapToGlobal(ui->connections->visualRect(index).center());
        QToolTip::showText(pos, tr("Copied!"), this);
        auto r = ++toolTipID;
        QTimer::singleShot(1500, this, [=,this] {
            if (r != toolTipID)
            {
                return;
            }
            QToolTip::hideText();
        });
    });

    syncConnectionSourceColumn();
    setConnectionViewMode(isConnectionTreeView());
}

void MainWindow::setupConnectionTree()
{
    connectionsTreeModel = new ConnectionsTreeModel(this);
    connectionsTreeFilterModel = new ConnectionsTreeFilterProxyModel(this);
    connectionsTreeFilterModel->setSourceModel(connectionsTreeModel);

    connectionsTree = new QTreeView(ui->connections_tab);
    ui->horizontalLayout_4->addWidget(connectionsTree);
    connectionsTree->setModel(connectionsTreeFilterModel);

    connectionsTreeFilterHeader = new ConnectionsTreeFilterHeader(connectionsTree);
    connectionsTree->setHeader(connectionsTreeFilterHeader);

    connectionsTree->setItemDelegateForColumn(ConnectionsTreeModel::ColClose, connectionCloseDelegate);

    auto* header = connectionsTree->header();
    header->setHighlightSections(false);
    header->setSectionResizeMode(ConnectionsTreeModel::ColTarget, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTreeModel::ColSource, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColProtocol, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColOutbound, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColTraffic, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColSpeed, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTreeModel::ColClose, QHeaderView::Fixed);
    connectionsTree->setColumnWidth(ConnectionsTreeModel::ColClose, ConnectionCloseDelegate::ColumnWidth);

    header->setResizeContentsPrecision(20);
    connectionsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connectionsTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    connectionsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    connectionsTree->setAlternatingRowColors(true);
    connectionsTree->setWordWrap(false);
    connectionsTree->setAnimated(true);

    connectionsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(connectionsTree, &QWidget::customContextMenuRequested, this, &MainWindow::onTreeConnectionContextMenu);

    connect(connectionsTree, &QAbstractItemView::clicked, this, [this](const QModelIndex& index)
    {
        if (!index.isValid() || index.column() == ConnectionsTreeModel::ColClose) return;
        const auto text = index.data(Qt::DisplayRole).toString();
        if (text.isEmpty()) return;

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
        if (index == ConnectionsTreeModel::ColClose) return;

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
}

bool MainWindow::isConnectionTreeView() const
{
    const auto* settings = Configs::dataManager->settingsRepo.get();
    return settings ? settings->connection_tree_view : false;
}

void MainWindow::setConnectionViewMode(bool treeMode)
{
    auto* settings = Configs::dataManager->settingsRepo.get();
    if (settings) {
        settings->connection_tree_view = treeMode;
        settings->Save();
    }

    ui->connections->setVisible(!treeMode);
    if (connectionsTree) connectionsTree->setVisible(treeMode);

    updateConnectionViewModeButton(treeMode);
}

void MainWindow::updateConnectionViewModeButton(bool treeMode)
{
    if (connectionViewModeButton == nullptr) return;

    const QColor textCol = palette().color(QPalette::ButtonText);
    if (treeMode) {
        connectionViewModeButton->setIcon(MakeTableIcon(textCol));
        connectionViewModeButton->setToolTip(tr("Switch to Flat Table"));
    } else {
        connectionViewModeButton->setIcon(MakeTreeIcon(textCol));
        connectionViewModeButton->setToolTip(tr("Switch to Process Tree"));
    }
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
    auto* btnFilter = new QToolButton(this);
    btnFilter->setIcon(QIcon(":/icon/filter.png"));
    btnFilter->setToolTip(tr("Enable Filter"));
    btnFilter->setCheckable(true);
    connect(btnFilter, &QToolButton::toggled, this, [this](bool visible) {
        if (connectionFilterHeader) connectionFilterHeader->setFiltersVisible(visible);
        if (connectionsTreeFilterHeader) connectionsTreeFilterHeader->setFiltersVisible(visible);
    });
    connect(connectionFilterHeader, &ConnectionsFilterHeader::closeRequested, btnFilter, [btnFilter] { btnFilter->setChecked(false); });
    if (connectionsTreeFilterHeader) {
        connect(connectionsTreeFilterHeader, &ConnectionsTreeFilterHeader::closeRequested, btnFilter, [btnFilter] { btnFilter->setChecked(false); });
    }

    connectionCloseAllButton = new QToolButton(this);
    connectionCloseAllButton->setIcon(connectionCloseIcon);
    connectionCloseAllButton->setToolTip(tr("Close every connection listed below"));
    connect(connectionCloseAllButton, &QToolButton::clicked, this, [this] { closeConnections(listedConnectionIds()); });

    connectionViewModeButton = new QToolButton(this);
    updateConnectionViewModeButton(isConnectionTreeView());

    auto* viewMenu = new QMenu(connectionViewModeButton);
    auto* actTable = viewMenu->addAction(tr("Flat Table"));
    actTable->setCheckable(true);
    auto* actTree = viewMenu->addAction(tr("Process Tree"));
    actTree->setCheckable(true);

    auto updateMenuChecks = [this, actTable, actTree] {
        const bool tree = isConnectionTreeView();
        actTable->setChecked(!tree);
        actTree->setChecked(tree);
    };
    connect(viewMenu, &QMenu::aboutToShow, this, updateMenuChecks);

    connect(actTable, &QAction::triggered, this, [this] { setConnectionViewMode(false); });
    connect(actTree, &QAction::triggered, this, [this] { setConnectionViewMode(true); });

    connectionViewModeButton->setMenu(viewMenu);
    connectionViewModeButton->setPopupMode(QToolButton::DelayedPopup);
    connect(connectionViewModeButton, &QToolButton::clicked, this, [this] {
        setConnectionViewMode(!isConnectionTreeView());
    });

    auto* corner = new QWidget(this);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(2);
    cornerLayout->addWidget(connectionViewModeButton);
    cornerLayout->addWidget(btnFilter);
    cornerLayout->addWidget(connectionCloseAllButton);
    ui->stats_widget->setCornerWidget(corner, Qt::TopRightCorner);

    auto syncCorner = [=,this] { corner->setVisible(ui->stats_widget->currentWidget() == ui->connections_tab); };
    connect(ui->stats_widget, &QTabWidget::currentChanged, this, [syncCorner](int) { syncCorner(); });
    syncCorner();

    connectionFilterDebounce = new QTimer(this);
    connectionFilterDebounce->setSingleShot(true);
    connectionFilterDebounce->setInterval(50);
    connect(connectionFilterDebounce, &QTimer::timeout, this, [this] { applyConnectionFilters(); });
    connect(connectionFilterHeader, &ConnectionsFilterHeader::filtersChanged, this, [this] { connectionFilterDebounce->start(); });
    if (connectionsTreeFilterHeader) {
        connect(connectionsTreeFilterHeader, &ConnectionsTreeFilterHeader::filtersChanged, this, [this] { connectionFilterDebounce->start(); });
    }
}

void MainWindow::applyConnectionFilters()
{
    if (isConnectionTreeView() && connectionsTreeFilterHeader != nullptr) {
        const auto filters = connectionsTreeFilterHeader->filters();
        if (connectionsTreeFilterModel != nullptr) {
            connectionsTreeFilterModel->setFilters(filters.source, filters.dest, filters.process,
                                                   filters.protocol, filters.outbound);
        }
        if (connectionsFilterModel != nullptr) {
            connectionsFilterModel->setFilters(filters.source, filters.dest, filters.process,
                                               filters.protocol, filters.outbound);
        }
    } else if (connectionFilterHeader != nullptr) {
        const auto filters = connectionFilterHeader->filters();
        if (connectionsFilterModel != nullptr) {
            connectionsFilterModel->setFilters(filters.source, filters.dest, filters.process,
                                               filters.protocol, filters.outbound);
        }
        if (connectionsTreeFilterModel != nullptr) {
            connectionsTreeFilterModel->setFilters(filters.source, filters.dest, filters.process,
                                                   filters.protocol, filters.outbound);
        }
    }
}

void MainWindow::syncConnectionSourceColumn()
{
    const bool show = LocalNetwork::LanInboundEnabled();

    if (connectionsModel != nullptr) {
        if (ui->connections->isColumnHidden(ConnectionsTableModel::ColSource) != !show) {
            ui->connections->setColumnHidden(ConnectionsTableModel::ColSource, !show);
            connectionFilterHeader->adjustPositions();
            // Both must be cleared here: a hidden header can be reached by neither the filter field nor a sort click.
            if (!show) {
                connectionFilterHeader->clearFilterFor(ConnectionsTableModel::ColSource);
                if (Stats::connection_lister->getSort() == Stats::BySource) applyConnectionSort(Stats::Default);
            }
        }
    }

    if (connectionsTree != nullptr && connectionsTreeModel != nullptr) {
        if (connectionsTree->isColumnHidden(ConnectionsTreeModel::ColSource) != !show) {
            connectionsTree->setColumnHidden(ConnectionsTreeModel::ColSource, !show);
            if (connectionsTreeFilterHeader != nullptr) {
                connectionsTreeFilterHeader->adjustPositions();
                if (!show) {
                    connectionsTreeFilterHeader->clearFilterFor(ConnectionsTreeModel::ColSource);
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

    installSortMenu(ui->connections->horizontalHeader(),
                    ConnectionsTableModel::ColTraffic, ConnectionsTableModel::ColSpeed);
    if (connectionsTree != nullptr) {
        installSortMenu(connectionsTree->header(),
                        ConnectionsTreeModel::ColTraffic, ConnectionsTreeModel::ColSpeed);
    }
}

void MainWindow::refreshConnectionCloseIcons()
{
    // ApplyTheme() fires PaletteChange from the constructor, before setupUi() has built the table.
    if (connectionCloseDelegate == nullptr) return;

    connectionCloseIcon = RecolorIcon(":/icon/material/cancel.png", palette().color(QPalette::ButtonText));
    connectionCloseDelegate->setIcon(connectionCloseIcon);
    if (connectionCloseAllButton != nullptr) connectionCloseAllButton->setIcon(connectionCloseIcon);
    updateConnectionViewModeButton(isConnectionTreeView());
    ui->connections->viewport()->update();
    if (connectionsTree != nullptr) connectionsTree->viewport()->update();
}

QStringList MainWindow::listedConnectionIds() const
{
    QStringList ids;
    if (isConnectionTreeView()) {
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

    if (connectionsFilterModel == nullptr) return ids;
    const int rows = connectionsFilterModel->rowCount();
    ids.reserve(rows);
    for (int row = 0; row < rows; row++)
    {
        const auto id = connectionsFilterModel->index(row, 0).data(ConnectionsTableModel::ConnIdRole).toString();
        if (!id.isEmpty()) ids << id;
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
    if (connectionsModel != nullptr) {
        connectionsModel->setConnections(connections);
    }

    if (connectionsTreeModel != nullptr && connectionsTree != nullptr) {
        const int scrollPos = connectionsTree->verticalScrollBar()->value();
        connectionsTreeModel->setConnections(connections);

        const int groupCount = connectionsTreeFilterModel ? connectionsTreeFilterModel->rowCount()
                                                          : connectionsTreeModel->rowCount();
        for (int i = 0; i < groupCount; ++i) {
            const QModelIndex idx = connectionsTreeFilterModel ? connectionsTreeFilterModel->index(i, 0)
                                                               : connectionsTreeModel->index(i, 0);
            const QString proc = idx.data(ConnectionsTreeModel::ProcessNameRole).toString();
            const bool shouldExpand = !m_collapsedProcesses.contains(proc);
            connectionsTree->setExpanded(idx, shouldExpand);
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

void MainWindow::onConnectionContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIndex = ui->connections->indexAt(pos);
    if (!proxyIndex.isValid()) return;

    const QModelIndex sourceIndex = connectionsFilterModel->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) return;

    const auto* meta = connectionsModel->metaAt(sourceIndex.row());
    if (!meta) return;

    const QString domain = meta->domain.trimmed();
    const QString process = meta->process.trimmed();
    const QString host = domain.isEmpty() ? Stats::EndpointHost(meta->dest.trimmed()) : domain;
    if (host.isEmpty() && process.isEmpty()) return;

    ui->connections->setCurrentIndex(proxyIndex);

    const bool isDomain = QHostAddress(host).isNull();
    const QString addressRule = isDomain ? ("suffix:" + host) : ("ip:" + host);
    const QString processRule = "processName:" + process;

    QMenu menu(this);
    const QPoint globalPos = ui->connections->viewport()->mapToGlobal(pos);

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
    if (!host.isEmpty()) addRouteSubmenu(tr("Append \"%1\" to").arg(host), addressRule);
    if (!process.isEmpty()) addRouteSubmenu(tr("Append process \"%1\" to").arg(process), processRule);

    menu.exec(globalPos);
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
            auto* closeAct = menu.addAction(tr("Close connection"));
            const QString id = meta->id;
            connect(closeAct, &QAction::triggered, this, [this, id] { closeConnections({id}); });
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
