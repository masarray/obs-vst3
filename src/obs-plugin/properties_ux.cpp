#ifdef _WIN32

#include <obs-module.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

constexpr const char* kPropertiesContainerName = "PropertiesContainer";
constexpr const char* kPanelName = "SafeVst3PropertiesPanel";
constexpr const char* kPolishedProperty = "safevst3Polished";
constexpr const char* kOriginalHiddenProperty = "safevst3OriginalHidden";
constexpr const char* kInstalledValue = "installed";
constexpr const char* kBrowseValue = "browse";

QString css_color(const QColor& color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QByteArray combo_value(const QComboBox* combo, int index)
{
    if (!combo || index < 0 || index >= combo->count())
        return {};
    return combo->itemData(index).toByteArray();
}

bool is_source_mode_combo(const QComboBox* combo)
{
    if (!combo || combo->count() != 2)
        return false;

    const QByteArray first = combo_value(combo, 0);
    const QByteArray second = combo_value(combo, 1);
    return (first == kInstalledValue && second == kBrowseValue) ||
           (first == kBrowseValue && second == kInstalledValue);
}

int find_mode_index(const QComboBox* combo, const QByteArray& value)
{
    if (!combo)
        return -1;
    for (int i = 0; i < combo->count(); ++i) {
        if (combo_value(combo, i) == value)
            return i;
    }
    return -1;
}

QComboBox* find_source_combo(QWidget* container)
{
    if (!container)
        return nullptr;
    const auto combos = container->findChildren<QComboBox*>();
    const auto it = std::find_if(combos.begin(), combos.end(), [](QComboBox* combo) {
        return is_source_mode_combo(combo);
    });
    return it == combos.end() ? nullptr : *it;
}

QComboBox* find_plugin_combo(QWidget* container, QComboBox* source_combo)
{
    if (!container)
        return nullptr;
    const auto combos = container->findChildren<QComboBox*>();
    const auto it = std::find_if(combos.begin(), combos.end(), [source_combo](QComboBox* combo) {
        return combo && combo != source_combo && combo->isVisible();
    });
    return it == combos.end() ? nullptr : *it;
}

QPushButton* find_button(QWidget* container, const QString& text)
{
    if (!container || text.isEmpty())
        return nullptr;
    const auto buttons = container->findChildren<QPushButton*>();
    const auto it = std::find_if(buttons.begin(), buttons.end(), [&text](QPushButton* button) {
        return button && button->text() == text;
    });
    return it == buttons.end() ? nullptr : *it;
}

QLineEdit* find_browse_path_edit(QWidget* container)
{
    if (!container)
        return nullptr;
    const auto edits = container->findChildren<QLineEdit*>();
    const auto it = std::find_if(edits.begin(), edits.end(), [](QLineEdit* edit) {
        return edit && edit->isVisible() && edit->isReadOnly();
    });
    return it == edits.end() ? nullptr : *it;
}

QPushButton* find_browse_button(QWidget* container,
                                QPushButton* open_button,
                                QPushButton* rescan_button,
                                QLineEdit* path_edit)
{
    if (!container || !path_edit)
        return nullptr;
    const auto buttons = container->findChildren<QPushButton*>();
    const auto it = std::find_if(buttons.begin(), buttons.end(),
                                 [open_button, rescan_button](QPushButton* button) {
        return button && button != open_button && button != rescan_button && button->isVisible();
    });
    return it == buttons.end() ? nullptr : *it;
}

QLabel* find_status_label(QWidget* container)
{
    if (!container)
        return nullptr;
    const auto labels = container->findChildren<QLabel*>();
    const auto it = std::find_if(labels.begin(), labels.end(), [](QLabel* label) {
        if (!label || !label->isVisible())
            return false;
        const QString text = label->text();
        return text.contains(QStringLiteral(" — Ready — ")) ||
               text.startsWith(QStringLiteral("Plug-in unavailable"));
    });
    return it == labels.end() ? nullptr : *it;
}

QString compact_status(QString status)
{
    constexpr auto marker = " — Ready — ";
    const qsizetype ready = status.indexOf(QString::fromUtf8(marker));
    if (ready >= 0)
        return QStringLiteral("Ready  ·  ") + status.mid(ready + static_cast<qsizetype>(std::char_traits<char>::length(marker)));
    return status;
}

void clone_combo_items(QComboBox* source, QComboBox* target)
{
    if (!source || !target)
        return;

    for (int i = 0; i < source->count(); ++i) {
        target->addItem(source->itemText(i), source->itemData(i));
        const QModelIndex index = source->model()->index(i, 0);
        if (!(source->model()->flags(index) & Qt::ItemIsEnabled)) {
            if (auto* model = target->model())
                model->setData(model->index(i, 0), 0, Qt::UserRole - 1);
        }
    }
    target->setCurrentIndex(source->currentIndex());
}

QString panel_style(const QWidget* panel)
{
    const QPalette palette = panel ? panel->palette() : QPalette{};
    const QColor button = palette.color(QPalette::Button);
    const QColor button_text = palette.color(QPalette::ButtonText);
    const QColor disabled_text = palette.color(QPalette::Disabled, QPalette::ButtonText);
    const QColor mid = palette.color(QPalette::Mid);
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor highlighted_text = palette.color(QPalette::HighlightedText);
    const QColor hover = highlight.lighter(112);
    QColor subtle = palette.color(QPalette::WindowText);
    subtle.setAlpha(180);

    return QStringLiteral(R"CSS(
        QWidget#SafeVst3PropertiesPanel {
            background: transparent;
        }
        QLabel#SafeVst3SectionLabel {
            font-weight: 600;
            padding: 1px 0 2px 0;
        }
        QPushButton#SafeVst3SegmentLeft,
        QPushButton#SafeVst3SegmentRight {
            min-height: 30px;
            padding: 4px 12px;
            background-color: %1;
            color: %2;
            border: 1px solid %3;
        }
        QPushButton#SafeVst3SegmentLeft {
            border-top-left-radius: 5px;
            border-bottom-left-radius: 5px;
            border-top-right-radius: 0px;
            border-bottom-right-radius: 0px;
            border-right-width: 0px;
        }
        QPushButton#SafeVst3SegmentRight {
            border-top-right-radius: 5px;
            border-bottom-right-radius: 5px;
            border-top-left-radius: 0px;
            border-bottom-left-radius: 0px;
        }
        QPushButton#SafeVst3SegmentLeft:checked,
        QPushButton#SafeVst3SegmentRight:checked {
            background-color: %4;
            color: %5;
            border-color: %4;
        }
        QPushButton#SafeVst3Rescan {
            min-height: 30px;
            padding: 4px 10px;
        }
        QComboBox#SafeVst3PluginCombo,
        QLineEdit#SafeVst3PathEdit {
            min-height: 30px;
        }
        QPushButton#SafeVst3Primary {
            min-height: 38px;
            padding: 7px 14px;
            border-radius: 5px;
            border: 1px solid %4;
            background-color: %4;
            color: %5;
            font-weight: 600;
        }
        QPushButton#SafeVst3Primary:hover {
            background-color: %6;
            border-color: %6;
        }
        QPushButton#SafeVst3Primary:disabled {
            background-color: %1;
            color: %7;
            border-color: %3;
        }
        QLabel#SafeVst3Status {
            color: %8;
            padding-top: 1px;
        }
    )CSS")
        .arg(css_color(button),
             css_color(button_text),
             css_color(mid),
             css_color(highlight),
             css_color(highlighted_text),
             css_color(hover),
             css_color(disabled_text),
             css_color(subtle));
}

