#include "utils/connection_test.hpp"

#include <atomic>
#include <borealis.hpp>
#include <cmath>
#include <moonbeam.hpp>
#include <numeric>
#include <thread>
#include <vector>

#include "debug.hpp"

#ifdef __PSV__
#include <psp2/kernel/clib.h>
#define VITALOG sceClibPrintf
#else
#define VITALOG(...) ((void)0)
#endif

namespace utils
{

void startConnectionTest(const HostInfo& host)
{
    brls::Box* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::CENTER);
    content->setJustifyContent(brls::JustifyContent::CENTER);
    content->setPadding(20.0f);

    brls::Label* titleLabel = new brls::Label();
    titleLabel->setText(brls::getStr("host_dialog/connection_test/title"));
    titleLabel->setFontSize(20.0f);
    titleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    titleLabel->setMarginBottom(20.0f);
    content->addView(titleLabel);

    brls::ProgressSpinner* spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::NORMAL);
    content->addView(spinner);

    brls::Label* statusLabel = new brls::Label();
    statusLabel->setText(brls::getStr("host_dialog/connection_test/testing"));
    statusLabel->setFontSize(16);
    statusLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    statusLabel->setMarginTop(15.0f);
    content->addView(statusLabel);

    brls::Label* pctLabel = new brls::Label();
    pctLabel->setText("0%");
    pctLabel->setFontSize(14);
    pctLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    pctLabel->setMarginTop(5.0f);
    content->addView(pctLabel);

    brls::Dialog* progressDialog = new brls::Dialog(content);
    progressDialog->setCancelable(false); // Force using the Cancel button to exit safely

    std::string currentLocale = brls::Application::getLocale();
    bool limit24G             = false;
    std::string deviceName    = "Device";
#ifdef __PSV__
    limit24G   = true; // PS Vita is hardware-limited to 2.4GHz
    deviceName = "PS Vita";
#elif defined(__SWITCH__)
    deviceName = "Nintendo Switch";
#elif defined(__WIIU__)
    deviceName = "Wii U";
