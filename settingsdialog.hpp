#ifndef SETTINGSDIALOG_HPP
#define SETTINGSDIALOG_HPP

#include <QDialog>

#include "geminirenderer.hpp"

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    void setGeminiStyle(GeminiStyle const & style);

    GeminiStyle geminiStyle() const {
        return current_style;
    }

private slots:
    void on_std_change_font_clicked();

    void on_pre_change_font_clicked();

    void on_h1_change_font_clicked();

    void on_h2_change_font_clicked();

    void on_h3_change_font_clicked();

    void on_std_change_color_clicked();

    void on_pre_change_color_clicked();

    void on_h1_change_color_clicked();

    void on_h2_change_color_clicked();

    void on_h3_change_color_clicked();

    void on_bg_change_color_clicked();

    void on_link_local_change_color_clicked();

    void on_link_foreign_change_color_clicked();

    void on_link_cross_change_color_clicked();

    void on_link_local_prefix_textChanged(const QString &arg1);

    void on_link_foreign_prefix_textChanged(const QString &arg1);

    void on_auto_theme_currentIndexChanged(int index);

    void on_preview_url_textChanged(const QString &arg1);

    void on_page_margin_valueChanged(double arg1);

private:
    void reloadStylePreview();

    void updateFont(QFont & input);

    void updateColor(QColor & input);

private:
    Ui::SettingsDialog *ui;

    GeminiStyle current_style;
    std::unique_ptr<QTextDocument> preview_document;
};

#endif // SETTINGSDIALOG_HPP