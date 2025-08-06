#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QComboBox>
#include <QLabel>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QDebug>
#include <QByteArray>
#include <QThread>

const char SOH = 0x01;
const char STX = 0x02;
const char ETX = 0x03;
const char ACK = 0x06;
const char CR  = 0x0D;
const char LF  = 0x0A;

QByteArray calculateBCC(const QByteArray &data) {
    char bcc = 0;
    for (char byte : data) bcc ^= byte;
    return QByteArray(1, bcc);
}

class SerialApp : public QMainWindow {
    Q_OBJECT

public:
    SerialApp(QWidget *parent = nullptr) : QMainWindow(parent), serial(new QSerialPort(this)) {
        setupUI();
        connect(serial, &QSerialPort::readyRead, this, &SerialApp::onDataReceived);
        refreshPorts();
    }

private slots:
    void connectPort() {
        if (portCombo->currentText().isEmpty()) {
            QMessageBox::warning(this, "Uyarı", "Lütfen bir port seçin.");
            return;
        }

        serial->setPortName(portCombo->currentText());
        serial->setBaudRate(300);
        serial->setDataBits(QSerialPort::Data7);
        serial->setParity(QSerialPort::EvenParity);
        serial->setStopBits(QSerialPort::OneStop);
        serial->setFlowControl(QSerialPort::NoFlowControl);

        if (serial->open(QIODevice::ReadWrite)) {
            log("Bağlantı kuruldu (300bps)", true);
            connectBtn->setEnabled(false);
            disconnectBtn->setEnabled(true);
        } else {
            log("Bağlantı hatası: " + serial->errorString(), false);
            QMessageBox::critical(this, "Hata", serial->errorString());
        }
    }

    void disconnectPort() {
        serial->close();
        log("Bağlantı kesildi", false);
        connectBtn->setEnabled(true);
        disconnectBtn->setEnabled(false);
    }

    void refreshPorts() {
    portCombo->clear();
    int aktifPortSayisi = 0;

    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        QSerialPort testPort;
        testPort.setPort(info);
        if (testPort.open(QIODevice::ReadWrite)) {
            portCombo->addItem(info.portName());
            testPort.close();
            aktifPortSayisi++;
        }
    }

    log(QString("Port listesi güncellendi (%1 aktif port bulundu)").arg(aktifPortSayisi), true);
}


    void onDataReceived() {
        static QByteArray buffer;
        static QByteArray hexLine, asciiLine;
        static bool switchedTo9600 = false;

        buffer += serial->readAll();

        for (char byte : std::as_const(buffer)) {
            hexLine.append(QString("%1 ").arg((quint8)byte, 2, 16, QLatin1Char('0')).toUpper().toLatin1());
            char asciiChar = (byte >= 32 && byte <= 126) ? byte : '.';
            asciiLine.append(QByteArray(1, asciiChar));

            if (byte == '\n') {
                QString hexStr = QString::fromLatin1(hexLine).trimmed();
                QString asciiStr = QString::fromLatin1(asciiLine).trimmed();

                log("GELEN (HEX): " + hexStr, false);
                log("GELEN (ASCII): " + asciiStr, false);

                // Kimlik sorgusu
                if (asciiStr.contains("/?!")) {
                    log("→ Kimlik sorgusu alındı", true);
                    QByteArray kimlik = QByteArray("/SAT6EM72000656621\r\n");
                    serial->write(kimlik);
                    log("← Kimlik cevabı gönderildi: " + kimlik, true);
                }

                // ACK050 kontrolü
                if (!switchedTo9600 && asciiStr.contains("050") && hexStr.startsWith("06")) {
                    log("→ ACK050 alındı, baud rate değiştiriliyor...", true);
                    serial->write(QByteArray(1, ACK));
                    serial->flush();
                    QThread::msleep(200);  // cihazın tepki süresi için
                    if (serial->setBaudRate(QSerialPort::Baud9600)) {
                        switchedTo9600 = true;
                        log("← ACK gönderildi, baud rate 9600 yapıldı", true);
                    } else {
                        log("⚠️ Baudrate değiştirilemedi!", false);
                    }
                }

                // OBIS 1.8.0 sorgusu (yalnızca 9600 baud'dayken beklenir)
                if (switchedTo9600 && buffer.contains(QByteArray(1, SOH) + "R2" + QByteArray(1, STX) + "1.8.0()" + QByteArray(1, ETX))) {
                    log("→ OBIS 1.8.0 sorgusu alındı", true);
                    QByteArray payload = QByteArray(1, STX) + "1.8.0(000123.456*kWh)" + QByteArray(1, ETX);
                    QByteArray bcc = calculateBCC(payload);
                    serial->write(payload + bcc);
                    log("← OBIS tüketim cevabı gönderildi", true);
                    buffer.clear();
                    return;
                }

                hexLine.clear();
                asciiLine.clear();
            }
        }

        if (buffer.size() > 1024) buffer = buffer.right(512);
        buffer.clear();
    }

private:
    QSerialPort *serial;
    QComboBox *portCombo;
    QPushButton *connectBtn;
    QPushButton *disconnectBtn;
    QPushButton *refreshBtn;
    QTextEdit *logBox;

    void setupUI() {
        QWidget *central = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(central);
        setCentralWidget(central);

        QHBoxLayout *portLayout = new QHBoxLayout();
        portCombo = new QComboBox();
        connectBtn = new QPushButton("Bağlan");
        disconnectBtn = new QPushButton("Kes");
        refreshBtn = new QPushButton("Yenile");
        disconnectBtn->setEnabled(false);

        portLayout->addWidget(new QLabel("Port:"));
        portLayout->addWidget(portCombo);
        portLayout->addWidget(connectBtn);
        portLayout->addWidget(disconnectBtn);
        portLayout->addWidget(refreshBtn);

        logBox = new QTextEdit();
        logBox->setReadOnly(true);

        layout->addLayout(portLayout);
        layout->addWidget(logBox);

        connect(connectBtn, &QPushButton::clicked, this, &SerialApp::connectPort);
        connect(disconnectBtn, &QPushButton::clicked, this, &SerialApp::disconnectPort);
        connect(refreshBtn, &QPushButton::clicked, this, &SerialApp::refreshPorts);

        setWindowTitle("IEC 62056-21 Sanal Sayaç");
        resize(800, 500);
    }

    void log(const QString &msg, bool green = false) {
        QString color = green ? "green" : "black";
        logBox->append(QString("<font color='%1'>%2</font>").arg(color, msg));
    }
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    SerialApp window;
    window.show();
    return app.exec();
}
