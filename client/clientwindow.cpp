#include "clientwindow.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QDebug>
#include <QFrame>
#include <Qdir>
#include <QFileInfoList>

ClientWindow::ClientWindow(QWidget* parent) : QMainWindow(parent){
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(30, 30, 30, 30); 
    layout->setSpacing(20);                     

    button = new QPushButton("Select Directory", this);
    button->setMinimumHeight(60);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(button, 0, Qt::AlignTop);

    QFrame* canvas = new QFrame(this);
    canvas->setStyleSheet("background-color: #333333;");
    canvas->setFrameShape(QFrame::Box);                  
    canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    layout->addWidget(canvas); 

    connect(button, &QPushButton::clicked, this, &ClientWindow::openDirectoryDialog);
}

void ClientWindow::openDirectoryDialog(){
    QString dirPath = QFileDialog::getExistingDirectory(this, "Select Directory", "",
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirPath.isEmpty()) {
        qDebug() << "Selected directory:" << dirPath;
    }

    QDir dir(dirPath);

    QStringList extensions;
    extensions << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp";
    QFileInfoList fileList = dir.entryInfoList(extensions, QDir::Files);

    images.clear();

    for (const QFileInfo& files : fileList) {
        QImage img(files.absoluteFilePath());
        if (!img.isNull()) {
            images.append(img);
        }
        else {
            qDebug() << "Failed to load image:" << files.absoluteFilePath();
        }
    }
    qDebug() << "Loaded" << images.size() << "images from" << dirPath;
}