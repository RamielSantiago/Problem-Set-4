
#include <QMainWindow>
#include <QString>

class QPushButton;

class ClientWindow : public QMainWindow
{
    Q_OBJECT

public:
    ClientWindow(QWidget* parent = nullptr);

private slots:
    void openDirectoryDialog();

private:
    QPushButton* button;
    QStringList filenames;
    QVector<QImage> images;
};