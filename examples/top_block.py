#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: Sigmf test
# GNU Radio version: 3.9.0.0-git

from distutils.version import StrictVersion

if __name__ == '__main__':
    import ctypes
    import sys
    if sys.platform.startswith('linux'):
        try:
            x11 = ctypes.cdll.LoadLibrary('libX11.so')
            x11.XInitThreads()
        except:
            print("Warning: failed to XInitThreads()")

from PyQt5 import Qt
from gnuradio import qtgui
import sip
from gnuradio import blocks
import pmt
from gnuradio import fft
from gnuradio.fft import window
from gnuradio import gr
from gnuradio.filter import firdes
import sys
import signal
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
import v  # embedded python module

from gnuradio import qtgui

class top_block(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "Sigmf test", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("Sigmf test")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except:
            pass
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("GNU Radio", "top_block")

        try:
            if StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
                self.restoreGeometry(self.settings.value("geometry").toByteArray())
            else:
                self.restoreGeometry(self.settings.value("geometry"))
        except:
            pass

        ##################################################
        # Variables
        ##################################################
        self.samp_rate = samp_rate = 50000000/2
        self.rest_freq = rest_freq = 1420405752
        self.cfreq = cfreq = 1420000000
        self.c = c = 300000000
        self.vmin = vmin = c*(1 - (cfreq + (samp_rate))/rest_freq)
        self.vmax = vmax = c*(1 - (cfreq - (samp_rate))/rest_freq)
        self.v_corr = v_corr = v.vlsr_correction('2020-09-16T04:15:07', 17.7611, -29.0028)
        self.fftsize = fftsize = 8192*2

        ##################################################
        # Blocks
        ##################################################
        self.qtgui_vector_sink_f_0_0 = qtgui.vector_sink_f(
            fftsize,
            cfreq - (samp_rate),
            samp_rate/fftsize,
            "Freq",
            "Log PSD",
            "",
            1, # Number of inputs
            None # parent
        )
        self.qtgui_vector_sink_f_0_0.set_update_time(0.10)
        self.qtgui_vector_sink_f_0_0.set_y_axis(0, 20)
        self.qtgui_vector_sink_f_0_0.enable_autoscale(False)
        self.qtgui_vector_sink_f_0_0.enable_grid(False)
        self.qtgui_vector_sink_f_0_0.set_x_axis_units("")
        self.qtgui_vector_sink_f_0_0.set_y_axis_units("")
        self.qtgui_vector_sink_f_0_0.set_ref_level(0)

        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_vector_sink_f_0_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_vector_sink_f_0_0.set_line_label(i, labels[i])
            self.qtgui_vector_sink_f_0_0.set_line_width(i, widths[i])
            self.qtgui_vector_sink_f_0_0.set_line_color(i, colors[i])
            self.qtgui_vector_sink_f_0_0.set_line_alpha(i, alphas[i])

        self._qtgui_vector_sink_f_0_0_win = sip.wrapinstance(self.qtgui_vector_sink_f_0_0.pyqwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_vector_sink_f_0_0_win)
        self.qtgui_vector_sink_f_0 = qtgui.vector_sink_f(
            fftsize,
            (vmin/1000) - v_corr,
            (vmax-vmin)/(fftsize*1000),
            "V_LSR (km/s)",
            "Log PSD",
            "",
            1, # Number of inputs
            None # parent
        )
        self.qtgui_vector_sink_f_0.set_update_time(0.10)
        self.qtgui_vector_sink_f_0.set_y_axis(50, 70)
        self.qtgui_vector_sink_f_0.enable_autoscale(True)
        self.qtgui_vector_sink_f_0.enable_grid(False)
        self.qtgui_vector_sink_f_0.set_x_axis_units("")
        self.qtgui_vector_sink_f_0.set_y_axis_units("")
        self.qtgui_vector_sink_f_0.set_ref_level(0)

        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_vector_sink_f_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_vector_sink_f_0.set_line_label(i, labels[i])
            self.qtgui_vector_sink_f_0.set_line_width(i, widths[i])
            self.qtgui_vector_sink_f_0.set_line_color(i, colors[i])
            self.qtgui_vector_sink_f_0.set_line_alpha(i, alphas[i])

        self._qtgui_vector_sink_f_0_win = sip.wrapinstance(self.qtgui_vector_sink_f_0.pyqwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_vector_sink_f_0_win)
        self.fft_vxx_0 = fft.fft_vcc(fftsize, True, window.blackmanharris(fftsize), True, 1)
        self.blocks_stream_to_vector_0 = blocks.stream_to_vector(gr.sizeof_gr_complex*1, fftsize)
        self.blocks_nlog10_ff_0 = blocks.nlog10_ff(10, fftsize, -30)
        self.blocks_integrate_xx_0 = blocks.integrate_ff(1000, fftsize)
        self.blocks_file_source_0 = blocks.file_source(gr.sizeof_gr_complex*1, '/home/ewhite/saga_raw_2020-09-16_041507.179121.dat', True, 0, 0)
        self.blocks_file_source_0.set_begin_tag(pmt.PMT_NIL)
        self.blocks_complex_to_mag_squared_0 = blocks.complex_to_mag_squared(fftsize)



        ##################################################
        # Connections
        ##################################################
        self.connect((self.blocks_complex_to_mag_squared_0, 0), (self.blocks_integrate_xx_0, 0))
        self.connect((self.blocks_file_source_0, 0), (self.blocks_stream_to_vector_0, 0))
        self.connect((self.blocks_integrate_xx_0, 0), (self.blocks_nlog10_ff_0, 0))
        self.connect((self.blocks_nlog10_ff_0, 0), (self.qtgui_vector_sink_f_0, 0))
        self.connect((self.blocks_nlog10_ff_0, 0), (self.qtgui_vector_sink_f_0_0, 0))
        self.connect((self.blocks_stream_to_vector_0, 0), (self.fft_vxx_0, 0))
        self.connect((self.fft_vxx_0, 0), (self.blocks_complex_to_mag_squared_0, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("GNU Radio", "top_block")
        self.settings.setValue("geometry", self.saveGeometry())
        event.accept()

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.set_vmax(self.c*(1 - (self.cfreq - (self.samp_rate))/self.rest_freq))
        self.set_vmin(self.c*(1 - (self.cfreq + (self.samp_rate))/self.rest_freq))
        self.qtgui_vector_sink_f_0_0.set_x_axis(self.cfreq - (self.samp_rate), self.samp_rate/self.fftsize)

    def get_rest_freq(self):
        return self.rest_freq

    def set_rest_freq(self, rest_freq):
        self.rest_freq = rest_freq
        self.set_vmax(self.c*(1 - (self.cfreq - (self.samp_rate))/self.rest_freq))
        self.set_vmin(self.c*(1 - (self.cfreq + (self.samp_rate))/self.rest_freq))

    def get_cfreq(self):
        return self.cfreq

    def set_cfreq(self, cfreq):
        self.cfreq = cfreq
        self.set_vmax(self.c*(1 - (self.cfreq - (self.samp_rate))/self.rest_freq))
        self.set_vmin(self.c*(1 - (self.cfreq + (self.samp_rate))/self.rest_freq))
        self.qtgui_vector_sink_f_0_0.set_x_axis(self.cfreq - (self.samp_rate), self.samp_rate/self.fftsize)

    def get_c(self):
        return self.c

    def set_c(self, c):
        self.c = c
        self.set_vmax(self.c*(1 - (self.cfreq - (self.samp_rate))/self.rest_freq))
        self.set_vmin(self.c*(1 - (self.cfreq + (self.samp_rate))/self.rest_freq))

    def get_vmin(self):
        return self.vmin

    def set_vmin(self, vmin):
        self.vmin = vmin
        self.qtgui_vector_sink_f_0.set_x_axis((self.vmin/1000) - self.v_corr, (self.vmax-self.vmin)/(self.fftsize*1000))

    def get_vmax(self):
        return self.vmax

    def set_vmax(self, vmax):
        self.vmax = vmax
        self.qtgui_vector_sink_f_0.set_x_axis((self.vmin/1000) - self.v_corr, (self.vmax-self.vmin)/(self.fftsize*1000))

    def get_v_corr(self):
        return self.v_corr

    def set_v_corr(self, v_corr):
        self.v_corr = v_corr
        self.qtgui_vector_sink_f_0.set_x_axis((self.vmin/1000) - self.v_corr, (self.vmax-self.vmin)/(self.fftsize*1000))

    def get_fftsize(self):
        return self.fftsize

    def set_fftsize(self, fftsize):
        self.fftsize = fftsize
        self.qtgui_vector_sink_f_0.set_x_axis((self.vmin/1000) - self.v_corr, (self.vmax-self.vmin)/(self.fftsize*1000))
        self.qtgui_vector_sink_f_0_0.set_x_axis(self.cfreq - (self.samp_rate), self.samp_rate/self.fftsize)





def main(top_block_cls=top_block, options=None):

    if StrictVersion("4.5.0") <= StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
        style = gr.prefs().get_string('qtgui', 'style', 'raster')
        Qt.QApplication.setGraphicsSystem(style)
    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()

    tb.show()

    def sig_handler(sig=None, frame=None):
        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    def quitting():
        tb.stop()
        tb.wait()

    qapp.aboutToQuit.connect(quitting)
    qapp.exec_()

if __name__ == '__main__':
    main()
