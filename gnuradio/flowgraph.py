"""GNU Radio flowgraph for APRS beacon transmission.

Signal chain: Vector Source -> NBFM TX -> Rational Resampler -> UHD Sink

A fresh top_block is created for each transmission burst to ensure
clean UHD streaming state.
"""

import numpy as np

BAND_LOW_HZ = 144.0e6
BAND_HIGH_HZ = 146.0e6

# Try to import GNU Radio; allow graceful failure for testing without hardware
try:
    from gnuradio import gr, analog, blocks, filter as gr_filter, uhd
    HAS_GNURADIO = True
except ImportError:
    HAS_GNURADIO = False


class APRSFlowgraph:
    """GNU Radio flowgraph for FM-modulated APRS transmission via B200 mini."""

    def __init__(self, freq_hz: float, gain: int, sdr_sample_rate: int,
                 audio_sample_rate: int = 48000, fm_deviation: float = 3500.0):
        """Initialise and verify hardware.

        Args:
            freq_hz: Centre frequency in Hz (e.g. 144.85e6).
            gain: Transmit gain (0-89).
            sdr_sample_rate: SDR sample rate in Hz (e.g. 480000).
            audio_sample_rate: Input audio sample rate in Hz.
            fm_deviation: FM deviation in Hz (default 3500).
        """
        if not HAS_GNURADIO:
            raise RuntimeError("GNU Radio is not installed")

        if freq_hz < BAND_LOW_HZ or freq_hz > BAND_HIGH_HZ:
            raise ValueError(
                f"frequency {freq_hz/1e6:.3f} MHz outside 2m band "
                f"({BAND_LOW_HZ/1e6:.3f}-{BAND_HIGH_HZ/1e6:.3f} MHz)"
            )

        self.freq_hz = freq_hz
        self.gain = gain
        self.sdr_sample_rate = sdr_sample_rate
        self.audio_sample_rate = audio_sample_rate
        self.fm_deviation = fm_deviation
        self.interp = sdr_sample_rate // audio_sample_rate

        # Verify B200 is present by probing UHD
        try:
            test_sink = uhd.usrp_sink(
                device_addr="",
                stream_args=uhd.stream_args(cpu_format="fc32", channels=[0]),
            )
            del test_sink
        except Exception as e:
            raise RuntimeError(f"B200 mini not detected: {e}")

    def transmit(self, audio_samples) -> None:
        """Transmit a burst of audio samples through the flowgraph.

        Creates a fresh top_block for each burst to ensure clean
        UHD streaming state.

        Args:
            audio_samples: numpy float32 array of AFSK audio.
        """
        # Add preamble silence (for TX ramp-up) and tail silence (ramp-down)
        # 1s each to allow B200 TX chain and receiver squelch to settle
        preamble = np.zeros(self.audio_sample_rate, dtype=np.float32)
        tail = np.zeros(self.audio_sample_rate, dtype=np.float32)
        data = np.concatenate([preamble, audio_samples, tail])

        tb = gr.top_block()

        src = blocks.vector_source_f(data.tolist(), False)

        # Direct FM modulation: audio -> frequency_modulator_fc
        # sensitivity = 2*pi*max_deviation / sample_rate
        import math
        sensitivity = 2.0 * math.pi * self.fm_deviation / self.audio_sample_rate
        fm_mod = analog.frequency_modulator_fc(sensitivity)

        resampler = gr_filter.rational_resampler_ccc(
            interpolation=self.interp,
            decimation=1,
        )

        usrp = uhd.usrp_sink(
            device_addr="",
            stream_args=uhd.stream_args(cpu_format="fc32", channels=[0]),
        )
        usrp.set_samp_rate(self.sdr_sample_rate)
        usrp.set_center_freq(self.freq_hz)
        usrp.set_gain(self.gain)

        tb.connect(src, fm_mod, resampler, usrp)
        tb.run()
        tb.wait()

    def stop(self) -> None:
        """No-op — each transmit() creates and cleans up its own top_block."""
        pass
