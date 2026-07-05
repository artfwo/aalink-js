#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <napi.h>
#include <thread>

#include <ableton/Link.hpp>

// tasks posted from link/scheduler threads, executed on the JS thread
using Task = std::function<void(Napi::Env)>;

static void call_task(Napi::Env env, Napi::Function, std::nullptr_t*, Task* task) {
    // env is nullptr when the queue is torn down before the task runs
    if (env != nullptr) {
        (*task)(env);
    }

    delete task;
}

using TaskQueue = Napi::TypedThreadSafeFunction<std::nullptr_t, Task, call_task>;

struct SyncEvent {
    Napi::Promise::Deferred deferred;

    double beat;
    double offset;
    double origin;
    double link_beat;
};

static double next_link_beat(double current_beat, double sync_beat, double offset, double origin) {
    double next_beat;
    double i;

    // return current_beat if evenly divisible by sync_beat
    if (modf(current_beat / sync_beat, &i) == 0) {
        return current_beat;
    }

    next_beat = floor((current_beat - origin) / sync_beat) + 1.0;
    next_beat = next_beat * sync_beat + origin;
    next_beat = next_beat + offset;

    while (next_beat <= current_beat) {
        next_beat += sync_beat;
    }

    return std::max(next_beat, 0.0);
}

class Link : public Napi::ObjectWrap<Link> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        // clang-format off
        Napi::Function func = DefineClass(env, "Link", {
            InstanceAccessor<&Link::num_peers>("numPeers"),
            InstanceAccessor<&Link::beat>("beat"),
            InstanceAccessor<&Link::phase>("phase"),
            InstanceAccessor<&Link::time>("time"),
            InstanceAccessor<&Link::quantum, &Link::set_quantum>("quantum"),
            InstanceAccessor<&Link::enabled, &Link::set_enabled>("enabled"),
            InstanceAccessor<&Link::start_stop_sync_enabled, &Link::set_start_stop_sync_enabled>("startStopSyncEnabled"),
            InstanceAccessor<&Link::tempo, &Link::set_tempo>("tempo"),
            InstanceAccessor<&Link::playing, &Link::set_playing>("playing"),
            InstanceMethod<&Link::request_beat>("requestBeat"),
            InstanceMethod<&Link::force_beat>("forceBeat"),
            InstanceMethod<&Link::request_beat_at_start_playing_time>("requestBeatAtStartPlayingTime"),
            InstanceMethod<&Link::set_is_playing_and_request_beat_at_time>("setIsPlayingAndRequestBeatAtTime"),
            InstanceMethod<&Link::set_num_peers_callback>("_setNumPeersCallback"),
            InstanceMethod<&Link::set_tempo_callback>("_setTempoCallback"),
            InstanceMethod<&Link::set_start_stop_callback>("_setStartStopCallback"),
            InstanceMethod<&Link::sync>("sync"),
        });
        // clang-format on

        exports.Set("Link", func);
        return exports;
    }

    Link(const Napi::CallbackInfo& info) : Napi::ObjectWrap<Link>(info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsNumber()) {
            throw Napi::TypeError::New(env, "expected bpm as the first argument");
        }

        double bpm = info[0].As<Napi::Number>().DoubleValue();

        m_link = std::make_unique<ableton::Link>(bpm);
        m_queue = TaskQueue::New(env, "aalink", 0, 1);

        // don't hold the event loop open while no syncs are pending
        m_queue.Unref(env);

        // shutdown threads implicitly on process exit
        m_env = env;
        napi_add_env_cleanup_hook(env, env_cleanup_hook, this);

        m_link->setNumPeersCallback([this](std::size_t num_peers) {
            post([this, num_peers](Napi::Env env) {
                if (!m_num_peers_callback.IsEmpty()) {
                    m_num_peers_callback.Call({Napi::Number::New(env, static_cast<double>(num_peers))});
                }
            });
        });

        m_link->setTempoCallback([this](double tempo) {
            post([this, tempo](Napi::Env env) {
                if (!m_tempo_callback.IsEmpty()) {
                    m_tempo_callback.Call({Napi::Number::New(env, tempo)});
                }
            });
        });

        m_link->setStartStopCallback([this](bool playing) {
            post([this, playing](Napi::Env env) {
                if (!m_start_stop_callback.IsEmpty()) {
                    m_start_stop_callback.Call({Napi::Boolean::New(env, playing)});
                }
            });
        });

        m_scheduler_thread = std::thread(&Link::run_scheduler, this);
    }

    ~Link() {
        shutdown(true);
    }

