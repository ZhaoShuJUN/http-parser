#ifndef GSTMPPRTSPPLAYER_H
#define GSTMPPRTSPPLAYER_H

#include <QAtomicInt>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>

class GstMppRtspPlayer : public QObject {
    Q_OBJECT
  public:
    explicit GstMppRtspPlayer(QObject *parent = nullptr);
    ~GstMppRtspPlayer() override;

    void play(const QString &rtspUrl);
    void stop();
    bool isPlaying() const;

    int pendingUiFrames() const;
    void addPendingUiFrame();
    bool reserveFrame(int generation);
    int streamGeneration() const {
        return m_generation.loadAcquire();
    }

  public slots:
    void pushFrame(const QImage &image);
    void pushError(const QString &text);

  signals:
    void frameReady(const QImage &image);
    void errorText(const QString &text);

  private:
    struct PipelineState;
    void destroyPipeline();
    bool buildPipeline(const QString &rtspUrl);
    void pollBus();

    PipelineState *m_state = nullptr;
    mutable QMutex m_mutex;
    bool m_playing = false;
    QAtomicInt m_pendingUi{0};
    QAtomicInt m_generation{0};
    QMutex m_frameMutex;
};

#endif
