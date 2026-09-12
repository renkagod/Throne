#pragma once

#include <array>

#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QToolButton>

#include "include/ui/utils/ConnectionsTreeModel.h"

class ConnectionsTreeFilterHeader : public QHeaderView {
    Q_OBJECT
public:
    struct Filters {
        QString source;
        QString target;
        QString protocol;
        QString outbound;
    };

    explicit ConnectionsTreeFilterHeader(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent) {
        setSectionsClickable(true);
        setDefaultAlignment(Qt::AlignHCenter | Qt::AlignTop);

        target_filter = makeEdit();
        source_filter = makeEdit();
        protocol_filter = makeEdit();
        outbound_filter = makeEdit();

        connect(this, &QHeaderView::sectionResized, this, &ConnectionsTreeFilterHeader::adjustPositions);

        setFiltersVisible(false);
    }

    bool filtersVisible() const { return m_filtersVisible; }

    void clearFilterFor(int column) {
        if (QLineEdit *edit = editForColumn(column)) edit->clear();
    }

    void setFilterText(int column, const QString &text) {
        if (QLineEdit *edit = editForColumn(column)) {
            if (edit->text() != text) edit->setText(text);
        }
    }

    Filters filters() const {
        return {textFor(ConnectionsTreeModel::ColSource),
                textFor(ConnectionsTreeModel::ColTarget),
                textFor(ConnectionsTreeModel::ColProtocol),
                textFor(ConnectionsTreeModel::ColOutbound)};
    }

    QSize sizeHint() const override {
        QSize s = QHeaderView::sizeHint();
        if (m_filtersVisible) {
            s.setHeight(s.height() + 32);
        }
        return s;
    }

protected:
    QSize sectionSizeFromContents(int logicalIndex) const override {
        QSize s = QHeaderView::sectionSizeFromContents(logicalIndex);
        if (m_filtersVisible && editForColumn(logicalIndex) != nullptr) {
            s.setWidth(qMax(s.width(), 120));
        }
        return s;
    }

    void updateGeometries() override {
        QHeaderView::updateGeometries();
        adjustPositions();
    }

    bool eventFilter(QObject *obj, QEvent *event) override {
        if (!qobject_cast<QLineEdit*>(obj)) return QHeaderView::eventFilter(obj, event);

        if (event->type() == QEvent::ShortcutOverride) {
            if (!isTextEditingKey(static_cast<QKeyEvent*>(event))) {
                return QHeaderView::eventFilter(obj, event);
            }
            event->accept();
            return true;
        }

        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            emit closeRequested();
            return true;
        }
        return QHeaderView::eventFilter(obj, event);
    }

public slots:
    void setFiltersVisible(bool visible) {
        m_filtersVisible = visible;

        if (!visible) {
            for (QLineEdit *edit : filterEdits()) edit->clear();
        }

        if (auto btn = qobject_cast<QToolButton*>(sender())) {
            btn->setToolTip(visible ? tr("Disable Filter") : tr("Enable Filter"));
        }

        for (QLineEdit *edit : filterEdits()) edit->setVisible(visible);

        resizeSections();
        emit geometriesChanged();
        adjustPositions();

        if (visible) {
            target_filter->setFocus(Qt::OtherFocusReason);
        }
    }

    void adjustPositions() {
        if (!m_filtersVisible || count() < ConnectionsTreeModel::ColumnCount) return;

        const int editHeight = 24;
        const int topPos = height() - editHeight - 4;

        auto place = [&](QLineEdit *edit, int section) {
            if (isSectionHidden(section)) {
                edit->hide();
                return;
            }
            edit->show();
            edit->setGeometry(sectionViewportPosition(section) + 2, topPos, sectionSize(section) - 4, editHeight);
        };

        place(target_filter, ConnectionsTreeModel::ColTarget);
        place(source_filter, ConnectionsTreeModel::ColSource);
        place(protocol_filter, ConnectionsTreeModel::ColProtocol);
        place(outbound_filter, ConnectionsTreeModel::ColOutbound);
    }

signals:
    void filtersChanged();
    void closeRequested();

private:
    QLineEdit *makeEdit() {
        auto *edit = new QLineEdit(this->viewport());
        edit->setPlaceholderText(tr("Filter..."));
        edit->setClearButtonEnabled(true);
        edit->installEventFilter(this);
        connect(edit, &QLineEdit::textChanged, this, [this] { emit filtersChanged(); });
        return edit;
    }

    QLineEdit *editForColumn(int column) const {
        switch (column) {
        case ConnectionsTreeModel::ColTarget:   return target_filter;
        case ConnectionsTreeModel::ColSource:   return source_filter;
        case ConnectionsTreeModel::ColProtocol: return protocol_filter;
        case ConnectionsTreeModel::ColOutbound: return outbound_filter;
        default:                                return nullptr;
        }
    }

    QString textFor(int column) const {
        QLineEdit *edit = editForColumn(column);
        if (edit == nullptr || isSectionHidden(column)) return {};
        return edit->text();
    }

    std::array<QLineEdit*, 4> filterEdits() const {
        return {target_filter, source_filter, protocol_filter, outbound_filter};
    }

    static bool isTextEditingKey(QKeyEvent *key) {
        if (!(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            return key->key() < Qt::Key_F1 || key->key() > Qt::Key_F35;
        }
        for (auto standard : {QKeySequence::SelectAll, QKeySequence::Copy, QKeySequence::Cut,
                              QKeySequence::Paste, QKeySequence::Undo, QKeySequence::Redo,
                              QKeySequence::MoveToStartOfLine, QKeySequence::MoveToEndOfLine,
                              QKeySequence::SelectStartOfLine, QKeySequence::SelectEndOfLine,
                              QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord}) {
            if (key->matches(standard)) return true;
        }
        return false;
    }

    QLineEdit *target_filter = nullptr;
    QLineEdit *source_filter = nullptr;
    QLineEdit *protocol_filter = nullptr;
    QLineEdit *outbound_filter = nullptr;
    bool m_filtersVisible = false;
};
