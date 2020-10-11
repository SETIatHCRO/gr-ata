To install and run this module, you'll need to follow these prerequisite steps:

```
git clone https://github.com/SETIatHCRO/ATA-Utils
cd ATA-Utils/pythonLibs
sudo pip install .
```

Install gr-ata by doing:

```
git clone https://github.com/SETIatHCRO/gr-ata
cd gr-ata
mkdir build
cd build
cmake ..
make
sudo make install
sudo ldconfig
```

The latest version has a SNAP source block that supports the new SNAP boards.  These boards have a voltage mode and spectrometer mode.  The voltage mode output is the output of a PFB->FFT conversion, whereas the spectrometer mode is a running accumulator/integrator for XX, YY, and XY* polarizations.  There are example flowgraphs in the examples directory.  Note that for the snap_source.grc (voltage) example, you will need to install gr-mesa (which also requires gr-lfast) for the long-term integrator block.  (Just make sure if you're running GNURadio 3.8, that you "git clone --branch maint-3.8" for each of those OOT modules.
