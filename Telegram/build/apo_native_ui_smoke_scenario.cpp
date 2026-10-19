/*
Native Ω-Telegram product smoke scenario.
Uses only Telegram Desktop's existing Debug test harness.
Do not use real Telegram sessions or require live network access.
*/
#ifdef _DEBUG
#include "test/test_runner.h"
#include "test/test_log.h"
#include "test/test_capture.h"
#include "apo/apo_runtime.h"
#include "apo/apo_state_bridge.h"
#include "test/test_widgets.h"
#include "intro/intro_widget.h"
#include "intro/intro_qr.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace Test {
void SetupScenario(not_null<Runner*> runner) {
    const auto productWindow = []() -> QWidget* {
        QWidget *largest = nullptr;
        auto largestArea = 0;
        for (auto widget : QApplication::topLevelWidgets()) {
            if (!widget || !widget->isVisible()) {
                continue;
            }
            const auto area = widget->width() * widget->height();
            if (area > largestArea) {
                largest = widget;
                largestArea = area;
            }
        }
        return largest;
    };

    runner->waitEvent(u"launch_finished"_q);

    runner->add({
        .name = u"native Telegram and AP runtime exist"_q,
        .then = [=] {
            const auto window = productWindow();
            Check(window != nullptr,
                u"native product has a visible top-level QWidget"_q);
            if (window) {
                Check(window->width() >= 600 && window->height() >= 450,
                    u"native product has full-size desktop geometry"_q,
                    u"window-size=%1x%2"_q.arg(window->width()).arg(window->height()));
            }
            Check(apo::Runtime::Instance().isRunning(),
                u"AP native runtime is running inside Telegram.exe"_q);
        },
    });
    runner->add({
        .name = u"isolated unlogged-in state does not invent a Telegram account"_q,
        .then = [] {
            const auto state = apo::StateBridge::Instance().currentSelf();
            Check(state.userId == 0 && state.phone.isEmpty(),
                u"anonymous test run has no Telegram account identity"_q);
            Check(!state.tonWalletBalanceKnown
                    && !state.telegramTonKnown
                    && !state.starsBalanceKnown,
                u"unloaded balances are unknown, not fabricated zero values"_q);
        },
    });
    runner->captureAndInspect(
        u"omega-telegram-native-window"_q,
        productWindow,
        [](QWidget *window) {
            return window->isVisible()
                && window->width() >= 600
                && window->height() >= 450;
        },
        [](QWidget *window, const QImage &image) {
            Check(image.width() >= 600 && image.height() >= 450,
                u"native Telegram widget rasterized to a full-size image"_q,
                u"raster-size=%1x%2"_q.arg(image.width()).arg(image.height()));
            Check(!LooksBlank(image),
                u"native Telegram painted frame is nonblank"_q);
        },
        kStartupStageTimeout);

    // Drive the real Telegram Desktop Start Messaging button. The offline
    // test account must transition to native login UI without any credentials.
    const auto startButton = [productWindow]() -> QWidget* {
        const auto top = productWindow();
        if (!top) {
            return nullptr;
        }
        for (const auto intro : FindVisible<Intro::Widget>(top)) {
            for (const auto button : FindVisible<Ui::RoundButton>(intro)) {
                if (button->accessibilityName()
                        == tr::lng_start_msgs(tr::now)) {
                    return button;
                }
            }
        }
        return nullptr;
    };
    runner->actOnWidget(
        u"native Start Messaging control"_q,
        startButton,
        [](QWidget *button) { Click(button); },
        [](QWidget *button) {
            return button->isVisible() && button->isEnabled();
        },
        kStartupStageTimeout);

    runner->add({
        .name = u"native login flow opens after Start Messaging"_q,
        .until = [productWindow] {
            const auto top = productWindow();
            return top && !FindVisible<Intro::details::QrWidget>(top).empty();
        },
        .then = [productWindow] {
            const auto top = productWindow();
            Check(top
                    && !FindVisible<Intro::details::QrWidget>(top).empty(),
                u"Telegram enters its real QR login step"_q);
            const auto state = apo::StateBridge::Instance().currentSelf();
            Check(state.userId == 0,
                u"entering login UI never fabricates authorization"_q);
        },
        .timeout = kStartupStageTimeout,
    });
}
} // namespace Test
#endif // _DEBUG