#endif
    auto tester = std::make_shared<moonbeam::ConnectionTester>(host.ip, limit24G, deviceName, currentLocale);

    progressDialog->addButton(brls::getStr("host_dialog/connection_test/cancel"), [tester, statusLabel]()
        {
        statusLabel->setText(brls::getStr("host_dialog/connection_test/cancelling"));
        tester->cancel(); });

    progressDialog->open();

    tester->setProgressCallback([statusLabel, pctLabel](float progress, const std::string& status)
        { brls::sync([statusLabel, pctLabel, progress, status]()
              {
            statusLabel->setText(status);
            int percent = static_cast<int>(progress * 100.0f);
            pctLabel->setText(std::to_string(percent) + "%"); }); });

    std::thread([tester, progressDialog, host, currentLocale]()
        {
        bool is_es = (currentLocale == "es");
        auto result = tester->run();

        vita_log::info("[Moonbeam] Unified Connection Test finished for Host: %s (%s)", host.name.c_str(), host.ip.c_str());
        if (!result.success) {
            vita_log::error("[Moonbeam] Test failed: %s", result.error_message.c_str());
        } else {
            vita_log::info("[Moonbeam] Quality Rating: %s", result.rating.c_str());
            vita_log::info("[Moonbeam] Latency: Avg: %.2f ms (Min: %.2f, Max: %.2f), Jitter: %.2f ms, Packet Loss: %.2f%%",
                           result.avg_ping_ms, result.min_ping_ms, result.max_ping_ms, result.jitter_ms, result.packet_loss_pct);
            vita_log::info("[Moonbeam] - Video Bitrate Stability Results:");
            for (const auto& r : result.steps) {
                std::string nameStr = (r.bitrate == -1) ? "Maximum/Unlimited" : std::to_string(r.bitrate) + " Kbps";
                vita_log::info("[Moonbeam]   - Bitrate %s: %s (Achieved: %.1f Kbps)",
                               nameStr.c_str(), r.stable ? "STABLE" : "UNSTABLE", r.speed_kbps);
                vita_log::info("[Moonbeam]     Latency under load: Avg: %.2f ms, Min: %.2f ms, Max: %.2f ms, Jitter: %.2f ms",
                               r.avg_latency_ms, r.min_latency_ms, r.max_latency_ms, r.jitter_ms);
                vita_log::info("[Moonbeam]     Stutter/Drop events: %d", r.stutters);
            }
            vita_log::info("[Moonbeam] Recommendation: %s", result.recommendation.c_str());
        }

        brls::sync([result, progressDialog, host, tester, is_es]() {
            progressDialog->close([result, host, tester, is_es]() {
                if (!result.success) {
                    if (result.error_message == "Test cancelled by user." ||
                        result.error_message == "Prueba cancelada por el usuario.") {
                        return;
                    }
                    brls::Dialog* errDialog = new brls::Dialog(result.error_message);
                    errDialog->setCancelable(true);
                    errDialog->addButton(brls::getStr("host_dialog/dialog/ok"), []() {});
                    errDialog->open();
                    return;
                }

                // Show unified report
                brls::Box* reportContent = new brls::Box(brls::Axis::COLUMN);
                reportContent->setAlignItems(brls::AlignItems::FLEX_START);
                reportContent->setJustifyContent(brls::JustifyContent::FLEX_START);
                reportContent->setPadding(20.0f);
                reportContent->setWidth(600.0f);

                brls::Label* repTitleLabel = new brls::Label();
                repTitleLabel->setText(brls::getStr("host_dialog/connection_test/report_title"));
                repTitleLabel->setFontSize(18.0f);
                repTitleLabel->setMarginBottom(10.0f);
                reportContent->addView(repTitleLabel);

                brls::Label* hostNameLabel = new brls::Label();
                hostNameLabel->setText(host.name + " (" + host.ip + ")");
                hostNameLabel->setFontSize(15);
                hostNameLabel->setMarginBottom(10.0f);
                reportContent->addView(hostNameLabel);

                brls::Label* ratingLabel = new brls::Label();
                std::string ratingStr = brls::getStr("host_dialog/connection_test/quality") + " " + result.rating;
                if (result.rating_raw == "Excellent") {
                    ratingLabel->setTextColor(nvgRGB(46, 204, 113));
                } else if (result.rating_raw == "Good") {
                    ratingLabel->setTextColor(nvgRGB(52, 152, 219));
                } else if (result.rating_raw == "Fair") {
                    ratingLabel->setTextColor(nvgRGB(241, 196, 15));
                } else {
                    ratingLabel->setTextColor(nvgRGB(231, 76, 60));
                }
                ratingLabel->setText(ratingStr);
                ratingLabel->setFontSize(18);
                ratingLabel->setMarginBottom(12.0f);
                reportContent->addView(ratingLabel);

                auto addMetric = [reportContent](const std::string& name, const std::string& value) -> brls::Label* {
                    brls::Box* row = new brls::Box(brls::Axis::ROW);
                    row->setMarginBottom(4.0f);

                    brls::Label* nameLabel = new brls::Label();
                    nameLabel->setText(name);
                    nameLabel->setFontSize(14);
                    nameLabel->setWidth(220.0f);
                    row->addView(nameLabel);

                    brls::Label* valLabel = new brls::Label();
                    valLabel->setText(value);
                    valLabel->setFontSize(14);
                    row->addView(valLabel);

                    reportContent->addView(row);
                    return valLabel;
                };

                char buffer[128];

                snprintf(buffer, sizeof(buffer), "%.1f ms (min: %.1f, max: %.1f)", result.avg_ping_ms, result.min_ping_ms, result.max_ping_ms);
                addMetric(brls::getStr("host_dialog/connection_test/avg_latency"), buffer);

                snprintf(buffer, sizeof(buffer), "%.1f ms", result.jitter_ms);
                addMetric(brls::getStr("host_dialog/connection_test/jitter"), buffer);

                snprintf(buffer, sizeof(buffer), "%.1f%%", result.packet_loss_pct);
                addMetric(brls::getStr("host_dialog/connection_test/packet_loss"), buffer);

                // Video stability header
                brls::Label* videoSectionLabel = new brls::Label();
                videoSectionLabel->setText(tester->translate("video_test_label", "Video Stability:"));
                videoSectionLabel->setFontSize(15);
                videoSectionLabel->setMarginTop(12.0f);
                videoSectionLabel->setMarginBottom(6.0f);
                reportContent->addView(videoSectionLabel);

                // Add each result row for the 6 bitrate steps
                for (const auto& r : result.steps) {
                    brls::Box* row = new brls::Box(brls::Axis::ROW);
                    row->setMarginBottom(4.0f);
                    row->setAlignItems(brls::AlignItems::CENTER);

                    brls::Label* brLabel = new brls::Label();
                    if (r.bitrate == -1) {
                        brLabel->setText(tester->translate("video_test_max_bitrate", "Maximum") + ":");
                    } else {
                        brLabel->setText(std::to_string(r.bitrate) + " Kbps:");
                    }
                    brLabel->setFontSize(14);
                    brLabel->setWidth(130.0f);
                    row->addView(brLabel);

                    brls::Label* statusVal = new brls::Label();
                    if (r.stable) {
                        statusVal->setText(tester->translate("video_test_stable", "Stable"));
                        statusVal->setTextColor(nvgRGB(46, 204, 113)); // Green
                    } else {
                        statusVal->setText(tester->translate("video_test_unstable", "Unstable"));
                        statusVal->setTextColor(nvgRGB(231, 76, 60)); // Red
                    }
                    statusVal->setFontSize(14);
                    statusVal->setWidth(100.0f);
                    row->addView(statusVal);

                    brls::Label* detailsVal = new brls::Label();
                    char detailsBuf[128];
                    if (r.stutters > 0) {
                        snprintf(detailsBuf, sizeof(detailsBuf),
                                 is_es ? "Alcanzado: %.0f Kbps, Latencia: %.1f ms (%d tirones)"
                                       : "Achieved: %.0f Kbps, Latency: %.1f ms (%d stutters)",
                                 r.speed_kbps, r.avg_latency_ms, r.stutters);
                    } else {
                        snprintf(detailsBuf, sizeof(detailsBuf),
                                 is_es ? "Alcanzado: %.0f Kbps, Latencia: %.1f ms"
                                       : "Achieved: %.0f Kbps, Latency: %.1f ms",
                                 r.speed_kbps, r.avg_latency_ms);
                    }
                    detailsVal->setText(detailsBuf);
                    detailsVal->setFontSize(13);
                    detailsVal->setTextColor(nvgRGB(180, 180, 180));
                    row->addView(detailsVal);

                    reportContent->addView(row);
                }

                // Add Recommendation
                brls::Label* recTitle = new brls::Label();
                recTitle->setText(brls::getStr("host_dialog/connection_test/recommendation"));
                recTitle->setFontSize(14);
                recTitle->setMarginTop(12.0f);
                recTitle->setMarginBottom(4.0f);
                reportContent->addView(recTitle);

                brls::Label* recLabel = new brls::Label();
                recLabel->setText(result.recommendation);
                recLabel->setFontSize(13);
                recLabel->setWidth(560.0f);
                recLabel->setMarginBottom(10.0f);
                reportContent->addView(recLabel);

                brls::Dialog* reportDialog = new brls::Dialog(reportContent);
                reportDialog->setCancelable(true);
                reportDialog->addButton(brls::getStr("host_dialog/dialog/ok"), []() {});
                reportDialog->open();
            });
        }); })
        .detach();
}

} // namespace utils