void hide_original_controls(QWidget* container)
{
    if (!container)
        return;
    const auto widgets = container->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        if (!widget || widget->objectName() == kPanelName)
            continue;
        widget->setProperty(kOriginalHiddenProperty, true);
        widget->hide();
    }
}

void restore_original_controls(QWidget* container)
{
    if (!container)
        return;
    const auto widgets = container->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        if (!widget || !widget->property(kOriginalHiddenProperty).toBool())
            continue;
        widget->setProperty(kOriginalHiddenProperty, false);
        widget->show();
    }
}

void polish_properties_container(QWidget* container)
{
    if (!container || container->objectName() != kPropertiesContainerName ||
        container->property(kPolishedProperty).toBool()) {
        return;
    }

    auto* form = qobject_cast<QFormLayout*>(container->layout());
    QComboBox* source_combo = find_source_combo(container);
    if (!form || !source_combo)
        return;

    QComboBox* plugin_combo = find_plugin_combo(container, source_combo);
    QPushButton* open_button = find_button(container, QString::fromUtf8(obs_module_text("OpenPluginUI")));
    QPushButton* rescan_button = find_button(container, QString::fromUtf8(obs_module_text("RescanVST3")));
    QLineEdit* browse_path = find_browse_path_edit(container);
    QPushButton* browse_button = find_browse_button(container, open_button, rescan_button, browse_path);
    QLabel* status_label = find_status_label(container);

    const QByteArray mode = source_combo->currentData().toByteArray();
    const bool installed_mode = mode == kInstalledValue;
    const bool browse_mode = mode == kBrowseValue;
    if (!installed_mode && !browse_mode)
        return;

    QPointer<QComboBox> source_proxy(source_combo);
    QPointer<QComboBox> plugin_proxy(plugin_combo);
    QPointer<QPushButton> open_proxy(open_button);
    QPointer<QPushButton> rescan_proxy(rescan_button);
    QPointer<QPushButton> browse_proxy(browse_button);

    hide_original_controls(container);

    auto* panel = new QWidget(container);
    panel->setObjectName(kPanelName);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    panel->setStyleSheet(panel_style(panel));

    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* source_label = new QLabel(QStringLiteral("Source"), panel);
    source_label->setObjectName(QStringLiteral("SafeVst3SectionLabel"));
    root->addWidget(source_label);

    auto* source_row = new QHBoxLayout();
    source_row->setContentsMargins(0, 0, 0, 0);
    source_row->setSpacing(8);

    auto* segment_box = new QWidget(panel);
    auto* segment_layout = new QHBoxLayout(segment_box);
    segment_layout->setContentsMargins(0, 0, 0, 0);
    segment_layout->setSpacing(0);

    auto* installed = new QPushButton(QStringLiteral("Installed Plug-ins"), segment_box);
    installed->setObjectName(QStringLiteral("SafeVst3SegmentLeft"));
    installed->setCheckable(true);
    installed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* browse = new QPushButton(QStringLiteral("Browse Manually"), segment_box);
    browse->setObjectName(QStringLiteral("SafeVst3SegmentRight"));
    browse->setCheckable(true);
    browse->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* source_group = new QButtonGroup(panel);
    source_group->setExclusive(true);
    source_group->addButton(installed);
    source_group->addButton(browse);
    installed->setChecked(installed_mode);
    browse->setChecked(browse_mode);

    segment_layout->addWidget(installed, 1);
    segment_layout->addWidget(browse, 1);
    source_row->addWidget(segment_box, 1);

    auto* rescan = new QPushButton(QStringLiteral("Rescan"), panel);
    rescan->setObjectName(QStringLiteral("SafeVst3Rescan"));
    rescan->setVisible(installed_mode && !rescan_proxy.isNull());
    rescan->setEnabled(!rescan_proxy.isNull() && rescan_proxy->isEnabled());
    source_row->addWidget(rescan, 0);
    root->addLayout(source_row);

    QObject::connect(installed, &QPushButton::clicked, panel, [source_proxy]() {
        if (!source_proxy)
            return;
        const int index = find_mode_index(source_proxy, kInstalledValue);
        if (index >= 0)
            source_proxy->setCurrentIndex(index);
    });
    QObject::connect(browse, &QPushButton::clicked, panel, [source_proxy]() {
        if (!source_proxy)
            return;
        const int index = find_mode_index(source_proxy, kBrowseValue);
        if (index >= 0)
            source_proxy->setCurrentIndex(index);
    });
    QObject::connect(rescan, &QPushButton::clicked, panel, [rescan_proxy]() {
        if (rescan_proxy)
            rescan_proxy->click();
    });

    auto* plugin_label = new QLabel(installed_mode ? QStringLiteral("Plug-in") : QStringLiteral("Plug-in File"), panel);
    plugin_label->setObjectName(QStringLiteral("SafeVst3SectionLabel"));
    root->addWidget(plugin_label);

    if (installed_mode) {
        auto* plugin = new QComboBox(panel);
        plugin->setObjectName(QStringLiteral("SafeVst3PluginCombo"));
        plugin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        clone_combo_items(plugin_proxy, plugin);
        root->addWidget(plugin);

        QObject::connect(plugin, qOverload<int>(&QComboBox::currentIndexChanged), panel,
                         [plugin_proxy](int index) {
            if (plugin_proxy && index >= 0 && index < plugin_proxy->count())
                plugin_proxy->setCurrentIndex(index);
        });
    } else {
        auto* path_row = new QHBoxLayout();
        path_row->setContentsMargins(0, 0, 0, 0);
        path_row->setSpacing(8);

        auto* path = new QLineEdit(panel);
        path->setObjectName(QStringLiteral("SafeVst3PathEdit"));
        path->setReadOnly(true);
        path->setPlaceholderText(QStringLiteral("Choose a .vst3 plug-in…"));
        if (browse_path)
            path->setText(browse_path->text());

        auto* choose = new QPushButton(QStringLiteral("Browse…"), panel);
        choose->setEnabled(!browse_proxy.isNull() && browse_proxy->isEnabled());
        QObject::connect(choose, &QPushButton::clicked, panel, [browse_proxy]() {
            if (browse_proxy)
                browse_proxy->click();
        });

        path_row->addWidget(path, 1);
        path_row->addWidget(choose, 0);
        root->addLayout(path_row);
    }

    auto* open = new QPushButton(QStringLiteral("Open Plug-in"), panel);
    open->setObjectName(QStringLiteral("SafeVst3Primary"));
    open->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    open->setEnabled(!open_proxy.isNull() && open_proxy->isEnabled());
    QObject::connect(open, &QPushButton::clicked, panel, [open_proxy]() {
        if (open_proxy)
            open_proxy->click();
    });
    root->addWidget(open);

    if (status_label && !status_label->text().isEmpty()) {
        auto* status = new QLabel(compact_status(status_label->text()), panel);
        status->setObjectName(QStringLiteral("SafeVst3Status"));
        status->setWordWrap(true);
        if (status->text().startsWith(QStringLiteral("Plug-in unavailable"))) {
            QColor warning = panel->palette().color(QPalette::BrightText);
            if (warning.lightness() > 220)
                warning = QColor(230, 170, 60);
            status->setStyleSheet(QStringLiteral("color:%1;").arg(css_color(warning)));
        }
        root->addWidget(status);
    }

    form->addRow(panel);
    container->setProperty(kPolishedProperty, true);
}

