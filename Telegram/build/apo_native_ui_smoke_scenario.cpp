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
#include "apo/apo_behavior_ledger.h"
#include "apo/apo_verifier.h"
#include "apo/apo_executor.h"
#include "apo/apo_cognitive_loop.h"
#include <limits>
#include <thread>
#include "test/test_widgets.h"
#include "intro/intro_widget.h"
#include "intro/intro_qr.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "settings.h"
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSet>
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
            Check(QFile::exists(QDir(cWorkingDir()).filePath(
                    u"apo/raw_behavior.jsonl"_q)),
                u"AP native ledger uses this Telegram working directory"_q);
            Check(QCoreApplication::applicationName() == u"OmegaTelegramDesktop"_q,
                u"Omega Telegram native application name is independent"_q);
            if (window) {
                Check(window->windowTitle().contains(u"\u03A9-Telegram"_q),
                    u"Omega Telegram identity is visible in native window title"_q);
            }
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

    runner->add({
        .name = u"native APΩ mathematical verification"_q,
        .then = [] {
            const apo::ApoStateQuad baseline{ 0.0, 0.07, 1.0, 0.985 };
            const apo::ApoStateQuad valid{ 0.0, 0.05, 1.0, 0.99 };
            const auto eval = apo::ApoVerifier::Instance().evaluateOmega(
                baseline, valid);
            Check(eval.converged && std::abs(eval.deltaD + 0.00375) < 1e-9,
                u"native Ω(P) accepts the sealed convergent fixture"_q);
            const apo::ApoStateQuad negative{ -0.001, 0.07, 1.0, 0.985 };
            Check(!apo::ApoVerifier::Instance().evaluateOmega(
                    baseline, negative).converged,
                u"native Ω(P) rejects a mathematically out-of-domain state"_q);
            const apo::ApoStateQuad infinity{
                std::numeric_limits<double>::infinity(), 0.07, 1.0, 0.985 };
            Check(!apo::ApoVerifier::Instance().evaluateOmega(
                    baseline, infinity).converged,
                u"native Ω(P) rejects infinite state components"_q);
            const apo::ApoStateQuad nan{
                std::numeric_limits<double>::quiet_NaN(), 0.07, 1.0, 0.985 };
            Check(!apo::ApoVerifier::Instance().evaluateOmega(
                    baseline, nan).converged,
                u"native Ω(P) rejects NaN state components"_q);
        },
    });
    runner->add({
        .name = u"APΩ state reduction only logs actual changes"_q,
        .then = [] {
            auto &state = apo::StateBridge::Instance();
            state.updateUserProfile(917045, QString(), QString(),
                QString(), QString(), false);
            state.updateUnreadCount(917045, 7);
            const auto beforeRepeat = apo::BehaviorLedger::Instance().totalEvents();
            state.updateUnreadCount(917045, 7);
            Check(apo::BehaviorLedger::Instance().totalEvents() == beforeRepeat,
                u"unchanged unread count does not append another ledger event"_q);
            state.updateUnreadCount(917045, 8);
            Check(apo::BehaviorLedger::Instance().totalEvents() == beforeRepeat + 1,
                u"changed unread count appends exactly one ledger event"_q);
            state.updateUnreadCount(917045, -1);
            Check(apo::BehaviorLedger::Instance().totalEvents() == beforeRepeat + 1,
                u"invalid negative unread count is ignored"_q);
            state.updateUserProfile(0, QString(), QString(),
                QString(), QString(), false);
        },
    });
    runner->add({
        .name = u"anonymous native cognitive step cannot plan a reply"_q,
        .then = [] {
            const auto self = apo::StateBridge::Instance().currentSelf();
            Check(self.userId == 0,
                u"anonymous cognitive fixture has no authorized account"_q);
            const auto before = apo::BehaviorLedger::Instance().totalEvents();
            apo::CognitiveLoop::Instance().step();
            Check(apo::BehaviorLedger::Instance().totalEvents() == before + 1,
                u"anonymous step records one observation but no fabricated action"_q);
        },
    });
    runner->add({
        .name = u"native server inspection requires authenticated session"_q,
        .then = [] {
            auto receipt = std::make_shared<std::pair<bool, bool>>(false, true);
            const apo::ApoStateQuad valid{ 0.0, 0.05, 1.0, 0.99 };
            apo::ActionExecutor::Instance().submitAction(
                u"dialogs.inspect_server"_q,
                u"native_main_dialogs"_q,
                QJsonObject(),
                u"test fixture verifies no network without authorized session"_q,
                valid,
                [receipt](bool success) {
                    receipt->first = true;
                    receipt->second = success;
                });
            Check(receipt->first && !receipt->second,
                u"native RPC inspection rejects unauthenticated client without dispatch"_q);
        },
    });
    runner->add({
        .name = u"native APΩ action must not fabricate server success"_q,
        .then = [] {
            auto receipt = std::make_shared<std::pair<bool, bool>>(false, true);
            const apo::ApoStateQuad valid{ 0.0, 0.05, 1.0, 0.99 };
            apo::ActionExecutor::Instance().submitAction(
                u"compute.draft_reply"_q,
                u"test_fixture_unread_only"_q,
                QJsonObject{{QStringLiteral("unread_count"), 1}},
                u"native fixture: verify truthful execution boundary"_q,
                valid,
                [receipt](bool success) {
                    receipt->first = true;
                    receipt->second = success;
                });
            Check(receipt->first && !receipt->second,
                u"unwired native dispatch must complete with failure not success"_q);
        },
    });
    // Last test only: synthesize a durable sensory ingress, then stop
    // the real native Runtime. No network write or live account is needed.
    runner->add({
        .name = u"unprocessed anonymous wake is persisted before shutdown"_q,
        .then = [] {
            auto &loop = apo::CognitiveLoop::Instance();
            auto &runtime = apo::Runtime::Instance();
            Check(loop.isRunning() && runtime.isRunning(),
                u"APΩ native runtime and cognitive loop are live before controlled stop"_q);
            const auto sourceTrace = u"fixture_pending_trace"_q;
            const auto admitted = apo::RecordBehavior(
                apo::EventType::UiEvent, sourceTrace,
                u"apo.test.synthetic_sensory_ingress"_q,
                QJsonObject{{QStringLiteral("kind"),
                    QStringLiteral("SYNTHETIC_PENDING_WAKE_STIMULUS")}});
            Check(admitted,
                u"synthetic sensory producer event is durably admitted"_q);
            if (!admitted) return;
            // Reproduce two source-account wakes before the 250 ms timer
            // fires. The old scope must be durably marked unresolved before
            // the anonymous scope replaces it; neither can dispatch.
            const auto oldTrace = u"fixture_old_account_pending"_q;
            const auto admittedOld = apo::RecordBehavior(
                apo::EventType::UiEvent, oldTrace,
                u"apo.test.synthetic_sensory_ingress"_q,
                QJsonObject{{QStringLiteral("kind"),
                    QStringLiteral("SYNTHETIC_ACCOUNT_SWITCH_STIMULUS")}});
            Check(admittedOld,
                u"old account source event was persisted before scope switch"_q);
            if (!admittedOld) return;
            loop.requestWake(u"fixture_old_account_signal"_q,
                oldTrace, QDateTime::currentMSecsSinceEpoch(), 990321);
            const auto beforeSwitch = apo::BehaviorLedger::Instance().totalEvents();
            loop.requestWake(u"fixture_pending_signal"_q,
                sourceTrace, QDateTime::currentMSecsSinceEpoch(), 0);
            Check(apo::BehaviorLedger::Instance().totalEvents() == beforeSwitch + 1,
                u"account switch durably records the pending prior-account wake"_q);
            std::thread worker([] {
                apo::Runtime::Instance().onMtprotoResponse(
                    89000241,
                    u"fixture.queuedResponse"_q,
                    false,
                    QJsonObject());
            });
            worker.join();
            runtime.stop();
            QCoreApplication::sendPostedEvents(&runtime);
            Check(!runtime.isRunning() && !loop.isRunning(),
                u"orderly native runtime shutdown stopped the cognitive loop"_q);
            const auto path = QDir(cWorkingDir()).filePath(
                u"apo/raw_behavior.jsonl"_q);
            QFile raw(path);
            const auto opened = raw.open(QIODevice::ReadOnly);
            Check(opened, u"native RAW ledger remains readable after shutdown seal"_q);
            if (!opened) return;
            QSet<QString> earlierTraces;
            auto pendingCount = 0;
            auto switchedCount = 0;
            auto switchedCorrect = false;
            auto switchedSequence = qint64(-1);
            auto oldSourceSequence = qint64(-1);
            auto pendingSequence = qint64(-1);
            auto sealSequence = qint64(-1);
            auto pendingHasRealParent = false;
            auto pendingContainsFixture = false;
            auto pendingContainsQueuedResponse = false;
            auto pendingCorrectScope = false;
            auto pendingCorrectStatus = false;
            auto queuedResponsePersistedWithoutSuccess = false;
            auto queuedResponseCount = 0;
            auto queuedResponseSequence = qint64(-1);
            auto actionsAfterFixture = 0;
            auto sourceSequence = qint64(-1);
            for (const auto &line : raw.readAll().split('\n')) {
                const auto doc = QJsonDocument::fromJson(line);
                if (!doc.isObject()) continue;
                const auto event = doc.object();
                const auto actor = event.value(QStringLiteral("actor")).toString();
                const auto trace = event.value(QStringLiteral("trace_id")).toString();
                const auto parent = event.value(QStringLiteral("parent_trace_id")).toString();
                const auto sequence = qint64(
                    event.value(QStringLiteral("sequence")).toDouble(-1));
                const auto payload = event.value(QStringLiteral("payload")).toObject();
                if (trace == u"mtproto-89000241"_q
                        && actor == u"apo.mtproto.receiver"_q) {
                    ++queuedResponseCount;
                    queuedResponseSequence = sequence;
                    queuedResponsePersistedWithoutSuccess
                        = !payload.value(QStringLiteral("success")).toBool(true);
                }
                if (trace == sourceTrace
                        && actor == u"apo.test.synthetic_sensory_ingress"_q) {
                    sourceSequence = sequence;
                }
                if (trace == oldTrace
                        && actor == u"apo.test.synthetic_sensory_ingress"_q) {
                    oldSourceSequence = sequence;
                }
                if (actor == u"apo.cognitive.sensory_wake"_q
                        && payload.value(QStringLiteral("result")).toString()
                            == u"NOT_REPLAYED_ACCOUNT_SWITCH_NO_ACTION"_q) {
                    ++switchedCount;
                    switchedSequence = sequence;
                    const auto oldScope = QString::fromUtf8(
                        QCryptographicHash::hash(
                            (QString::number(990321) + QStringLiteral("|")
                                + cWorkingDir()).toUtf8(),
                            QCryptographicHash::Sha256).toHex());
                    switchedCorrect = earlierTraces.contains(oldTrace)
                        && parent == oldTrace
                        && payload.value(QStringLiteral("account_scope")).toString()
                            == oldScope
                        && payload.value(QStringLiteral("scope_kind")).toString()
                            == u"source_account_at_admission"_q
                        && !payload.contains(QStringLiteral("profile_scope"))
                        && payload.value(QStringLiteral("phase")).toString()
                            == u"PENDING"_q
                        && payload.value(QStringLiteral("coalesced_events")).toInt() == 1
                        && payload.value(QStringLiteral("sources")).toArray().contains(
                            QJsonValue(QStringLiteral("fixture_old_account_signal")));
                }
                if (actor == u"apo.cognitive.sensory_wake"_q
                        && payload.value(QStringLiteral("kind")).toString()
                            == u"NATIVE_SENSORY_WAKE_UNRESOLVED"_q
                        && payload.value(QStringLiteral("result")).toString()
                            == u"NOT_REPLAYED_NO_ACTION"_q) {
                    ++pendingCount;
                    pendingSequence = sequence;
                    pendingHasRealParent = earlierTraces.contains(parent);
                    pendingContainsFixture = payload.value(
                        QStringLiteral("sources")).toArray().contains(
                            QJsonValue(QStringLiteral("fixture_pending_signal")));
                    pendingContainsQueuedResponse = payload.value(
                        QStringLiteral("sources")).toArray().contains(
                            QJsonValue(QStringLiteral("mtproto_response")));
                    pendingCorrectScope = payload.value(
                        QStringLiteral("scope_kind")).toString()
                            == u"anonymous_profile"_q
                        && payload.contains(QStringLiteral("profile_scope"))
                        && !payload.contains(QStringLiteral("account_scope"));
                    pendingCorrectStatus = event.value(
                        QStringLiteral("event_type")).toString()
                            == u"RAW.COMPUTATION"_q
                        && payload.value(QStringLiteral("phase")).toString()
                            == u"PENDING"_q
                        && payload.value(QStringLiteral("result")).toString()
                            == u"NOT_REPLAYED_NO_ACTION"_q;
                }
                if (actor == u"apo.runtime"_q
                        && payload.value(QStringLiteral("status")).toString()
                            == u"APO_RUNTIME_STOPPED"_q) {
                    sealSequence = sequence;
                }
                if (sourceSequence > 0 && sequence > sourceSequence
                        && actor == u"apo.executor.native"_q) {
                    ++actionsAfterFixture;
                }
                if (!trace.isEmpty()) earlierTraces.insert(trace);
            }
            Check(switchedCount == 1 && switchedCorrect
                    && oldSourceSequence > sourceSequence
                    && switchedSequence > oldSourceSequence
                    && pendingSequence > switchedSequence,
                u"account switch keeps prior scoped wake and causal order without replay"_q);
            Check(pendingCount == 1 && pendingHasRealParent,
                u"exactly one shutdown wake has an earlier durable producer"_q);
            Check(pendingContainsFixture && pendingCorrectScope,
                u"pending marker preserves anonymous scope and fixture source"_q);
            Check(pendingCorrectStatus,
                u"pending marker never claims an observed or closed action"_q);
            Check(queuedResponseCount == 1
                    && queuedResponsePersistedWithoutSuccess
                    && queuedResponseSequence > 0
                    && pendingSequence > queuedResponseSequence
                    && pendingContainsQueuedResponse,
                u"exactly one queued receiver precedes its linked pending marker"_q);
            Check(sourceSequence > 0 && pendingSequence > sourceSequence
                    && sealSequence > pendingSequence,
                u"native ingress precedes pending wake and durable shutdown seal"_q);
            Check(actionsAfterFixture == 0,
                u"pending sensory stimulus triggers no native executor action"_q);
        },
    });
}
} // namespace Test
#endif // _DEBUG
