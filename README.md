FM_tune
=======

Run with:

    ./fm_tune <radio freq in MHz>
    ./fm_tune 101.3

Gives a rough PPM estimation which can be used with something more precise like kalibrate-rtl.

How does it work
----------------

1. Get samples at 1.8Msps at requested freq
2. Lowpass to isolate one station
3. FM demod
4. Decimate to ~50kHz
5. Bandpass to filter only the FM pilot signal (19kHz)
6. Adjust the frequency until the pilot signal is centered around 0

Build
-----

Dependencies:
- liquid-dsp
- SoapySDR

Build with:

    make