void unpolish_all()
{
    const auto widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) {
        if (!widget || widget->objectName() != kPropertiesContainerName ||
            !widget->property(kPolishedProperty).toBool()) {
            continue;
        }
        if (auto* panel = widget->findChild<QWidget*>(kPanelName))
            delete panel;
        restore_original_controls(widget);
        widget->setProperty(kPolishedProperty, false);
    }
}

class PropertiesUxEventFilter final : public QObject {
public:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event && event->type() == QEvent::Show) {
            if (auto* widget = qobject_cast<QWidget*>(watched);
                widget && widget->objectName() == kPropertiesContainerName) {
                polish_properties_container(widget);
            }
        }
        return false;
    }
};

class PropertiesUxRuntime final {
public:
    PropertiesUxRuntime()
    {
        app_ = qobject_cast<QApplication*>(QCoreApplication::instance());
        if (!app_)
            return;

        filter_ = new PropertiesUxEventFilter();
        app_->installEventFilter(filter_);

        const auto widgets = QApplication::allWidgets();
        for (QWidget* widget : widgets) {
            if (widget && widget->objectName() == kPropertiesContainerName)
                polish_properties_container(widget);
        }
    }

    ~PropertiesUxRuntime()
    {
        if (app_)
            unpolish_all();
        if (app_ && filter_)
            app_->removeEventFilter(filter_);
        delete filter_;
        filter_ = nullptr;
    }

private:
    QPointer<QApplication> app_;
    PropertiesUxEventFilter* filter_ = nullptr;
};

PropertiesUxRuntime g_properties_ux_runtime;

} // namespace

#endif
