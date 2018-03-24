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

Precision
---------

If the FM signal is clear (you can hear the station without static) the measurements should be repeatable to within 1ppm of each run. Radio stations do not necessarily have to keep a stable signal though and some transmitting error is allowed.

General approch: If you can run the check against 3 or more strong stations and get same result, you could assume that's the right PPM. If you have fewer stations or varied results, use the result as an inital value to a more precise app like https://github.com/viraptor/kalibrate-rtl

Build
-----

Dependencies:
- liquid-dsp
- SoapySDR

Build with:

    make
