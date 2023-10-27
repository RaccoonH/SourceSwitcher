#include "SourceSwitcher.h"
#include <gst/rtsp/gstrtsptransport.h>
#include <stdexcept>
#include <thread>

static void cb_message(GstBus *bus,
                       GstMessage *message,
                       gpointer user_data)
{
    auto switcher = reinterpret_cast<SourceSwitcher *>(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError *err = NULL;
        gchar *dbg_info = NULL;
        gst_message_parse_error(message, &err, &dbg_info);
        g_print("%s", err->message);
        switcher->Stop();
        break;
    }
    case GST_MESSAGE_EOS:
        g_print("we reached EOS\n");
        switcher->Stop();
        break;
    default:
        break;
    }
}

static GstPadProbeReturn event_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_DATA(info)) != GST_EVENT_EOS)
        return GST_PAD_PROBE_OK;

    auto switcher = reinterpret_cast<SourceSwitcher *>(user_data);
    switcher->OnFileEOS();
    return GST_PAD_PROBE_HANDLED;
}

static void RtspNewPadCallback(GstElement *, GstPad *pad, gpointer data)
{
    auto switcher = reinterpret_cast<SourceSwitcher *>(data);
    switcher->OnRtspNewPad(pad);
}

static void RtspDecodebinNewPadCallback(GstElement *, GstPad *pad, gpointer data)
{
    auto switcher = reinterpret_cast<SourceSwitcher *>(data);
    switcher->OnRtspDecodeBinNewPad(pad);
}

static void FileDecodebinNewPadCallback(GstElement *, GstPad *pad, gpointer data)
{
    auto switcher = reinterpret_cast<SourceSwitcher *>(data);
    switcher->OnFileDecodeBinNewPad(pad);
}

SourceSwitcher::SourceSwitcher(const SourceSwitcherConfig &config)
{
    if (config.filepath.empty())
        throw std::runtime_error("Filepath is empty!");
    if (config.switchInterval == std::chrono::seconds::zero())
        throw std::runtime_error("Switch interval is zero!");
    if (!std::filesystem::exists(config.filepath))
        throw std::runtime_error("File " + config.filepath.string() + " does not exists");

    m_config = config;

    m_gstPipeline.loop = g_main_loop_new(NULL, FALSE);
    m_gstPipeline.pipeline = gst_pipeline_new("pipeline");

    auto rtspsrc = gst_element_factory_make("rtspsrc", "rtspsrc");
    if (rtspsrc == NULL) {
        FreeGstPipeline();
        throw std::runtime_error("Could not create rtspsrc");
    }
    g_object_set(G_OBJECT(rtspsrc), "location", "rtsp://admin:Admin12345@reg.fuzzun.ru:50232/ISAPI/Streaming/Channels/101", NULL);
    g_object_set(G_OBJECT(rtspsrc), "timeout", 500000, NULL);

    auto filesrc = gst_element_factory_make("filesrc", "filesrc");
    if (filesrc == NULL) {
        FreeGstPipeline();
        throw std::runtime_error("Could not create filesrc");
    }
    g_object_set(G_OBJECT(filesrc), "location", config.filepath.string().c_str(), NULL);

    m_gstPipeline.rtspDecodebin = gst_element_factory_make("decodebin", "rtspDecodebin");
    if (m_gstPipeline.rtspDecodebin == NULL) {
        FreeGstPipeline();
        throw std::runtime_error("Could not create rtspDecodebin");
    }
    m_gstPipeline.fileDecodebin = gst_element_factory_make("decodebin", "fileDecodebin");
    if (m_gstPipeline.fileDecodebin == NULL) {
        FreeGstPipeline();
        throw std::runtime_error("Could not create fileDecodebin");
    }
    m_gstPipeline.autovideosink = gst_element_factory_make("autovideosink", "autovideosink");
    if (m_gstPipeline.autovideosink == NULL) {
        FreeGstPipeline();
        throw std::runtime_error("Could not create autovideosink");
    }
    m_gstPipeline.clocksync = gst_element_factory_make("clocksync", "clocksync");
    m_gstPipeline.fakesink = gst_element_factory_make("fakesink", "fakesink");
    if (m_gstPipeline.fakesink == NULL) {
        FreeGstPipeline();
        throw std::runtime_error("Could not create fakesink");
    }
    m_gstPipeline.fakesinkBin = gst_bin_new("fakesinkBin");
    gst_bin_add_many(GST_BIN(m_gstPipeline.fakesinkBin), m_gstPipeline.clocksync, m_gstPipeline.fakesink, NULL);
    gst_element_link_many(m_gstPipeline.clocksync, m_gstPipeline.fakesink, NULL);

    g_signal_connect(rtspsrc, "pad-added", G_CALLBACK(RtspNewPadCallback), this);
    g_signal_connect(m_gstPipeline.rtspDecodebin, "pad-added", G_CALLBACK(RtspDecodebinNewPadCallback), this);
    g_signal_connect(m_gstPipeline.fileDecodebin, "pad-added", G_CALLBACK(FileDecodebinNewPadCallback), this);

    auto bus = gst_pipeline_get_bus(GST_PIPELINE(m_gstPipeline.pipeline));
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", (GCallback)cb_message, this);

    m_gstPipeline.rtspBin = gst_bin_new("rtspBin");
    gst_bin_add_many(GST_BIN(m_gstPipeline.rtspBin), rtspsrc, m_gstPipeline.rtspDecodebin, NULL);

    m_gstPipeline.fileBin = gst_bin_new("filebin");
    gst_bin_add_many(GST_BIN(m_gstPipeline.fileBin), filesrc, m_gstPipeline.fileDecodebin, NULL);
    gst_element_link_many(filesrc, m_gstPipeline.fileDecodebin, NULL);

    gst_bin_add_many(GST_BIN(m_gstPipeline.pipeline), m_gstPipeline.rtspBin, m_gstPipeline.fileBin, m_gstPipeline.autovideosink, m_gstPipeline.fakesinkBin, NULL);
}