private:
    static void env_cleanup_hook(void* arg) {
        // node consumes the hook when running it, do not remove it again
        static_cast<Link*>(arg)->shutdown(false);
    }

    void run_scheduler() {
        while (!m_stop_thread.load(std::memory_order_relaxed)) {
            auto link_state = m_link->captureAppSessionState();

            auto link_time = m_link->clock().micros();
            auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
            auto link_beat = link_state.beatAtTime(link_time, link_quantum);

            m_link_beat.store(link_beat, std::memory_order_relaxed);

            std::list<SyncEvent> ready_events;

            {
                std::lock_guard<std::mutex> lock(m_events_mutex);

                for (auto it = m_events.begin(); it != m_events.end();) {
                    if (link_beat > it->link_beat) {
                        ready_events.splice(ready_events.end(), m_events, it++);
                    } else {
                        ++it;
                    }
                }
            }

            for (auto& event : ready_events) {
                auto deferred = event.deferred;
                auto beat = event.link_beat;

                post([this, deferred, beat](Napi::Env env) {
                    deferred.Resolve(Napi::Number::New(env, beat));

                    // release the event loop when the last pending sync resolves
                    if (--m_pending_syncs == 0 && !m_closed.load(std::memory_order_relaxed)) {
                        m_queue.Unref(env);
                    }
                });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // safe to call from any thread until shutdown() releases the queue
    void post(Task task) {
        if (m_closed.load(std::memory_order_relaxed)) {
            return;
        }

        m_queue.NonBlockingCall(new Task(std::move(task)));
    }

    void reschedule_sync_events(double link_beat) {
        std::lock_guard<std::mutex> lock(m_events_mutex);

        for (auto& event : m_events) {
            event.link_beat = next_link_beat(link_beat, event.beat, event.offset, event.origin);
        }

        // update m_link_beat here to ensure that interim sync events will not be scheduled later
        m_link_beat.store(link_beat, std::memory_order_relaxed);
    }

    void shutdown(bool remove_cleanup_hook) {
        // prevent double shutdown from close(), the destructor and the cleanup hook
        if (m_closed.exchange(true)) {
            return;
        }

        if (remove_cleanup_hook) {
            napi_remove_env_cleanup_hook(m_env, env_cleanup_hook, this);
        }

        // neutralize link callbacks before releasing the task queue
        m_link->setNumPeersCallback([](std::size_t) {});
        m_link->setTempoCallback([](double) {});
        m_link->setStartStopCallback([](bool) {});

        m_stop_thread.store(true, std::memory_order_relaxed);
        m_scheduler_thread.join();

        m_link.reset();
        m_queue.Release();
    }

    Napi::Value num_peers(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), static_cast<double>(m_link->numPeers()));
    }

    Napi::Value beat(const Napi::CallbackInfo& info) {
        auto link_state = m_link->captureAppSessionState();
        auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
        return Napi::Number::New(info.Env(), link_state.beatAtTime(m_link->clock().micros(), link_quantum));
    }

    Napi::Value phase(const Napi::CallbackInfo& info) {
        auto link_state = m_link->captureAppSessionState();
        auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
        return Napi::Number::New(info.Env(), link_state.phaseAtTime(m_link->clock().micros(), link_quantum));
    }

    // link clock time in seconds
    Napi::Value time(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), m_link->clock().micros().count() / 1e6);
    }

    Napi::Value quantum(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), m_link_quantum.load(std::memory_order_relaxed));
    }

    void set_quantum(const Napi::CallbackInfo& info, const Napi::Value& value) {
        m_link_quantum.store(value.As<Napi::Number>().DoubleValue(), std::memory_order_relaxed);
    }

    Napi::Value enabled(const Napi::CallbackInfo& info) {
        return Napi::Boolean::New(info.Env(), m_link->isEnabled());
    }

    void set_enabled(const Napi::CallbackInfo& info, const Napi::Value& value) {
        m_link->enable(value.ToBoolean());
    }

    Napi::Value start_stop_sync_enabled(const Napi::CallbackInfo& info) {
        return Napi::Boolean::New(info.Env(), m_link->isStartStopSyncEnabled());
    }

    void set_start_stop_sync_enabled(const Napi::CallbackInfo& info, const Napi::Value& value) {
        m_link->enableStartStopSync(value.ToBoolean());
    }

    Napi::Value tempo(const Napi::CallbackInfo& info) {
        auto link_state = m_link->captureAppSessionState();
        return Napi::Number::New(info.Env(), link_state.tempo());
    }

    void set_tempo(const Napi::CallbackInfo& info, const Napi::Value& value) {
        auto link_state = m_link->captureAppSessionState();
        link_state.setTempo(value.As<Napi::Number>().DoubleValue(), m_link->clock().micros());
        m_link->commitAppSessionState(link_state);
    }

    Napi::Value playing(const Napi::CallbackInfo& info) {
        auto link_state = m_link->captureAppSessionState();
        return Napi::Boolean::New(info.Env(), link_state.isPlaying());
    }

    void set_playing(const Napi::CallbackInfo& info, const Napi::Value& value) {
        auto link_state = m_link->captureAppSessionState();
        link_state.setIsPlaying(value.ToBoolean(), m_link->clock().micros());
        m_link->commitAppSessionState(link_state);
    }

    Napi::Value request_beat(const Napi::CallbackInfo& info) {
        double beat = info[0].As<Napi::Number>().DoubleValue();

        auto link_state = m_link->captureAppSessionState();
        auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
        link_state.requestBeatAtTime(beat, m_link->clock().micros(), link_quantum);
        m_link->commitAppSessionState(link_state);

        reschedule_sync_events(beat);
        return info.Env().Undefined();
    }

    Napi::Value force_beat(const Napi::CallbackInfo& info) {
        double beat = info[0].As<Napi::Number>().DoubleValue();

        auto link_state = m_link->captureAppSessionState();
        auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
        link_state.forceBeatAtTime(beat, m_link->clock().micros(), link_quantum);
        m_link->commitAppSessionState(link_state);

        reschedule_sync_events(beat);
        return info.Env().Undefined();
    }

    Napi::Value request_beat_at_start_playing_time(const Napi::CallbackInfo& info) {
        double beat = info[0].As<Napi::Number>().DoubleValue();

        auto link_state = m_link->captureAppSessionState();
        auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
        link_state.requestBeatAtStartPlayingTime(beat, link_quantum);
        m_link->commitAppSessionState(link_state);

        reschedule_sync_events(beat);
        return info.Env().Undefined();
    }

    Napi::Value set_is_playing_and_request_beat_at_time(const Napi::CallbackInfo& info) {
        bool playing = info[0].ToBoolean();
        double time_seconds = info[1].As<Napi::Number>().DoubleValue();
        double beat = info[2].As<Napi::Number>().DoubleValue();

        auto time = std::chrono::microseconds(static_cast<int64_t>(time_seconds * 1e6));

        auto link_state = m_link->captureAppSessionState();
        auto link_quantum = m_link_quantum.load(std::memory_order_relaxed);
        link_state.setIsPlayingAndRequestBeatAtTime(playing, time, beat, link_quantum);
        m_link->commitAppSessionState(link_state);

        reschedule_sync_events(beat);
        return info.Env().Undefined();
    }

    static void assign_callback(const Napi::CallbackInfo& info, Napi::FunctionReference& ref) {
        if (info[0].IsFunction()) {
            ref = Napi::Persistent(info[0].As<Napi::Function>());
        } else {
            ref.Reset();
        }
    }

    Napi::Value set_num_peers_callback(const Napi::CallbackInfo& info) {
        assign_callback(info, m_num_peers_callback);
        return info.Env().Undefined();
    }

    Napi::Value set_tempo_callback(const Napi::CallbackInfo& info) {
        assign_callback(info, m_tempo_callback);
        return info.Env().Undefined();
    }

    Napi::Value set_start_stop_callback(const Napi::CallbackInfo& info) {
        assign_callback(info, m_start_stop_callback);
        return info.Env().Undefined();
    }

    Napi::Value sync(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        double beat = info.Length() > 0 ? info[0].ToNumber().DoubleValue() : 0;
        double offset = info.Length() > 1 ? info[1].ToNumber().DoubleValue() : 0;
        double origin = info.Length() > 2 ? info[2].ToNumber().DoubleValue() : 0;

        // prevent hanging in next_link_beat on non-positive or NaN beats
        if (!(beat > 0)) {
            throw Napi::RangeError::New(env, "invalid beat value");
        }

        auto deferred = Napi::Promise::Deferred::New(env);
        auto link_beat = m_link_beat.load(std::memory_order_relaxed);

        SyncEvent event = {
            deferred,
            beat,
            offset,
            origin,
            next_link_beat(link_beat, beat, offset, origin),
        };

        {
            std::lock_guard<std::mutex> lock(m_events_mutex);
            m_events.push_back(std::move(event));
        }

        // hold the event loop open until the sync resolves
        if (m_pending_syncs++ == 0) {
            m_queue.Ref(env);
        }

        return deferred.Promise();
    }

    std::unique_ptr<ableton::Link> m_link;
    TaskQueue m_queue;
    napi_env m_env;

    // modified on the JS thread only
    size_t m_pending_syncs{0};

    std::thread m_scheduler_thread;
    std::atomic<bool> m_stop_thread{false};
    std::atomic<bool> m_closed{false};

    std::mutex m_events_mutex;
    std::list<SyncEvent> m_events;

    std::atomic<double> m_link_beat{0};
    std::atomic<double> m_link_quantum{1};

    Napi::FunctionReference m_num_peers_callback;
    Napi::FunctionReference m_tempo_callback;
    Napi::FunctionReference m_start_stop_callback;
};

static Napi::Object init_module(Napi::Env env, Napi::Object exports) {
    return Link::Init(env, exports);
}

NODE_API_MODULE(aalink, init_module)
