#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <cstdint>
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/udp_socket.h"

namespace cameraunlock {
/// Routes receiver diagnostics to cameraunlock::logging by default.
std::function<void(const std::string&)> DefaultLogSink();
}  // namespace cameraunlock

namespace cameraunlock {

/// UDP receiver for OpenTrack protocol.
/// Thread-safe with lock-free reads on the game thread.
class UdpReceiver {
public:
    /// Default OpenTrack UDP port.
    static constexpr uint16_t kDefaultPort = 4242;

    /// Connection timeout in milliseconds.
    /// Lower than PollingUdpReceiver (500 vs 1000) because the threaded receiver
    /// checks more frequently and can detect disconnects sooner.
    static constexpr int kConnectionTimeoutMs = 500;

    // Separate from the connection-liveness timeout above. Re-arming trailer
    // first-sighting is a wire-contract rule fixed at ~5s of packet silence (see
    // AGENTS.md and OpenTrackReceiver.cs, which implements 50 x 100ms). Reusing the
    // 500ms liveness value meant a routine Wi-Fi stall inside a recenter burst re-armed
    // mid-burst, so the burst's tail - carrying the SAME counter - read as a second
    // press and recentred on whatever pose the head had drifted to.
    static constexpr int kRecenterRearmMs = 5000;
    /// How long the chosen tracker may go quiet before another sender is
    /// allowed to take over. Long enough that ordinary jitter or a dropped
    /// packet never hands control to a second app mid-session.
    static constexpr int kSourceHandoverMs = 2000;

    /// The FIRST pose change larger than this is held back one packet and only
    /// accepted if the next packet differs from it.
    ///
    /// Note what this measures: degrees per PACKET, not per second, so what
    /// counts as large depends entirely on the tracker's sample rate. 300 deg/s
    /// is 5 degrees a packet at 60 Hz but 9 at 33 Hz, and eye trackers commonly
    /// run at the low end - so ordinary movement DOES cross this on real
    /// hardware. That is survivable only because a continuing movement is never
    /// held (see m_lastStepWasLarge); holding every large step rejects alternate
    /// packets and publishes a pose that alternates between current and stale.
    /// A pose change larger than this waits one packet for the next one to land
    /// near it before it is published. Ordinary head movement between packets is
    /// far smaller; a tracker snapping to its "lost the head" pose is far
    /// larger, and the packet after it corroborates the snap rather than
    /// continuing the movement.
    static constexpr float kConfirmJumpDegrees = 8.0f;

    /// Interval between bind retries when the port is held by another process.
    /// Short on purpose: this is the only path that reclaims the port, so it
    /// covers both a slow-exiting previous game instance (sub-second) and a
    /// tracker app the user quits mid-session, which should be picked up
    /// promptly rather than feeling broken.
    static constexpr int kRetryIntervalMs = 500;

    /// Interval between "still waiting" retry log messages.
    static constexpr int kRetryLogIntervalMs = 30000;

    UdpReceiver() = default;
    ~UdpReceiver();

    // Non-copyable
    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    /// Starts the UDP receiver on the specified port and a supervisor thread
    /// that keeps it listening for as long as the receiver lives. If the port
    /// is already in use, Start returns false and the supervisor retries every
    /// kRetryIntervalMs until it frees up, with no further action from the
    /// caller; once it binds, the receive thread starts and IsRunning becomes
    /// true. The supervisor also re-establishes the socket if the receive
    /// thread dies on a socket error.
    /// @return True if bound and the receive thread started immediately.
    bool Start(uint16_t port = kDefaultPort);

    /// Stops the UDP receiver. Joins the supervisor and receive threads,
    /// closes the socket, and clears tracking state.
    void Stop();

    /// Optional logging callback for bind failures and retry messages.
    /// Must be thread-safe: invoked from Start (caller thread) and from the
    /// background retry thread.
    /// Diagnostic sink. Defaults to the shared file log rather than to nothing:
    /// forgetting this call used to silently discard the bind result and the
    /// latched first-packet line, which are the two the "no head tracking"
    /// reports turn on. Mods with their own logger override it here; mods that
    /// never open a core log get a no-op.
    void SetLog(std::function<void(const std::string&)> log) { m_log = std::move(log); }

    /// True if the receive thread is running.
    bool IsRunning() const { return m_running.load(std::memory_order_acquire); }

    /// True if the supervisor is retrying the bind (port currently unavailable).
    bool IsRetrying() const { return m_retrying.load(std::memory_order_acquire); }

    /// True if data has been received recently.
    bool IsReceiving() const;

    /// True if the data source is from a remote address.
    bool IsRemoteConnection() const { return m_isRemoteConnection.load(std::memory_order_relaxed); }

    /// True if the most recent bind attempt failed. Cleared once retry succeeds.
    bool IsFailed() const { return m_failed.load(std::memory_order_acquire); }

    /// Timestamp of the last received packet (microseconds since epoch).
    /// Compare across frames to detect new samples for interpolation.
    int64_t GetLastReceiveTimestamp() const { return m_lastReceiveTimestamp.load(std::memory_order_relaxed); }

    /// Gets the current rotation values with offset applied.
    /// @return True if data is available.
    bool GetRotation(float& yaw, float& pitch, float& roll) const;

    /// Gets the current position values with offset applied.
    /// @return True if position data is available.
    bool GetPosition(float& x, float& y, float& z) const;