SourceSwitcher::~SourceSwitcher()
{
    FreeGstPipeline();
}

void SourceSwitcher::FreeGstPipeline()
{
    gst_object_unref(GST_OBJECT(m_gstPipeline.pipeline));
    gst_object_unref(m_gstPipeline.autovideosinkPad);
    g_main_loop_unref(m_gstPipeline.loop);
}

void SourceSwitcher::LoopRun()
{
    if (gst_element_set_state(m_gstPipeline.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to change state to GST_STATE_PLAYING \n");
        return;
    }
    m_switchThread = std::thread(&SourceSwitcher::SwitchThread, this);
    g_main_loop_run(m_gstPipeline.loop);
}

void SourceSwitcher::Stop()
{
    m_stopped = true;
    g_main_loop_quit(m_gstPipeline.loop);
    if (gst_element_set_state(m_gstPipeline.pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to change state to GST_STATE_NULL \n");
        return;
    }
    if (m_switchThread.joinable()) {
        m_cv.notify_one();
        m_switchThread.join();
    }
}

void SourceSwitcher::SwitchThread()
{
    std::unique_lock<std::mutex> lock(m_switchMutex);
    while (!m_stopped && !m_fileEOS) {
        if (m_gstPipeline.rtspBinPad == NULL ||
            m_gstPipeline.fileBinPad == NULL ||
            m_gstPipeline.fakesinkPad == NULL ||
            m_gstPipeline.autovideosinkPad == NULL) {
            m_cv.wait(lock);
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        while ((std::chrono::steady_clock::now() - now) < m_config.switchInterval) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            lock.lock();
            if (m_stopped || m_fileEOS)
                return;
        }

        auto rtspPeer = gst_pad_get_peer(m_gstPipeline.rtspBinPad);
        if (!rtspPeer) {
            gst_object_unref(rtspPeer);
            return;
        }

        if (rtspPeer == m_gstPipeline.autovideosinkPad) {
            gst_object_unref(rtspPeer);
            gst_pad_unlink(m_gstPipeline.rtspBinPad, m_gstPipeline.autovideosinkPad);
            gst_pad_unlink(m_gstPipeline.fileBinPad, m_gstPipeline.fakesinkPad);

            auto x = gst_pad_link(m_gstPipeline.fileBinPad, m_gstPipeline.autovideosinkPad);
            if (x != GST_PAD_LINK_OK) {
                g_print("video pad link error\n");
                Stop();
                return;
            }
            x = gst_pad_link(m_gstPipeline.rtspBinPad, m_gstPipeline.fakesinkPad);
            if (x != GST_PAD_LINK_OK) {
                g_print("video pad link error\n");
                Stop();
                return;
            }
        } else {
            gst_object_unref(rtspPeer);
            gst_pad_unlink(m_gstPipeline.fileBinPad, m_gstPipeline.autovideosinkPad);
            gst_pad_unlink(m_gstPipeline.rtspBinPad, m_gstPipeline.fakesinkPad);

            auto x = gst_pad_link(m_gstPipeline.rtspBinPad, m_gstPipeline.autovideosinkPad);
            if (x != GST_PAD_LINK_OK) {
                g_print("video pad link error\n");
                Stop();
                return;
            }
            x = gst_pad_link(m_gstPipeline.fileBinPad, m_gstPipeline.fakesinkPad);
            if (x != GST_PAD_LINK_OK) {
                g_print("video pad link error\n");
                Stop();
                return;
            }
        }
    }
}

void SourceSwitcher::OnRtspNewPad(GstPad *pad)
{
    auto rtspSink = gst_element_get_static_pad(m_gstPipeline.rtspDecodebin, "sink");
    if (GST_PAD_IS_LINKED(rtspSink)) {
        gst_object_unref(rtspSink);
        return;
    }

    if (gst_pad_link(pad, rtspSink) != GST_PAD_LINK_OK) {
        g_print("video pad link error\n");
        gst_object_unref(rtspSink);
        Stop();
        return;
    }

    gst_object_unref(rtspSink);
}
void SourceSwitcher::OnRtspDecodeBinNewPad(GstPad *pad)
{
    m_gstPipeline.autovideosinkPad = gst_element_get_static_pad(m_gstPipeline.autovideosink, "sink");
    if (GST_PAD_IS_LINKED(m_gstPipeline.autovideosinkPad)) {
        return;
    }

    gst_pad_add_probe(m_gstPipeline.autovideosinkPad, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)event_probe_cb, this, NULL);
    m_gstPipeline.rtspBinPad = gst_ghost_pad_new("rtspBinGhost", pad);
    gst_element_add_pad(m_gstPipeline.rtspBin, m_gstPipeline.rtspBinPad);
    if (gst_pad_link(m_gstPipeline.rtspBinPad, m_gstPipeline.autovideosinkPad) != GST_PAD_LINK_OK) {
        g_print("video pad link error\n");
        Stop();
        return;
    }

    m_cv.notify_one();
}

void SourceSwitcher::OnFileDecodeBinNewPad(GstPad *pad)
{
    auto clockSyncSink = gst_element_get_static_pad(m_gstPipeline.clocksync, "sink");
    if (GST_PAD_IS_LINKED(clockSyncSink)) {
        return;
    }

    gst_pad_add_probe(clockSyncSink, GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback)event_probe_cb, this, NULL);
    m_gstPipeline.fileBinPad = gst_ghost_pad_new("fileBinGhost", pad);
    gst_element_add_pad(m_gstPipeline.fileBin, m_gstPipeline.fileBinPad);
    m_gstPipeline.fakesinkPad = gst_ghost_pad_new("fakeSinkPadGhost", clockSyncSink);
    gst_element_add_pad(m_gstPipeline.fakesinkBin, m_gstPipeline.fakesinkPad);

    gst_object_unref(clockSyncSink);
    if (gst_pad_link(m_gstPipeline.fileBinPad, m_gstPipeline.fakesinkPad) != GST_PAD_LINK_OK) {
        g_print("video pad link error\n");
        Stop();
        return;
    }

    m_cv.notify_one();
}

void SourceSwitcher::OnFileEOS()
{
    if (m_fileEOS == true)
        return;

    std::lock_guard<std::mutex> lock(m_switchMutex);
    m_fileEOS = true;

    auto rtspPeer = gst_pad_get_peer(m_gstPipeline.rtspBinPad);
    if (!rtspPeer) {
        gst_object_unref(rtspPeer);
        return;
    }

    if (rtspPeer != m_gstPipeline.autovideosinkPad) {
        gst_object_unref(rtspPeer);
        gst_pad_unlink(m_gstPipeline.fileBinPad, m_gstPipeline.autovideosinkPad);
        gst_pad_unlink(m_gstPipeline.rtspBinPad, m_gstPipeline.fakesinkPad);

        if (gst_pad_link(m_gstPipeline.rtspBinPad, m_gstPipeline.autovideosinkPad) != GST_PAD_LINK_OK) {
            g_print("video pad link error\n");
            Stop();
            return;
        }
        if (gst_pad_link(m_gstPipeline.fileBinPad, m_gstPipeline.fakesinkPad) != GST_PAD_LINK_OK) {
            g_print("video pad link error\n");
            Stop();
            return;
        }
    } else {
        gst_object_unref(rtspPeer);
    }
}