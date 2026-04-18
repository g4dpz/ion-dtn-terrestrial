#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: APRS Beacon TX (B200 mini)
# Author: David Johnson, G4DPZ
# Copyright: 2026 AMSAT-UK
# Description: APRS position beacon transmitter using Bell 202 AFSK over FM
# GNU Radio version: 3.10.9.2

from gnuradio import analog
from gnuradio import blocks
from gnuradio import filter
from gnuradio.filter import firdes
from gnuradio import gr
from gnuradio.fft import window
import sys
import signal
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
from gnuradio import uhd
import time




class aprs_beacon_tx(gr.top_block):

    def __init__(self):
        gr.top_block.__init__(self, "APRS Beacon TX (B200 mini)", catch_exceptions=True)

        ##################################################
        # Variables
        ##################################################
        self.fm_deviation = fm_deviation = 3000
        self.audio_rate = audio_rate = 48000
        self.tx_gain = tx_gain = 89
        self.samp_rate = samp_rate = 480000
        self.fm_sensitivity = fm_sensitivity = 2*3.14159265*fm_deviation/audio_rate
        self.centre_freq = centre_freq = 144.85e6

        ##################################################
        # Blocks
        ##################################################

        self.wav_source = blocks.wavfile_source('aprs_beacon.wav', True)
        self.uhd_sink = uhd.usrp_sink(
            ",".join(("", "")),
            uhd.stream_args(
                cpu_format="fc32",
                args='',
                channels=list(range(0,1)),
            ),
            "",
        )
        self.uhd_sink.set_samp_rate(samp_rate)
        self.uhd_sink.set_time_unknown_pps(uhd.time_spec(0))

        self.uhd_sink.set_center_freq(centre_freq, 0)
        self.uhd_sink.set_antenna('TX/RX', 0)
        self.uhd_sink.set_gain(tx_gain, 0)
        self.resampler = filter.rational_resampler_ccc(
                interpolation=(int(samp_rate/audio_rate)),
                decimation=1,
                taps=[],
                fractional_bw=0)
        self.fm_mod = analog.frequency_modulator_fc(fm_sensitivity)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.fm_mod, 0), (self.resampler, 0))
        self.connect((self.resampler, 0), (self.uhd_sink, 0))
        self.connect((self.wav_source, 0), (self.fm_mod, 0))


    def get_fm_deviation(self):
        return self.fm_deviation

    def set_fm_deviation(self, fm_deviation):
        self.fm_deviation = fm_deviation
        self.set_fm_sensitivity(2*3.14159265*self.fm_deviation/self.audio_rate)

    def get_audio_rate(self):
        return self.audio_rate

    def set_audio_rate(self, audio_rate):
        self.audio_rate = audio_rate
        self.set_fm_sensitivity(2*3.14159265*self.fm_deviation/self.audio_rate)

    def get_tx_gain(self):
        return self.tx_gain

    def set_tx_gain(self, tx_gain):
        self.tx_gain = tx_gain
        self.uhd_sink.set_gain(self.tx_gain, 0)

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.uhd_sink.set_samp_rate(self.samp_rate)

    def get_fm_sensitivity(self):
        return self.fm_sensitivity

    def set_fm_sensitivity(self, fm_sensitivity):
        self.fm_sensitivity = fm_sensitivity
        self.fm_mod.set_sensitivity(self.fm_sensitivity)

    def get_centre_freq(self):
        return self.centre_freq

    def set_centre_freq(self, centre_freq):
        self.centre_freq = centre_freq
        self.uhd_sink.set_center_freq(self.centre_freq, 0)




def main(top_block_cls=aprs_beacon_tx, options=None):
    tb = top_block_cls()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        sys.exit(0)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    tb.start()

    tb.wait()


if __name__ == '__main__':
    main()
