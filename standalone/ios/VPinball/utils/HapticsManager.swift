import CoreHaptics

final class HapticsManager {
    static let shared = HapticsManager()

    private let queue = DispatchQueue(label: "org.vpinball.haptics")
    private var engine: CHHapticEngine?
    private var player: CHHapticAdvancedPatternPlayer?
    private var needsRestart = false
    private var isRunning = false
    private var stopTime = DispatchTime.now()

    private init() {}

    func start() {
        queue.async {
            guard CHHapticEngine.capabilitiesForHardware().supportsHaptics,
                  let engine = try? CHHapticEngine()
            else {
                return
            }

            engine.playsHapticsOnly = true
            engine.stoppedHandler = { [weak self] _ in
                self?.markForRestart()
            }
            engine.resetHandler = { [weak self] in
                self?.markForRestart()
            }

            self.engine = engine
            self.restart()
        }
    }

    func play(lowFrequencySpeed: Float, highFrequencySpeed: Float, durationMs: UInt32) {
        queue.async {
            self.rumble(lowFrequencySpeed: lowFrequencySpeed,
                        highFrequencySpeed: highFrequencySpeed,
                        durationMs: durationMs)
        }
    }

    private func markForRestart() {
        queue.async {
            self.needsRestart = true
        }
    }

    private func restart() {
        guard let engine else {
            return
        }

        needsRestart = false
        isRunning = false
        player = nil

        do {
            try engine.start()
            let event = CHHapticEvent(
                eventType: .hapticContinuous,
                parameters: [
                    CHHapticEventParameter(parameterID: .hapticIntensity, value: 1),
                    CHHapticEventParameter(parameterID: .hapticSharpness, value: 0),
                ],
                relativeTime: 0,
                duration: 30
            )
            let player = try engine.makeAdvancedPlayer(with: CHHapticPattern(events: [event], parameters: []))
            player.loopEnabled = true
            self.player = player
        } catch {
            needsRestart = true
        }
    }

    private func rumble(lowFrequencySpeed low: Float, highFrequencySpeed high: Float, durationMs: UInt32) {
        if needsRestart {
            restart()
        }

        guard let player else {
            return
        }

        let intensity = max(low, high)
        guard intensity > 0 else {
            stop()
            return
        }

        do {
            if !isRunning {
                try player.start(atTime: CHHapticTimeImmediate)
                isRunning = true
            }

            try player.sendParameters([
                CHHapticDynamicParameter(parameterID: .hapticIntensityControl, value: cbrtf(intensity), relativeTime: 0),
                CHHapticDynamicParameter(parameterID: .hapticSharpnessControl, value: high / (low + high), relativeTime: 0),
            ], atTime: CHHapticTimeImmediate)

            let stopAt = DispatchTime.now() + .milliseconds(Int(durationMs))
            stopTime = stopAt
            queue.asyncAfter(deadline: stopAt) { [weak self] in
                guard let self, self.stopTime == stopAt else {
                    return
                }
                self.stop()
            }
        } catch {
            needsRestart = true
        }
    }

    private func stop() {
        guard isRunning, let player else {
            return
        }

        isRunning = false
        try? player.stop(atTime: CHHapticTimeImmediate)
    }
}
