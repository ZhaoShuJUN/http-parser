#include "gstmpprtspplayer.h"

#include <QMetaObject>
#include <QTimer>
#include <QtGlobal>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

struct GstMppRtspPlayer::PipelineState {
    GstElement *pipeline = nullptr;
    GstBus *bus = nullptr;
    QTimer *busTimer = nullptr;
};

static void onDeepElementAdded(GstBin *, GstBin *, GstElement *element, gpointer) {
    GstElementFactory *factory = gst_element_get_factory(element);
    if (!factory) {
        return;
    }
    const gchar *name = GST_OBJECT_NAME(factory);
    if (name && g_strcmp0(name, "mppvideodec") == 0) {
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "arm-afbc"))
            g_object_set(element, "arm-afbc", FALSE, nullptr);
    }
}

struct PreviewCallbackContext {
    GstMppRtspPlayer *player;
    int generation;
};

static GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData) {
    auto *context = static_cast<PreviewCallbackContext *>(userData);
    auto *player = context->player;
    const int generation = context->generation;
    if (player->streamGeneration() != generation || player->pendingUiFrames() > 0) {
        GstSample *drop = gst_app_sink_pull_sample(sink);
        if (drop) {
            gst_sample_unref(drop);
        }
        return GST_FLOW_OK;
    }

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!caps || !buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    const int w = GST_VIDEO_INFO_WIDTH(&info);
    const int h = GST_VIDEO_INFO_HEIGHT(&info);
    const int stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
    QImage::Format fmt = QImage::Format_Invalid;
    if (GST_VIDEO_INFO_FORMAT(&info) == GST_VIDEO_FORMAT_RGB) {
        fmt = QImage::Format_RGB888;
    } else if (GST_VIDEO_INFO_FORMAT(&info) == GST_VIDEO_FORMAT_BGR) {
        fmt = QImage::Format_RGB888;
    }

    if (fmt != QImage::Format_Invalid && w > 0 && h > 0 && map.data) {
        const QImage view(map.data, w, h, stride, fmt);
        const QImage copy =
            GST_VIDEO_INFO_FORMAT(&info) == GST_VIDEO_FORMAT_BGR ? view.rgbSwapped() : view.copy();
        if (player->reserveFrame(generation))
            QMetaObject::invokeMethod(
                player,
                [player, copy, generation] {
                    if (player->streamGeneration() == generation)
                        player->pushFrame(copy);
                },
                Qt::QueuedConnection);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

GstMppRtspPlayer::GstMppRtspPlayer(QObject *parent) : QObject(parent) {
    static bool inited = false;
    if (!inited) {
        gst_init(nullptr, nullptr);
        inited = true;
    }
}

GstMppRtspPlayer::~GstMppRtspPlayer() {
    stop();
}

bool GstMppRtspPlayer::isPlaying() const {
    QMutexLocker lock(&m_mutex);
    return m_playing;
}

int GstMppRtspPlayer::pendingUiFrames() const {
    return m_pendingUi.loadRelaxed();
}

void GstMppRtspPlayer::addPendingUiFrame() {
    m_pendingUi.storeRelaxed(1);
}

void GstMppRtspPlayer::pushFrame(const QImage &image) {
    m_pendingUi.storeRelaxed(0);
    emit frameReady(image);
}

void GstMppRtspPlayer::pushError(const QString &text) {
    emit errorText(text);
}

void GstMppRtspPlayer::pollBus() {
    if (!m_state || !m_state->bus) {
        return;
    }
    while (true) {
        GstMessage *msg = gst_bus_pop(m_state->bus);
        if (!msg) {
            break;
        }
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            QString text = QString::fromUtf8(err ? err->message : "gstreamer error");
            if (dbg && *dbg) {
                text += QStringLiteral(" | ");
                text += QString::fromUtf8(dbg);
            }
            if (err) {
                g_error_free(err);
            }
            if (dbg) {
                g_free(dbg);
            }
            qWarning("GStreamer video input error");
            text = QStringLiteral("视频流异常，请检查地址、认证与解码插件");
            pushError(text);
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_warning(msg, &err, &dbg);
            qWarning("GStreamer video pipeline warning");
            if (err) {
                g_error_free(err);
            }
            if (dbg) {
                g_free(dbg);
            }
            break;
        }
        case GST_MESSAGE_EOS:
            pushError(QStringLiteral("流结束"));
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }
}

void GstMppRtspPlayer::destroyPipeline() {
    {
        QMutexLocker frames(&m_frameMutex);
        m_generation.fetchAndAddOrdered(1);
        m_pendingUi.storeRelaxed(0);
    }
    if (!m_state) {
        return;
    }
    if (m_state->busTimer) {
        m_state->busTimer->stop();
        m_state->busTimer->deleteLater();
        m_state->busTimer = nullptr;
    }
    if (m_state->pipeline) {
        gst_element_set_state(m_state->pipeline, GST_STATE_NULL);
        gst_element_get_state(m_state->pipeline, nullptr, nullptr, 3 * GST_SECOND);
        gst_object_unref(m_state->pipeline);
        m_state->pipeline = nullptr;
    }
    if (m_state->bus) {
        gst_object_unref(m_state->bus);
        m_state->bus = nullptr;
    }
    delete m_state;
    m_state = nullptr;
    m_playing = false;
    m_pendingUi.storeRelaxed(0);
}

bool GstMppRtspPlayer::buildPipeline(const QString &rtspUrl) {
    const QByteArray loc = rtspUrl.toUtf8();
    const QByteArray desc = "rtspsrc name=src latency=500 drop-on-latency=true protocols=tcp ! "
                            "decodebin name=dec ! "
                            "queue max-size-buffers=2 leaky=downstream ! "
                            "videoconvert n-threads=2 ! "
                            "videoscale method=nearest-neighbour ! "
                            "video/x-raw,format=RGB,width=960,height=540 ! "
                            "appsink name=sink emit-signals=true sync=false max-buffers=1 drop=true";

    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(desc.constData(), &err);
    if (!pipeline) {
        const QString text = QString::fromUtf8(err ? err->message : "parse pipeline failed");
        if (err) {
            g_error_free(err);
        }
        pushError(text);
        return false;
    }
    if (err) {
        g_error_free(err);
    }

    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    if (src) {
        g_object_set(src, "location", loc.constData(), "buffer-mode", 0, nullptr);
        gst_object_unref(src);
    }

    GstElement *dec = gst_bin_get_by_name(GST_BIN(pipeline), "dec");
    if (dec) {
        g_signal_connect(dec, "deep-element-added", G_CALLBACK(onDeepElementAdded), nullptr);
        gst_object_unref(dec);
    }

    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sink) {
        gst_object_unref(pipeline);
        pushError(QStringLiteral("appsink missing"));
        return false;
    }

    GstAppSinkCallbacks cbs = {};
    cbs.new_sample = onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cbs, new PreviewCallbackContext{this, streamGeneration()},
                               [](gpointer ptr) { delete static_cast<PreviewCallbackContext *>(ptr); });
    gst_object_unref(sink);

    auto *state = new PipelineState;
    state->pipeline = pipeline;
    state->bus = gst_element_get_bus(pipeline);
    state->busTimer = new QTimer(this);
    connect(state->busTimer, &QTimer::timeout, this, &GstMppRtspPlayer::pollBus);
    state->busTimer->start(200);

    m_state = state;
    const GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        destroyPipeline();
        pushError(QStringLiteral("pipeline PLAYING failed"));
        return false;
    }
    m_playing = true;
    return true;
}

void GstMppRtspPlayer::play(const QString &rtspUrl) {
    stop();
    QMutexLocker lock(&m_mutex);
    buildPipeline(rtspUrl);
}

void GstMppRtspPlayer::stop() {
    QMutexLocker lock(&m_mutex);
    destroyPipeline();
}

bool GstMppRtspPlayer::reserveFrame(int generation) {
    QMutexLocker guard(&m_frameMutex);
    if (generation != streamGeneration() || m_pendingUi.loadRelaxed() > 0)
        return false;
    m_pendingUi.fetchAndAddRelaxed(1);
    return true;
}