    /// Sets the current rotation and position as the new center point.
    void Recenter();

    /// Clears the centering offset, so raw tracker values pass through untouched.
    /// PollingUdpReceiver and the C# OpenTrackReceiver have always had this; its
    /// absence here was a public-API asymmetry between two receivers of one protocol.
    void ResetOffset();

    /// Always false. The HCAM trailer is still parsed, but it no longer raises a
    /// recenter request: Headcam zeroes its own output on CENTER and the pipeline's
    /// centre is identity, so the zeroed stream is already correct. Kept on the API
    /// because mods call it; they simply never see a press.
    bool TryConsumeRecenterRequest() {
        return m_recenterRequested.exchange(false, std::memory_order_acq_rel);
    }

    /// Packets ignored because the tracker had stopped tracking and was
    /// repeating one pose. Non-zero means head tracking survived a dropout that
    /// would otherwise have swung the view to centre and back.
    uint64_t GetFrozenPacketCount() const {
        return m_frozenPackets.load(std::memory_order_relaxed);
    }

    /// Step to a different tracker app when the wrong one won the startup race.
    /// Safe to call from any thread; it takes effect on the next packet.
    void CycleSource() {
        m_cycleRequestedAtUs.store(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_relaxed);
        m_cycleRequested.store(true, std::memory_order_release);
    }

    /// Packets dropped because they came from a second tracker source. Non-zero
    /// means two apps are sending to this port and one of them is being ignored.
    uint64_t GetRejectedPacketCount() const {
        return m_rejectedPackets.load(std::memory_order_relaxed);
    }

private:
    bool AcceptPose(const TrackingPose& pose, bool repeatsPrevious);
    void SeedPoseGate(const TrackingPose& pose);
    void ReceiverThread();
    void SupervisorThread();
    bool BindAndReceive();
    void StopReceiverThread();

    // Thread-safe tracking data
    TrackingData m_trackingData;

    UdpSocket m_socket;
    std::thread m_thread;
    std::thread m_supervisorThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopFlag{false};
    std::atomic<bool> m_supervising{false};
    std::atomic<bool> m_retrying{false};
    std::atomic<bool> m_failed{false};
    /// Set by the receive thread when it gives up on a socket error, so the
    /// supervisor re-establishes the socket instead of leaving the receiver
    /// bound and permanently deaf.
    std::atomic<bool> m_receiveFailed{false};
    uint16_t m_port{kDefaultPort};
    std::function<void(const std::string&)> m_log = DefaultLogSink();

    // Offset for recentering
    std::atomic<float> m_yawOffset{0.0f};
    std::atomic<float> m_pitchOffset{0.0f};
    std::atomic<float> m_rollOffset{0.0f};
    std::atomic<float> m_posXOffset{0.0f};
    std::atomic<float> m_posYOffset{0.0f};
    std::atomic<float> m_posZOffset{0.0f};

    // Position data (mm, from OpenTrack)
    std::atomic<float> m_posX{0.0f};
    std::atomic<float> m_posY{0.0f};
    std::atomic<float> m_posZ{0.0f};
    std::atomic<bool> m_hasPosition{false};

    // Remote recenter (Headcam trailer). The trailer only rides packets sent
    // right after a CENTER press, so any sighting with a new counter is a
    // press. Counter state is receive-thread-only; Stop() resets it after
    // the join.
    std::atomic<bool> m_recenterRequested{false};
    uint8_t m_lastRecenterCounter{0};
    bool m_hasRecenterCounter{false};

    // Timestamp for connection detection
    std::atomic<int64_t> m_lastReceiveTimestamp{0};
    std::atomic<bool> m_isRemoteConnection{false};

    // The first sender seen, and anything else that turns up.
    //
    // Two tracker apps pointed at this port both get through, and the head pose
    // then alternates between them packet by packet - which looks exactly like
    // the view flicking between two positions, in every camera mode, and is
    // impossible to tell from a mod bug without knowing to look. Receive-thread
    // only; the counter is atomic so the outside can report it.
    uint64_t m_primarySource{0};
    // The source cycled away from, so the next lock does not land back on it.
    uint64_t m_avoidSource{0};
    std::atomic<bool> m_cycleRequested{false};
    std::atomic<int64_t> m_cycleRequestedAtUs{0};
    int64_t m_primaryLastSeenUs{0};
    float m_lastSourceYaw{0.0f};
    float m_lastSourcePitch{0.0f};
    float m_lastSourceRoll{0.0f};
    std::atomic<uint64_t> m_rejectedPackets{0};

    /// Dropout rejection: the last pose actually published, and whether a large
    /// jump is waiting to be confirmed by a following packet that differs from
    /// it.
    TrackingPose m_acceptedPose;
    bool m_hasAcceptedPose{false};
    bool m_pendingValid{false};
    /// True when the last accepted step was itself larger than the threshold, so
    /// the head is mid-movement rather than starting one. Only the FIRST large
    /// step of a movement is held for confirmation; holding every one rejects
    /// alternate packets for as long as the movement lasts and publishes a pose
    /// that alternates between current and stale.
    bool m_lastStepWasLarge{false};
    std::atomic<uint64_t> m_frozenPackets{0};

    static constexpr int kMaxSeenSources = 8;
    uint64_t m_seenSources[kMaxSeenSources]{};
    int m_seenSourceCount{0};
};

}  // namespace cameraunlock
