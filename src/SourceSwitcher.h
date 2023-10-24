#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <gst/gst.h>
#include <mutex>
#include <string>
#include <thread>

struct SourceSwitcherConfig
{
    std::filesystem::path filepath;
    std::chrono::seconds switchInterval;
};

class SourceSwitcher
{
public:
    SourceSwitcher(const SourceSwitcherConfig &);
    ~SourceSwitcher();
    void LoopRun();
    void Stop();

    void OnRtspNewPad(GstPad *pad);
    void OnRtspDecodeBinNewPad(GstPad *pad);
    void OnFileDecodeBinNewPad(GstPad *pad);
    void OnFileEOS();

private:
    void FreeGstPipeline();
    void SwitchThread();

private:
    SourceSwitcherConfig m_config;
    std::atomic_bool m_stopped = false;
    std::atomic_bool m_fileEOS = false;
    std::mutex m_switchMutex;
    std::thread m_switchThread;
    std::condition_variable m_cv;

    struct
    {
        GMainLoop *loop;
        GstElement *pipeline;

        GstBus *bus;

        GstElement *rtspDecodebin;
        GstElement *fileDecodebin;

        GstElement *fileBin;
        GstElement *rtspBin;
        GstElement *fakesinkBin;
        GstElement *clocksync;

        GstElement *autovideosink;
        GstElement *fakesink;

        GstPad *fakesinkPad = NULL;
        GstPad *autovideosinkPad = NULL;
        GstPad *fileBinPad = NULL;
        GstPad *rtspBinPad = NULL;

    } m_gstPipeline;
};